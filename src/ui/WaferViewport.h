#ifndef WAFERVIEWPORT_H
#define WAFERVIEWPORT_H

#include "core/DefectTypes.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector3D>

class QOpenGLShaderProgram;
class QTimer;

namespace wafer {

class SceneRenderer;
class SceneStateManager;
class DataBufferManager;
class DefectFilter;

/**
 * @brief 主视口组件（UI 层）。
 *
 * QOpenGLWidget 嵌入主窗口，接收鼠标事件并转换为旋转/缩放/平移指令，
 * 转发给业务层 SceneStateManager；渲染委托给渲染层 SceneRenderer。
 *
 * 线程约束：所有 OpenGL 调用（initializeGL/paintGL/resizeGL 及其内部调用）
 * 都在渲染线程执行；UI 层其它槽函数若需要更新渲染资源，只修改 CPU 侧
 * 待处理状态，由渲染线程在下一帧消费。
 */
class WaferViewport : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit WaferViewport(QWidget *parent = nullptr);
    ~WaferViewport() override;

    /** 注入依赖（由 MainWindow 调用，不转移所有权） */
    void attach(SceneRenderer *renderer, SceneStateManager *state,
                DataBufferManager *bufferManager, DefectFilter *filter);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    /** 开启/关闭框选截图模式（Ctrl + 左键拖拽触发） */
    void setCaptureModeActive(bool active);
    bool isCaptureModeActive() const { return m_captureModeActive; }

    /** 请求一次完整重绘（线程安全：只置脏标记） */
    void requestRepaint();

    /**
     * @brief 外部状态变化时调用：标记为「交互帧」，下一帧立即渲染。
     * 相机/筛选/开关变化后调用，避免被空闲降频拖慢响应。
     */
    void requestInteractiveFrame();

    /**
     * @brief 重新初始化渲染器（§4：初始化失败后没有重试入口）。
     *
     * 通过设置新的 QSurfaceFormat 并重建上下文，让 Qt 重新走一次
     * initializeGL()；成功后会再次发出 rendererReady。
     */
    void retryInitialize();

signals:
    /** 拾取到缺陷 */
    void defectPicked(int defectId, int filteredIndex);
    /** 未命中任何缺陷（点击空白处） */
    void pickMissed();
    /** 框选出区域，请求业务/UI 层执行离屏截图导出 */
    void captureRegionSelected(const QRect &widgetRect);
    /** 相机变化（用于触发重绘与状态显示） */
    void cameraChanged();
    /** 渲染器初始化失败 */
    void rendererError(const QString &message);
    /** 渲染器初始化成功，携带信息 */
    void rendererReady(const QString &infoText);

protected:
    // QOpenGLWidget 生命周期（渲染线程）
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // 鼠标 / 滚轮交互
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    enum class Interaction { None, Rotate, Pan, Capture };

    SceneRenderer *m_renderer = nullptr;         // 不持有所有权
    SceneStateManager *m_state = nullptr;        // 不持有所有权
    DataBufferManager *m_bufferManager = nullptr; // 不持有所有权
    DefectFilter *m_filter = nullptr;            // 不持有所有权

    Interaction m_interaction = Interaction::None;
    QPoint m_lastMousePos;
    QPoint m_pressPos;      // 左键按下位置（用于区分单击拾取与拖拽旋转）
    QPoint m_captureStart;
    QRect m_captureRect;
    bool m_captureModeActive = false;
    bool m_rendererInitialized = false;
    QString m_initError;

    // ---- 空闲降频（§2-7）----
    // QOpenGLWidget 在无交互时本身不会持续出帧；但筛选/相机改变会触发 update()，
    // 连续拖动滑块时会一帧一帧重复提交相同画面。这里统一把「短时间内的多次请求」
    // 合并成 ≤30fps，交互结束后立即恢复即时渲染。
    bool m_interactiveFrame = false;  // 本次请求是否来自用户交互（需要立即出帧）
    qint64 m_lastFrameMs = 0;         // 上次实际渲染时间（QElapsedTimer 毫秒）
    QTimer *m_repaintTimer = nullptr; // 等待补帧的定时器（空闲期合并请求）
    QElapsedTimer m_clock;            // 帧间隔计时（非静态，避免多视口互相干扰）
};

} // namespace wafer

#endif // WAFERVIEWPORT_H
