#include "ui/WaferViewport.h"

#include "business/PerformanceStats.h"
#include "business/RayPicker.h"
#include "business/SceneStateManager.h"
#include "core/Constants.h"
#include "data/DataBufferManager.h"
#include "data/DefectFilter.h"
#include "render/SceneRenderer.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace wafer {

namespace {
/** 空闲期最小帧间隔：约 30fps。连续拖动筛选/相机时把多次 update() 合并为一次 */
constexpr int kIdleFrameIntervalMs = 33;
} // namespace

WaferViewport::WaferViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(400, 320);
    setCursor(Qt::CrossCursor);

    // 空闲补帧定时器：一帧最多等 ~33ms（30fps），把密集 update() 合并成一次渲染
    m_clock.start();
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setSingleShot(true);
    m_repaintTimer->setInterval(kIdleFrameIntervalMs);
    connect(m_repaintTimer, &QTimer::timeout, this, [this]() { update(); });
}

WaferViewport::~WaferViewport()
{
    // 关键：在上下文仍然有效时释放全部 GPU 资源，避免窗口析构后
    // OpenGL 对象泄漏（难点5）。makeCurrent 由 Qt 在 aboutToBeDestroyed
    // 中已保证有效，这里显式再次 makeCurrent 以防顺序问题。
    if (m_renderer && context()) {
        makeCurrent();
        m_renderer->shutdown();
        doneCurrent();
    }
}

void WaferViewport::attach(SceneRenderer *renderer, SceneStateManager *state,
                           DataBufferManager *bufferManager, DefectFilter *filter)
{
    m_renderer = renderer;
    m_state = state;
    m_bufferManager = bufferManager;
    m_filter = filter;

    if (m_state) {
        m_state->setViewportHeight(height());
        connect(m_state, &SceneStateManager::cameraChanged, this, [this]() {
            update();
            emit cameraChanged();
        });
        connect(m_state, &SceneStateManager::resetCameraRequested, this, [this]() {
            update();
        });
    }
}

QSize WaferViewport::minimumSizeHint() const
{
    return QSize(400, 320);
}

QSize WaferViewport::sizeHint() const
{
    return QSize(1100, 760);
}

void WaferViewport::requestRepaint()
{
    update();
}

void WaferViewport::retryInitialize()
{
    // 1) 已成功初始化过：直接重建上下文没有意义，改为请求重绘
    if (m_rendererInitialized) {
        requestInteractiveFrame();
        return;
    }

    // 2) 先清理可能残留的半初始化资源（失败时可能已创建部分 GL 对象）
    if (m_renderer && context()) {
        makeCurrent();
        m_renderer->shutdown();
        doneCurrent();
    }

    // 3) 设置一个新的 QSurfaceFormat 并重建上下文：
    //    QOpenGLWidget 只有在上下文重建后才会再次调用 initializeGL()。
    QSurfaceFormat fmt = format();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);

    // 显式销毁旧上下文，避免 Qt 复用已失败的实例
    if (QOpenGLContext *old = context()) {
        old->doneCurrent();
        old->deleteLater();
    }
    update();
}

void WaferViewport::requestInteractiveFrame()
{
    m_interactiveFrame = true;
    update();
}

void WaferViewport::setCaptureModeActive(bool active)
{
    m_captureModeActive = active;
    setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
}

void WaferViewport::initializeGL()
{
    if (!m_renderer) {
        m_initError = tr("渲染器未注入");
        emit rendererError(m_initError);
        return;
    }

    QString errorLog;
    const QString shaderDir = QStringLiteral(":/shaders");

    if (!m_renderer->initialize(shaderDir, &errorLog)) {
        m_initError = errorLog.isEmpty() ? tr("渲染器初始化失败") : errorLog;
        emit rendererError(m_initError);
        return;
    }

    m_renderer->setViewportSize(size());
    m_renderer->setDevicePixelRatio(devicePixelRatioF());
    m_rendererInitialized = true;
    m_initError.clear();

    emit rendererReady(
        tr("OpenGL 渲染上下文就绪 | 版本: %1 | 着色器目录: %2")
            .arg(QString::fromLatin1(reinterpret_cast<const char *>(glGetString(GL_VERSION))))
            .arg(shaderDir));
}

