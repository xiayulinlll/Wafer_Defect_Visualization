#ifndef CAPTURECONTROLLER_H
#define CAPTURECONTROLLER_H

#include <QObject>
#include <QRect>
#include <QString>

#include <QComboBox>
#include <QListWidget>

#include <functional>

namespace wafer {

class SceneRenderer;
class SceneStateManager;
class WaferViewport;

/**
 * @brief 截图导出控制器（从 MainWindow 抽取的「框选 / 离屏导出」职责）。
 *
 * 设计目标（对应企业级重构项：#12 拆分上帝类）：
 *  - 把「输出路径推导 + FBO 离屏渲染调用 + 结果文案」从 UI 类中剥离；
 *  - 需要弹保存对话框时由 MainWindow 注入一个回调，避免本类直接依赖具体 UI；
 *  - 返回结构化 CaptureResult，由 MainWindow 决定如何呈现（日志 / 状态栏 / 弹窗）。
 *
 * 线程约束：captureRegionToFile 必须在渲染上下文有效的线程中调用，
 * 因此本类内部会执行 makeCurrent()/doneCurrent() 包裹。
 */
class CaptureController : public QObject
{
    Q_OBJECT

public:
    /**
     * 需要用户选择保存路径时的回调签名。
     * 入参为建议路径，返回用户选择的路径；返回空串表示用户取消。
     */
    using SaveFileNameProvider = std::function<QString(const QString &suggestedPath)>;

    /** 一次截图导出的结果 */
    struct CaptureResult {
        bool ok = false;        ///< 是否成功写出文件
        bool cancelled = false; ///< 用户取消了保存对话框
        QString path;           ///< 输出文件绝对路径（成功时有效）
        QRect region;           ///< 实际导出的区域（widget 像素）
        QString error;          ///< 失败原因（ok == false 且非取消时有效）
    };

    explicit CaptureController(SceneRenderer *renderer, SceneStateManager *state,
                               WaferViewport *viewport, QObject *parent = nullptr);

    /**
     * @brief 导出指定区域为图片。
     *
     * 流程：校验区域 -> 推导输出路径（自动模式或弹框）-> FBO 离屏渲染写文件。
     * 区域小于 8x8 像素会被直接拒绝，避免产生无意义的小图。
     *
     * @param widgetRect 框选区域（widget 像素坐标）
     * @param autoSave   true 时不起保存对话框，直接按自动命名规则落盘
     * @param baseName   自动命名时使用的基础名（通常为数据集文件名）
     * @param saveFileNameProvider 需要用户选择路径时的回调；返回空串表示取消
     * @return 结构化结果，供调用方决定日志与提示
     */
    CaptureResult captureRegion(const QRect &widgetRect, bool autoSave, const QString &baseName,
                                const SaveFileNameProvider &saveFileNameProvider);

    /** 默认截图输出目录（优先用户图片目录，回退到可执行文件旁 screenshots/） */
    static QString defaultOutputDir();

private:
    SceneRenderer *m_renderer = nullptr;  // 不持有所有权
    SceneStateManager *m_state = nullptr; // 不持有所有权
    WaferViewport *m_viewport = nullptr;  // 不持有所有权
};

} // namespace wafer

#endif // CAPTURECONTROLLER_H
