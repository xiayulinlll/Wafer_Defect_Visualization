#include "ui/CaptureController.h"

#include "business/SceneStateManager.h"
#include "render/SceneRenderer.h"
#include "ui/WaferViewport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace wafer {

namespace {
/** 小于该像素边长的框选区域视为误操作，直接忽略 */
constexpr int kMinCaptureSize = 8;
} // namespace

CaptureController::CaptureController(SceneRenderer *renderer, SceneStateManager *state,
                                     WaferViewport *viewport, QObject *parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_state(state)
    , m_viewport(viewport)
{
}

QString CaptureController::defaultOutputDir()
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty())
        return QDir(pictures).absoluteFilePath(QStringLiteral("WaferDefectScreenshots"));
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("screenshots"));
}

CaptureController::CaptureResult
CaptureController::captureRegion(const QRect &widgetRect, bool autoSave, const QString &baseName,
                                 const SaveFileNameProvider &saveFileNameProvider)
{
    CaptureResult result;

    if (!m_renderer || !m_viewport) {
        result.error = QStringLiteral("renderer/viewport not available");
        return result;
    }

    // 与视口求交，避免越界区域导致 FBO 尺寸异常
    const QRect rect =
        widgetRect.intersected(QRect(0, 0, m_viewport->width(), m_viewport->height()));
    if (rect.width() < kMinCaptureSize || rect.height() < kMinCaptureSize) {
        result.error = QStringLiteral("region too small");
        return result;
    }
    result.region = rect;

    QDir dir(defaultOutputDir());
    dir.mkpath(QStringLiteral("."));

    // ---- 推导输出路径 ----
    QString path;
    if (autoSave && !baseName.isEmpty()) {
        // 自动命名带毫秒时间戳：旧命名只有「尺寸」，同一区域连续截图会静默覆盖上一张。
        // 若极端情况下仍撞名（同毫秒），追加序号而不是覆盖。
        const QString stamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        QString candidate = dir.absoluteFilePath(
            QStringLiteral("%1_capture_%2_%3x%4.png")
                .arg(baseName, stamp)
                .arg(rect.width())
                .arg(rect.height()));
        int suffix = 1;
        while (QFileInfo::exists(candidate)) {
            candidate = dir.absoluteFilePath(QStringLiteral("%1_capture_%2_%3x%4_%5.png")
                                                 .arg(baseName, stamp)
                                                 .arg(rect.width())
                                                 .arg(rect.height())
                                                 .arg(suffix++));
        }
        path = candidate;
    } else {
        const QString suggested = dir.absoluteFilePath(
            QStringLiteral("wafer_capture_%1.png")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
        path = saveFileNameProvider ? saveFileNameProvider(suggested) : suggested;
        if (path.isEmpty()) {
            result.cancelled = true;
            return result;
        }
    }

    // ---- 离屏 FBO 渲染：在渲染上下文有效的线程中执行 ----
    // 水印文案：数据集名 + 截图时间，便于报告归档后仍能追溯来源。
    QString caption;
    if (!baseName.isEmpty()) {
        caption = QStringLiteral("%1 | %2 | %3x%4 px")
                      .arg(baseName,
                           QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                      .arg(rect.width())
                      .arg(rect.height());
    }

    QString error;
    m_viewport->makeCurrent();
    const bool ok = m_renderer->captureRegionToFile(rect, m_viewport->size(), path, m_state,
                                                    &error, caption, m_viewport->devicePixelRatioF());
    m_viewport->doneCurrent();
    m_viewport->update();

    result.ok = ok;
    result.path = path;
    result.error = error;
    return result;
}

} // namespace wafer