void WaferViewport::resizeGL(int w, int h)
{
    if (!m_rendererInitialized || !m_renderer)
        return;

    m_renderer->setViewportSize(QSize(w, h));
    m_renderer->setDevicePixelRatio(devicePixelRatioF());
    if (m_state)
        m_state->setViewportHeight(h); // 平移 1:1 跟手依赖视口高度
}

void WaferViewport::paintGL()
{
    if (!m_rendererInitialized || !m_renderer || !m_state)
        return;

    // 空闲降频（§2-7）：
    //  - 用户交互（拖拽/滚轮/筛选/开关）标记为交互帧，立即渲染；
    //  - 只有连续的程序性 update()（例如拖动滑块引发的连锁重绘）才合并到 30fps，
    //    避免一帧里反复提交相同画面。
    // 窗口最小化时不渲染（Qt 通常也会跳过，这里显式兜底，节省 GPU）
    if (isMinimized())
        return;
    const bool idleThrottled = !m_interactiveFrame && m_lastFrameMs > 0
                               && m_clock.elapsed() - m_lastFrameMs < kIdleFrameIntervalMs;
    if (idleThrottled) {
        if (m_repaintTimer && !m_repaintTimer->isActive())
            m_repaintTimer->start(kIdleFrameIntervalMs);
        return; // 由定时器补帧
    }
    m_interactiveFrame = false;
    m_lastFrameMs = m_clock.elapsed();

    m_renderer->renderFrame(m_state);

    // 框选模式下叠加选区矩形（用 QPainter 绘制 2D 覆盖层，不影响 3D 管线）
    if (m_captureModeActive && m_interaction == Interaction::Capture
        && m_captureRect.width() > 1 && m_captureRect.height() > 1) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPen pen(QColor(90, 200, 255));
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);

        painter.fillRect(m_captureRect, QColor(90, 200, 255, 40));
        painter.drawRect(m_captureRect);

        painter.setPen(QColor(230, 245, 255));
        painter.drawText(m_captureRect.adjusted(4, 4, 0, 0),
                         QStringLiteral("%1 x %2").arg(m_captureRect.width()).arg(
                             m_captureRect.height()));
    }
}

void WaferViewport::mousePressEvent(QMouseEvent *event)
{
    if (!m_state) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    m_lastMousePos = event->pos();
    m_pressPos = event->pos();

    const bool ctrl = event->modifiers() & Qt::ControlModifier;

    if (event->button() == Qt::LeftButton && (ctrl || m_captureModeActive)) {
        // 框选模式：记录起点，等待拖拽形成选区
        m_interaction = Interaction::Capture;
        m_captureStart = event->pos();
        m_captureRect = QRect(m_captureStart, m_captureStart);
    } else if (event->button() == Qt::RightButton
               || event->button() == Qt::MiddleButton) {
        m_interaction = Interaction::Pan;
        setCursor(Qt::ClosedHandCursor);
    } else if (event->button() == Qt::LeftButton) {
        // 左键按下先按"可能拾取"处理：若期间发生拖拽则转为旋转
        m_interaction = Interaction::Rotate;
        setCursor(Qt::SizeAllCursor);
    }

    event->accept();
}

void WaferViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_state) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    switch (m_interaction) {
    case Interaction::Rotate:
        m_state->orbitBy(static_cast<float>(delta.x()) * 0.45f,
                         static_cast<float>(-delta.y()) * 0.45f);
        requestInteractiveFrame();
        break;

    case Interaction::Pan:
        m_state->panByScreenDelta(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        requestInteractiveFrame();
        break;

    case Interaction::Capture:
        m_captureRect = QRect(m_captureStart, event->pos()).normalized();
        requestInteractiveFrame();
        break;

    case Interaction::None:
    default:
        break;
    }

    event->accept();
}

void WaferViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_state) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    const Interaction finished = m_interaction;
    m_interaction = Interaction::None;
    setCursor(m_captureModeActive ? Qt::CrossCursor : Qt::ArrowCursor);

    // 框选结束：发出区域选择信号，由 UI/业务层执行离屏渲染导出
    if (finished == Interaction::Capture) {
        const QRect rect = m_captureRect.normalized();
        m_captureRect = QRect();
        if (rect.width() >= 8 && rect.height() >= 8) {
            emit captureRegionSelected(rect);
        }
        requestInteractiveFrame();
        event->accept();
        return;
    }

    // 左键点击（无拖拽）：执行射线拾取。
    // 用「按下点与松开点的距离」对照系统拖拽阈值判定点击：
    // 只看 mouseMoveEvent 里清标志，在事件被合并/跳帧时会漏判，
    // 表现为「旋转完松手却弹出未命中提示」。
    if (finished == Interaction::Rotate && event->button() == Qt::LeftButton) {
        const int moved = (event->pos() - m_pressPos).manhattanLength();
        const bool isClick = moved <= QApplication::startDragDistance();
        if (isClick) {
            const float dpr = devicePixelRatioF();
            const int pw = static_cast<int>(width() * dpr);
            const int ph = static_cast<int>(height() * dpr);

            const math::Ray ray = RayPicker::buildRay(
                static_cast<float>(event->position().x()) * dpr,
                static_cast<float>(event->position().y()) * dpr, pw, ph,
                m_state->viewMatrix(), m_state->projectionMatrix(pw, ph));

            // 拾取尺度与渲染共用同一套像素公式（含 DPR 与视口高度），
            // 避免「相机拉远后看得到却点不中」。
            PickParams params;
            params.viewportWidth = static_cast<float>(width()) * dpr;
            params.viewportHeight = static_cast<float>(height()) * dpr;
            params.projScale = m_state->projectionMatrix(pw, ph)(1, 1);
            params.tolerancePx = constants::kPickTolerancePx * dpr;

            // 空间索引的探查范围：取最大可能拾取半径。
            // 半径 ∝ 深度，最近的可见点（相机到场景的近端）给出上界，取大不漏检。
            const float nearDepth = std::max(
                0.05f, m_state->distance() - constants::kWorldExtent * 1.5f);
            const float maxPixel =
                constants::markerPixelSize(constants::kAreaNormalizeRef * constants::kAreaNormalizeRef,
                                           5, params.viewportHeight)
                + params.tolerancePx;
            params.maxPickRadius =
                constants::pixelSizeToWorldRadius(maxPixel, nearDepth, params.projScale,
                                                  params.viewportHeight)
                + constants::pixelSizeToWorldRadius(constants::kMinPickRadiusPx * 2.0f, nearDepth,
                                                    params.projScale, params.viewportHeight);

            // 使用渲染层缓存的过滤后缺陷 + XZ 空间索引：
            // 点数 > 2 万时点击不再线性遍历全部可见点。
            const PickResult result =
                RayPicker::pickWithGrid(ray, m_renderer->worldPositions(),
                                        m_renderer->currentDefects(), params,
                                        m_renderer->pickGrid());

            if (result.hit) {
                m_state->setSelectedDefectId(result.defectId);
                m_renderer->setSelectedIndex(result.index);
                emit defectPicked(result.defectId, result.index);
            } else {
                m_state->setSelectedDefectId(-1);
                m_renderer->setSelectedIndex(-1);
                emit pickMissed();
            }
            update();
        }
    }

    event->accept();
}

void WaferViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_state && event->button() == Qt::LeftButton) {
        // 双击恢复默认视角。
        // 世界坐标已归一化（晶圆半径恒为 1.0），投影半径固定为 1，
        // 故这里不再按缺陷包围盒推导相机距离。
        m_state->resetCamera(1.0f);
        update();
    }
    event->accept();
}

void WaferViewport::wheelEvent(QWheelEvent *event)
{
    if (!m_state) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    const int steps = event->angleDelta().y();
    if (steps == 0) {
        event->accept();
        return;
    }

    // 每档缩放 8%，向上滚动（正值）拉近
    const float factor = steps > 0 ? 0.92f : 1.0f / 0.92f;
    m_state->zoomBy(factor);
    update();

    event->accept();
}

} // namespace wafer
