#include "core/DefectTypes.h"
#include "core/Logger.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSurfaceFormat>
#include <QTranslator>

#include <cstdio>

/**
 * 程序入口。
 *
 * 关键点：
 *  1) 必须在创建任何 QOpenGLWidget 之前设置 QSurfaceFormat：
 *     OpenGL 3.3 Core Profile + 深度缓冲 + 双缓冲，否则 QOpenGLFunctions_3_3_Core
 *     初始化会失败（难点：跨平台一致的核心模式上下文）。
 *  2) qRegisterMetaType<wafer::DefectDataset>()：使数据集可以通过
 *     跨线程信号槽（QueuedConnection）传递。
 *  3) 早期安装日志系统（落盘 + 控制台），保证启动阶段异常也能追溯。
 *  4) 加载 Qt 自带翻译与项目翻译（zh_CN），并在退出时移除。
 */
namespace {

/** 安装 Qt 基础翻译（如 QMessageBox 按钮文本）与项目翻译 */
void installTranslators(QApplication &app, QTranslator *&qtTranslator,
                        QTranslator *&appTranslator)
{
    const QString locale = QLocale::system().name(); // 例如 zh_CN

    qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(QStringLiteral("qtbase_") + locale,
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(qtTranslator);
    }

    appTranslator = new QTranslator(&app);
    // 项目翻译通过 qt_add_translations 以资源方式内嵌（前缀 /i18n）
    if (appTranslator->load(QStringLiteral(":/i18n/wafer_") + locale)
        || appTranslator->load(QStringLiteral("wafer_") + locale,
                               QCoreApplication::applicationDirPath() + QStringLiteral("/i18n"))) {
        app.installTranslator(appTranslator);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // ---- 0) 尽最早初始化日志，保证后续任何环节的问题都能落盘 ----
    wafer::Logger::instance().initialize();

    // ---- 1) 请求 OpenGL 3.3 Core Profile 上下文 ----
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4); // 4x MSAA，提升标记点边缘质量
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1); // 开启垂直同步，避免 GPU 空转与撕裂
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    // 注意顺序：组织名/应用名必须在创建 MainWindow **之前**设置，
    // 因为 AppSettings 用它定位 QSettings 的 ini 文件名（否则会写到默认的 exe 名下）。
    app.setOrganizationName(QStringLiteral("SemiconductorInspection"));
    app.setApplicationName(QStringLiteral("WaferDefectVisualization"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // ---- 2) 国际化 ----
    QTranslator *qtTranslator = nullptr;
    QTranslator *appTranslator = nullptr;
    installTranslators(app, qtTranslator, appTranslator);

    // ---- 3) 注册跨线程自定义类型 ----
    qRegisterMetaType<wafer::DefectDataset>("wafer::DefectDataset");
    qRegisterMetaType<wafer::DefectDataset>("DefectDataset");

    // ---- 4) 校验实际获得的上下文版本（驱动过旧时给出明确提示） ----
    {
        const QSurfaceFormat effective = QSurfaceFormat::defaultFormat();
        qCInfo(wafer::lcApp) << QStringLiteral("请求的 OpenGL 上下文: %1.%2 Core")
                                    .arg(effective.majorVersion())
                                    .arg(effective.minorVersion());
    }

    wafer::MainWindow window;
    window.show();

    const int exitCode = app.exec();

    // ---- 5) 退出清理：卸载翻译、关闭日志 ----
    if (appTranslator)
        app.removeTranslator(appTranslator);
    if (qtTranslator)
        app.removeTranslator(qtTranslator);
    wafer::Logger::instance().shutdown();

    return exitCode;
}

