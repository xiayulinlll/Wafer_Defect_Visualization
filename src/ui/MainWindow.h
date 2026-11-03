#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/DefectTypes.h"
#include "data/DataBufferManager.h"
#include "data/DefectFilter.h"
#include "data/DefectStatistics.h"

#include <QMainWindow>
#include <QSet>
#include <QVector>

#include <memory>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QLabel;
class QListWidget;
class QProgressBar;
class QLineEdit;
class QRect;
class QTimer;

namespace wafer {

class SceneRenderer;
class SceneStateManager;
class WaferViewport;
class DefectInfoDialog;
class PerformancePanel;
class LogOverlayPanel;
class SeverityOverlayPanel;
class DatasetController;
class CaptureController;
class AppSettings;
class StatisticsDialog;

/**
 * @brief 主窗口（UI 交互层入口）。
 *
 * 职责（重构后收敛为「UI 编排」单一职责）：
 *  - 组装四层模块（数据层 / 业务层 / 渲染层）并注入视口；
 *  - 构建右侧控制面板与两个悬浮框，处理用户交互事件；
 *  - 把「后台加载线程」委托给 DatasetController（见 ui/DatasetController.h）；
 *  - 把「框选截图导出」委托给 CaptureController（见 ui/CaptureController.h）。
 *
 * 信息呈现布局：
 *  - 3D 视口占据窗口主体；
 *  - 「缺陷等级分布」以悬浮框停靠视口左上角；
 *  - 「运行日志」以悬浮框停靠视口右上角；
 *  - 数据集 / 筛选 / 渲染控制 / 截图导出 / 性能监控仍固定在右侧面板。
 *
 * 线程模型：
 *  - UI 线程：本类与所有 Widget；
 *  - 数据线程：DatasetLoadWorker（仅 CPU 解析，无 GPU 资源），由 DatasetController 托管；
 *  - 渲染线程：QOpenGLWidget 内部线程，SceneRenderer 全部资源在此。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // 数据集加载
    void onLoadDatasetClicked();
    void onLoadProgress(int percent, const QString &stage);
    void onDatasetReady(int version);
    void onLoadFailed(const QString &errorMessage);
    void onBrowseFileClicked();

    // 筛选
    void onSeverityFilterChanged();
    void onTypeFilterChanged();
    void onLayerFilterChanged();
    void onLayerOffsetToggled(bool on);
    void onResetFilterClicked();
    void onJumpToDefect();
    /** 面积 / 高度区间筛选（§3-4） */
    void onRangeFilterChanged();
    /** 导出当前筛选结果为 CSV（§3-6） */
    void onExportFilteredClicked();
    /** 打开统计视图（§3-7） */
    void onShowStatisticsClicked();

    // 渲染开关
    void onHeatmapToggled(bool on);
    void onMarkersToggled(bool on);
    void onWaferSurfaceToggled(bool on);
    void onZScaleChanged(int value);
    void onResetCameraClicked();
    void onCaptureModeToggled(bool on);
    void onExportScreenshotClicked();

    // 视口交互反馈
    void onDefectPicked(int defectId, int filteredIndex);
    void onPickMissed();
    void onCaptureRegionSelected(const QRect &widgetRect);
    void onRendererReady(const QString &infoText);
    void onRendererError(const QString &message);
    void onCameraChanged();

    // 性能面板刷新
    void onStatsUpdated(const QString &summary);

private:
    void setupUi();
    void setupConnections();
    /** 应用筛选并把结果推送到渲染层；reason 用于日志 */
    void applyFilterAndPushToRenderer(const QString &reason);
    void loadDatasetFile(const QString &filePath);
    void updateStatusCounts();
    /** 用当前数据集刷新「掩膜层」多选列表（含各层缺陷数） */
    void populateLayerList();
    /** 把筛选控件恢复为「显示全部」（内部阻塞信号，避免连续触发筛选） */
    void resetFilterControls();
    /** 从多选列表读取勾选的类型集合 */
    QSet<int> checkedTypes() const;
    /** 从多选列表读取勾选的层号集合 */
    QSet<int> checkedLayers() const;
    /** 定位并高亮指定 ID 的缺陷（必要时自动放宽筛选）；返回是否找到 */
    bool selectDefectById(int defectId);
    /** 在渲染层当前（过滤后）结果里按 ID 查下标；找不到返回 -1 */
    int filteredIndexForId(int defectId) const;
    /** 当前筛选条件的可读描述（日志 / 导出注释复用） */
    QString describeFilter() const;
    /** 从控件与 DefectFilter 读取当前状态并写入配置（§3-9） */
    void persistUiState();
    /** 读取配置并应用到控件与相机（数据集列表在加载后再次对齐） */
    void restoreUiState();

    // ---- 日志（单一路径：qC* → core::Logger → 悬浮日志面板 + 落盘） ----
    // 原先 UI 直接调 m_logPanel->appendXxx()，同一个事件又会经 Logger::messageLogged
    // 再出现一次（日志双写）。现在 UI 只往 Logger 写，面板由订阅统一接收。
    void logInfo(const QString &message);
    void logSuccess(const QString &message);
    void logWarning(const QString &message);
    void logError(const QString &message);

    // ---- 业务层 ----
    std::unique_ptr<SceneStateManager> m_state;
    std::unique_ptr<DefectFilter> m_filter;

    // ---- 配置（相机由视口与主窗口共享同一份，避免互相覆盖） ----
    AppSettings *m_settings = nullptr;
    /** 启动后是否已经把上次保存的筛选条件套用到控件上 */
    bool m_filterRestored = false;
    /** 渲染器是否处于失败状态（用于 onRendererReady 恢复控件可用性） */
    bool m_rendererFailed = false;

    // ---- 数据层 ----
    std::unique_ptr<DataBufferManager> m_bufferManager;

    // ---- 渲染层（由渲染线程持有上下文，本类仅持有对象） ----
    std::unique_ptr<SceneRenderer> m_renderer;

    // ---- 控制器（从本类抽取出的非 UI 职责） ----
    std::unique_ptr<DatasetController> m_datasetController;
    std::unique_ptr<CaptureController> m_captureController;
    QString m_lastLoadedFile;

    // ---- UI ----
    WaferViewport *m_viewport = nullptr;
    DefectInfoDialog *m_infoDialog = nullptr;
    PerformancePanel *m_perfPanel = nullptr;

    // ---- 悬浮框（停靠在 3D 视口容器之上）----
    QWidget *m_viewportHost = nullptr;                 // 视口 + 悬浮框的定位容器
    SeverityOverlayPanel *m_severityOverlay = nullptr; // 左上角：缺陷等级分布
    LogOverlayPanel *m_logPanel = nullptr;             // 右上角：运行日志

    QLineEdit *m_filePathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_loadButton = nullptr;
    QProgressBar *m_progressBar = nullptr;

    QComboBox *m_severityBox = nullptr;
    QComboBox *m_severityMaxBox = nullptr;
    QListWidget *m_typeList = nullptr;
    QListWidget *m_layerList = nullptr;
    QCheckBox *m_layerOffsetCheck = nullptr;
    QLineEdit *m_jumpIdEdit = nullptr;
    QPushButton *m_jumpButton = nullptr;
    QPushButton *m_resetFilterButton = nullptr;
    QLabel *m_filterStatLabel = nullptr;

    // 面积 / 高度区间（-1 表示该端不限制）
    QDoubleSpinBox *m_areaMinSpin = nullptr;
    QDoubleSpinBox *m_areaMaxSpin = nullptr;
    QDoubleSpinBox *m_heightMinSpin = nullptr;
    QDoubleSpinBox *m_heightMaxSpin = nullptr;
    QPushButton *m_exportFilteredButton = nullptr;
    QPushButton *m_statisticsButton = nullptr;

    QCheckBox *m_heatmapCheck = nullptr;
    QCheckBox *m_markerCheck = nullptr;
    QCheckBox *m_waferCheck = nullptr;
    QCheckBox *m_captureModeCheck = nullptr;
    QSlider *m_zScaleSlider = nullptr;
    QLabel *m_zScaleLabel = nullptr;
    QPushButton *m_resetCameraButton = nullptr;
    QPushButton *m_exportButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_cameraLabel = nullptr;

    // 自动截图设置（「框选后自动保存」勾选项）
    QCheckBox *m_autoCaptureCheck = nullptr;

    // 统计视图（懒创建，首次点击「统计视图」时构造）
    StatisticsDialog *m_statsDialog = nullptr;

    // Z 高度滑块节流（§2-2）：拖动时合并事件，避免每个 valueChanged 都重建几何
    QTimer *m_zScaleTimer = nullptr;
    int m_pendingZValue = 10;
};

} // namespace wafer

#endif // MAINWINDOW_H