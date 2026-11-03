#include "ui/MainWindow.h"

#include "business/PerformanceStats.h"
#include "business/SceneStateManager.h"
#include "core/Constants.h"
#include "core/Logger.h"
#include "data/CsvDatasetParser.h"
#include "data/CsvDatasetExporter.h"
#include "data/Neighborhood.h"
#include "data/WaferDefect.h"
#include "render/SceneRenderer.h"
#include "ui/AppSettings.h"
#include "ui/CaptureController.h"
#include "ui/DatasetController.h"
#include "ui/DefectInfoDialog.h"
#include "ui/LogOverlayPanel.h"
#include "ui/PerformancePanel.h"
#include "ui/SeverityOverlayPanel.h"
#include "ui/StatisticsDialog.h"
#include "ui/WaferViewport.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace wafer {

namespace {
/** 默认数据集路径（源码树 data 目录，兼容从 build 目录运行） */
QString defaultDatasetPath()
{
    static const char *const candidates[] = {
        "../../data/wafer_defects.csv",
        "../data/wafer_defects.csv",
        "data/wafer_defects.csv",
    };
    for (const char *rel : candidates) {
        const QString p = QDir::current().absoluteFilePath(QString::fromLatin1(rel));
        if (QFileInfo::exists(p))
            return QDir::cleanPath(p);
    }
    return QString();
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 业务层与数据层对象在 UI 线程创建，但不含 OpenGL 资源
    m_state = std::make_unique<SceneStateManager>();
    m_filter = std::make_unique<DefectFilter>();
    m_bufferManager = std::make_unique<DataBufferManager>();
    m_renderer = std::make_unique<SceneRenderer>();
    m_settings = new AppSettings(this);

    setupUi();
    setupConnections();
    // 应用身份此时已确定（main.cpp 在构造 MainWindow 之前设置），显式绑定一次，
    // 保证 QSettings 落到 <AppData>/<组织>/<应用>/<应用>.ini。
    m_settings->bindApplicationIdentity();
    restoreUiState();

    // 注入依赖：渲染器 / 业务状态 / 数据缓冲 / 过滤器（不转移所有权）
    m_viewport->attach(m_renderer.get(), m_state.get(), m_bufferManager.get(), m_filter.get());

    // 渲染器需要数据缓冲才能「尚未筛选时自动生成热力图」。
    // 漏掉这一步会在第一帧里解引用空指针（启动即崩溃），因此显式注入。
    m_renderer->setDataBufferManager(m_bufferManager.get());

    // 控制器：加载线程托管 + 截图导出（在 UI 线程创建，内部线程由其自行管理）
    m_datasetController = std::make_unique<DatasetController>(m_bufferManager.get(), this);
    m_captureController = std::make_unique<CaptureController>(m_renderer.get(), m_state.get(),
                                                              m_viewport, this);

    // 控制器 -> UI 的进度 / 结果回传
    connect(m_datasetController.get(), &DatasetController::loadProgress, this,
            &MainWindow::onLoadProgress);
    connect(m_datasetController.get(), &DatasetController::datasetReady, this,
            &MainWindow::onDatasetReady);
    connect(m_datasetController.get(), &DatasetController::loadFailed, this,
            &MainWindow::onLoadFailed);

    // 记录默认数据集路径，供渲染器就绪后自动加载
    m_lastLoadedFile = m_filePathEdit->text().trimmed();

    // 启动提示：Logger 的消息处理器此刻已安装（main.cpp），直接走 logXxx 即可与面板同源。
    logSuccess(tr("系统启动完成，等待加载晶圆检测数据集。"));
    logInfo(tr("数据集格式：%1").arg(CsvDatasetParser::headerLine()));
}

MainWindow::~MainWindow()
{
    // 0) 先落盘界面状态（相机/筛选/开关）：此时所有控件仍有效
    persistUiState();

    // 关闭顺序很关键：
    // 1) 停止后台加载线程，避免其访问已析构的对象（难点1）；
    //    DatasetController 析构时也会兜底调用 shutdown()。
    if (m_datasetController)
        m_datasetController->shutdown();

    // 2) GPU 资源只由 WaferViewport 析构在上下文有效时释放一次
    //    （见 WaferViewport::~WaferViewport）。这里不再重复调用 shutdown()，
    //    避免两处析构顺序互相依赖（原实现靠 m_initialized 守卫才安全）。
    if (m_bufferManager)
        m_bufferManager->clear();
}

void MainWindow::setupUi()
{
    setWindowTitle(tr("轻量化3D晶圆缺陷可视化与检测数据叠加系统"));
    resize(1500, 940);

    // 主窗口可自由拉伸；给出一个下限，避免侧栏与悬浮框被压到不可用
    setMinimumSize(900, 600);
    // 允许最大化 / 用鼠标拖动任意边框缩放（QMainWindow 默认即支持，此处显式声明意图）
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *central = new QWidget(this);
    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(8);

    auto *splitter = new QSplitter(Qt::Horizontal, central);

    // ---------------- 左侧：3D 视口 + 悬浮信息框 ----------------
    // 用一个无边距容器承载视口，悬浮框以绝对定位贴在其四角，
    // 因此可以覆盖在 3D 画面之上（真正的 HUD 效果）。
    m_viewportHost = new QWidget(splitter);
    auto *hostLayout = new QVBoxLayout(m_viewportHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    m_viewport = new WaferViewport(m_viewportHost);
    hostLayout->addWidget(m_viewport);

    // 左上角：缺陷等级分布悬浮框
    m_severityOverlay = new SeverityOverlayPanel(m_viewportHost);
    m_severityOverlay->attach(m_viewportHost, OverlayPanel::Anchor::TopLeft);

    // 右上角：运行日志悬浮框
    m_logPanel = new LogOverlayPanel(m_viewportHost);
    m_logPanel->attach(m_viewportHost, OverlayPanel::Anchor::TopRight);

    splitter->addWidget(m_viewportHost);

    // ---------------- 右侧：控制面板 ----------------
    auto *sidePanel = new QWidget(splitter);
    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(6, 0, 0, 0);
    sideLayout->setSpacing(8);

    // 数据集加载
    auto *dataGroup = new QGroupBox(tr("检测数据集"), sidePanel);
    auto *dataLayout = new QVBoxLayout(dataGroup);

    auto *fileRow = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(dataGroup);
    m_filePathEdit->setPlaceholderText(tr("CSV 数据集路径"));
    m_filePathEdit->setText(defaultDatasetPath());
    m_browseButton = new QPushButton(tr("浏览..."), dataGroup);
    fileRow->addWidget(m_filePathEdit, 1);
    fileRow->addWidget(m_browseButton);
    dataLayout->addLayout(fileRow);

    m_loadButton = new QPushButton(tr("加载数据集"), dataGroup);
    m_progressBar = new QProgressBar(dataGroup);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    dataLayout->addWidget(m_loadButton);
    dataLayout->addWidget(m_progressBar);

    m_filterStatLabel = new QLabel(tr("尚未加载数据"), dataGroup);
    m_filterStatLabel->setWordWrap(true);
    dataLayout->addWidget(m_filterStatLabel);

    sideLayout->addWidget(dataGroup);

    // 筛选
    auto *filterGroup = new QGroupBox(tr("缺陷筛选"), sidePanel);
    // 两列布局：左列 = 等级下限 / 缺陷类型 / 等级上限 / 掩膜层，
    // 右列 = 按层高度错开 / 缺陷面积 / 缺陷高度 / 按 ID 定位。
    // 两列各自独立成组，行之间互不对齐，列高由各自内容决定。
    auto *filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(10);

    auto *filterLeftLayout = new QFormLayout();
    auto *filterRightLayout = new QFormLayout();
    filterLeftLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    filterRightLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    filterLayout->addLayout(filterLeftLayout, 1);
    filterLayout->addLayout(filterRightLayout, 1);
    // 两列各自等宽：在下方「导出筛选结果 / 统计视图」按钮的宽度约束下，
    // 让两列都能收缩（否则长按钮文本会把面板最小宽度顶到 500px 以上）。
    filterLayout->setStretch(0, 1);
    filterLayout->setStretch(1, 1);

    m_severityBox = new QComboBox(filterGroup);
    m_severityBox->addItem(tr("全部等级 (1-5)"), 1);
    m_severityBox->addItem(tr("≥ 2 - 一般"), 2);
    m_severityBox->addItem(tr("≥ 3 - 中等"), 3);
    m_severityBox->addItem(tr("≥ 4 - 严重"), 4);
    m_severityBox->addItem(tr("仅 5 - 致命"), 5);
    filterLeftLayout->addRow(tr("等级下限："), m_severityBox);

    // 类型多选：可勾选多个类型，全部勾选表示不过滤（列表在构造时按枚举填充）
    m_typeList = new QListWidget(filterGroup);
    m_typeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_typeList->setMaximumHeight(112);
    m_typeList->setToolTip(tr("可多选；全部勾选表示不过滤"));
    for (int i = 0; i <= static_cast<int>(DefectType::Other); ++i) {
        auto *item = new QListWidgetItem(defectTypeName(static_cast<DefectType>(i)), m_typeList);
        item->setData(Qt::UserRole, i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    filterLeftLayout->addRow(tr("缺陷类型："), m_typeList);

    m_severityMaxBox = new QComboBox(filterGroup);
    m_severityMaxBox->addItem(tr("5 - 全部"), 5);
    m_severityMaxBox->addItem(tr("≤ 4"), 4);
    m_severityMaxBox->addItem(tr("≤ 3"), 3);
    m_severityMaxBox->addItem(tr("≤ 2"), 2);
    filterLeftLayout->addRow(tr("等级上限："), m_severityMaxBox);

    m_layerList = new QListWidget(filterGroup);
    m_layerList->setSelectionMode(QAbstractItemView::NoSelection);
    m_layerList->setMaximumHeight(96);
    m_layerList->setToolTip(tr("勾选要显示的掩膜层；列表在数据集加载后自动填充"));
    filterLeftLayout->addRow(tr("掩膜层："), m_layerList);

    m_layerOffsetCheck = new QCheckBox(tr("按层高度错开（叠加视图）"), filterGroup);
    m_layerOffsetCheck->setToolTip(tr("不同掩膜层的缺陷在 Z 方向按层号错开，便于区分层次"));
    filterRightLayout->addRow(m_layerOffsetCheck);

    // 面积 / 高度区间（§3-4）：-1 表示该端不限制，与 DefectFilter 的哨兵值一致
    auto makeRangeSpin = [filterGroup](const QString &suffix, double maxValue) {
        auto *spin = new QDoubleSpinBox(filterGroup);
        spin->setRange(-1.0, maxValue);
        spin->setDecimals(1);
        spin->setSingleStep(maxValue / 50.0);
        spin->setSuffix(suffix);
        spin->setSpecialValueText(tr("不限"));
        spin->setValue(-1.0);
        return spin;
    };
    auto *areaRow = new QHBoxLayout();
    m_areaMinSpin = makeRangeSpin(tr(" um²"), 100000.0);
    m_areaMaxSpin = makeRangeSpin(tr(" um²"), 100000.0);
    areaRow->addWidget(m_areaMinSpin, 1);
    areaRow->addWidget(new QLabel(tr("~"), filterGroup));
    areaRow->addWidget(m_areaMaxSpin, 1);
    filterRightLayout->addRow(tr("缺陷面积："), areaRow);

    auto *heightRow = new QHBoxLayout();
    m_heightMinSpin = makeRangeSpin(tr(" um"), 10000.0);
    m_heightMaxSpin = makeRangeSpin(tr(" um"), 10000.0);
    heightRow->addWidget(m_heightMinSpin, 1);
    heightRow->addWidget(new QLabel(tr("~"), filterGroup));
    heightRow->addWidget(m_heightMaxSpin, 1);
    filterRightLayout->addRow(tr("缺陷高度 Z："), heightRow);

    auto *jumpRow = new QHBoxLayout();
    m_jumpIdEdit = new QLineEdit(filterGroup);
    m_jumpIdEdit->setPlaceholderText(tr("缺陷 ID"));
    m_jumpButton = new QPushButton(tr("定位"), filterGroup);
    jumpRow->addWidget(m_jumpIdEdit, 1);
    jumpRow->addWidget(m_jumpButton);
    filterRightLayout->addRow(tr("按 ID 定位："), jumpRow);

    // 底部整行：重置 + 分析出口
    auto *filterBottomLayout = new QVBoxLayout();
    filterBottomLayout->setContentsMargins(0, 0, 0, 0);
    filterBottomLayout->setSpacing(6);

    m_resetFilterButton = new QPushButton(tr("重置筛选条件"), filterGroup);
    filterBottomLayout->addWidget(m_resetFilterButton);

    // 分析出口：导出当前筛选结果 CSV + 统计视图（§3-6 / §3-7）
    auto *analysisRow = new QHBoxLayout();
    m_exportFilteredButton = new QPushButton(tr("导出筛选结果"), filterGroup);
    m_exportFilteredButton->setToolTip(tr("把当前筛选结果导出为 CSV（含统计注释，可被本程序重新加载）"));
    m_statisticsButton = new QPushButton(tr("统计视图"), filterGroup);
    m_statisticsButton->setToolTip(tr("类型×等级交叉表、半径分区分布与良率估算"));
    analysisRow->addWidget(m_exportFilteredButton, 1);
    analysisRow->addWidget(m_statisticsButton, 1);
    filterBottomLayout->addLayout(analysisRow);

    auto *filterGroupLayout = new QVBoxLayout(filterGroup);
    filterGroupLayout->setSpacing(8);
    filterGroupLayout->addLayout(filterLayout);
    filterGroupLayout->addLayout(filterBottomLayout);

    sideLayout->addWidget(filterGroup);

    // 渲染控制
    auto *renderGroup = new QGroupBox(tr("渲染控制"), sidePanel);
    auto *renderLayout = new QVBoxLayout(renderGroup);

    m_heatmapCheck = new QCheckBox(tr("缺陷热力云图 (GPU 伪彩色)"), renderGroup);
    m_heatmapCheck->setChecked(true);
    m_markerCheck = new QCheckBox(tr("缺陷标记点"), renderGroup);
    m_markerCheck->setChecked(true);
    m_waferCheck = new QCheckBox(tr("晶圆本体"), renderGroup);
    m_waferCheck->setChecked(true);

    renderLayout->addWidget(m_heatmapCheck);
    renderLayout->addWidget(m_markerCheck);
    renderLayout->addWidget(m_waferCheck);

    m_zScaleLabel = new QLabel(tr("高度放缩：1.0x"), renderGroup);
    m_zScaleSlider = new QSlider(Qt::Horizontal, renderGroup);
    m_zScaleSlider->setRange(0, 100);
    m_zScaleSlider->setValue(10);
    renderLayout->addWidget(m_zScaleLabel);
    renderLayout->addWidget(m_zScaleSlider);

    m_resetCameraButton = new QPushButton(tr("恢复默认视角"), renderGroup);
    renderLayout->addWidget(m_resetCameraButton);

    sideLayout->addWidget(renderGroup);

    // 截图导出
    auto *captureGroup = new QGroupBox(tr("报告截图导出 (FBO 离屏渲染)"), sidePanel);
    auto *captureLayout = new QVBoxLayout(captureGroup);

    m_captureModeCheck = new QCheckBox(tr("开启框选模式 (或 Ctrl+左键拖拽)"), captureGroup);
    m_autoCaptureCheck = new QCheckBox(tr("框选后自动保存，不弹保存框"), captureGroup);
    m_exportButton = new QPushButton(tr("导出当前视口截图"), captureGroup);

    captureLayout->addWidget(m_captureModeCheck);
    captureLayout->addWidget(m_autoCaptureCheck);
    captureLayout->addWidget(m_exportButton);

    sideLayout->addWidget(captureGroup);

    // 性能面板
    m_perfPanel = new PerformancePanel(sidePanel);
    sideLayout->addWidget(m_perfPanel);

    // 说明：「缺陷等级分布」与「运行日志」已改为悬浮框，
    // 分别停靠在 3D 视口的左上角与右上角（见 setupUi 前半部分）。

    sideLayout->addStretch(1);

    splitter->addWidget(sidePanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1080, 420});

    rootLayout->addWidget(splitter);
    setCentralWidget(central);

    // 状态栏
    m_statusLabel = new QLabel(tr("就绪"), this);
    m_cameraLabel = new QLabel(tr("相机：默认视角"), this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_cameraLabel);

    // 「缺陷等级分布」「运行日志」两个悬浮框的初始尺寸由各自的内容自然尺寸决定
    // （OverlayPanel::attach() 内完成），保证默认状态下内容完整显示；
    // 之后用户可拖拽边缘缩放，或由已保存的布局恢复尺寸（见 restoreUiState()）。

    // 详情弹窗（非模态，便于与主窗口对照查看）
    m_infoDialog = new DefectInfoDialog(this);
}

void MainWindow::setupConnections()
{
    // ---- 日志同源：core::Logger（控制台 + 落盘） -> 界面日志面板 ----
    // 跨线程安全：Logger 可能从渲染/加载线程发出信号，队列连接自动转投 UI 线程。
    connect(&Logger::instance(), &Logger::messageLogged, m_logPanel,
            &LogOverlayPanel::appendFromLogger, Qt::QueuedConnection);

    // ---- 数据集加载 ----
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseFileClicked);
    connect(m_loadButton, &QPushButton::clicked, this, &MainWindow::onLoadDatasetClicked);

    // ---- 筛选 ----
    connect(m_severityBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onSeverityFilterChanged);
    connect(m_severityMaxBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onSeverityFilterChanged);
    connect(m_typeList, &QListWidget::itemChanged, this, &MainWindow::onTypeFilterChanged);
    connect(m_layerList, &QListWidget::itemChanged, this, &MainWindow::onLayerFilterChanged);
    connect(m_layerOffsetCheck, &QCheckBox::toggled, this, &MainWindow::onLayerOffsetToggled);
    connect(m_jumpButton, &QPushButton::clicked, this, &MainWindow::onJumpToDefect);
    connect(m_jumpIdEdit, &QLineEdit::returnPressed, this, &MainWindow::onJumpToDefect);
    connect(m_resetFilterButton, &QPushButton::clicked, this, &MainWindow::onResetFilterClicked);
    connect(m_exportFilteredButton, &QPushButton::clicked, this,
            &MainWindow::onExportFilteredClicked);
    connect(m_statisticsButton, &QPushButton::clicked, this,
            &MainWindow::onShowStatisticsClicked);

    // 面积 / 高度区间：用 editingFinished 而不是 valueChanged，
    // 避免用户输入「1」→「10」的中间态各触发一次全量重建。
    const auto rangeSpins = {m_areaMinSpin, m_areaMaxSpin, m_heightMinSpin, m_heightMaxSpin};
    for (QDoubleSpinBox *spin : rangeSpins) {
        connect(spin, &QDoubleSpinBox::editingFinished, this,
                &MainWindow::onRangeFilterChanged);
    }

    // ---- 渲染开关 ----
    connect(m_heatmapCheck, &QCheckBox::toggled, this, &MainWindow::onHeatmapToggled);
    connect(m_markerCheck, &QCheckBox::toggled, this, &MainWindow::onMarkersToggled);
    connect(m_waferCheck, &QCheckBox::toggled, this, &MainWindow::onWaferSurfaceToggled);
    connect(m_zScaleSlider, &QSlider::valueChanged, this, &MainWindow::onZScaleChanged);

    // Z 缩放节流：拖动停止 30ms 后才真正重建几何（合并连续事件）
    m_zScaleTimer = new QTimer(this);
    m_zScaleTimer->setSingleShot(true);
    m_zScaleTimer->setInterval(30);
    connect(m_zScaleTimer, &QTimer::timeout, this, [this]() {
        const float scale = 0.1f + static_cast<float>(m_pendingZValue) * 0.099f;
        m_state->setZScale(scale);
    });
    connect(m_resetCameraButton, &QPushButton::clicked, this, &MainWindow::onResetCameraClicked);
    connect(m_captureModeCheck, &QCheckBox::toggled, this, &MainWindow::onCaptureModeToggled);
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::onExportScreenshotClicked);

    // ---- 视口交互 ----
    connect(m_viewport, &WaferViewport::defectPicked, this, &MainWindow::onDefectPicked);
    connect(m_viewport, &WaferViewport::pickMissed, this, &MainWindow::onPickMissed);
    connect(m_viewport, &WaferViewport::captureRegionSelected, this,
            &MainWindow::onCaptureRegionSelected);
    connect(m_viewport, &WaferViewport::rendererReady, this, &MainWindow::onRendererReady);
    connect(m_viewport, &WaferViewport::rendererError, this, &MainWindow::onRendererError);
    connect(m_viewport, &WaferViewport::cameraChanged, this, &MainWindow::onCameraChanged);

    // ---- 详情弹窗定位请求 ----
    connect(m_infoDialog, &DefectInfoDialog::focusRequested, this, [this](int defectId) {
        DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
        if (!snapshot || snapshot->isEmpty())
            return;
        // 在渲染层当前持有的过滤结果中查找该缺陷并聚焦
        const auto &current = m_renderer->currentDefects();
        for (int i = 0; i < current.size(); ++i) {
            if (current.at(i).id == defectId) {
                m_state->setSelectedDefectId(defectId);
                m_renderer->setSelectedIndex(i);
                m_viewport->requestInteractiveFrame();
                logInfo(tr("已定位到缺陷 #%1（视口内高亮）。").arg(defectId));
                return;
            }
        }
        logWarning(tr("缺陷 #%1 当前不在筛选结果中，无法定位。").arg(defectId));
    });

    connect(m_state.get(), &SceneStateManager::heatmapEnabledChanged, this, [this](bool on) {
        m_renderer->setHeatmapEnabled(on);
        m_viewport->update();
    });
    connect(m_state.get(), &SceneStateManager::markersEnabledChanged, this, [this](bool on) {
        m_renderer->setMarkersEnabled(on);
        m_viewport->update();
    });
    connect(m_state.get(), &SceneStateManager::waferSurfaceVisibleChanged, this, [this](bool on) {
        m_renderer->setWaferSurfaceVisible(on);
        m_viewport->update();
    });
    connect(m_state.get(), &SceneStateManager::zScaleChanged, this, [this](float scale) {
        m_renderer->setZScale(scale);
        m_viewport->update();
    });

    connect(m_renderer->performanceStats(), &PerformanceStats::statsUpdated, this,
            &MainWindow::onStatsUpdated);
}

void MainWindow::onBrowseFileClicked()
{
    const QString startDir = m_filePathEdit->text().isEmpty()
                                 ? QDir::currentPath()
                                 : QFileInfo(m_filePathEdit->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择晶圆缺陷数据集"), startDir,
        tr("CSV 数据集 (*.csv);;所有文件 (*.*)"));

    if (!path.isEmpty()) {
        m_filePathEdit->setText(path);
        logInfo(tr("已选择数据集：%1").arg(path));
    }
}

void MainWindow::onLoadDatasetClicked()
{
    const QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("未指定数据集"), tr("请先选择要加载的 CSV 数据集文件。"));
        return;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("文件不存在"), tr("找不到文件：\n%1").arg(path));
        return;
    }
    loadDatasetFile(path);
}

void MainWindow::loadDatasetFile(const QString &filePath)
{
    m_lastLoadedFile = filePath;
    m_loadButton->setEnabled(false);
    m_progressBar->setValue(0);
    m_filterStatLabel->setText(tr("正在加载：%1").arg(QFileInfo(filePath).fileName()));
    m_statusLabel->setText(tr("正在解析数据集..."));
    logInfo(tr("开始加载数据集：%1").arg(filePath));

    // 后台线程：仅做 CPU 解析与校验，不触碰任何 GPU 资源（难点1）。
    // 线程创建/取消/回收全部由 DatasetController 负责，UI 层只发指令。
    if (m_renderer)
        m_renderer->setSyntheticHeatmapEnabled(true);
    m_datasetController->load(filePath);
}

void MainWindow::onLoadProgress(int percent, const QString &stage)
{
    m_progressBar->setValue(percent);
    m_progressBar->setFormat(tr("%1% - %2").arg(percent).arg(stage));
}

void MainWindow::onDatasetReady(int version)
{
    m_progressBar->setValue(100);
    m_loadButton->setEnabled(true);

    // worker 已在后台线程通过 DataBufferManager::publish() 完成双缓冲发布，
    // 信号只带版本号；这里读取快照做 UI 侧统计，不再跨线程传整份数据集。
    const DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot) {
        onLoadFailed(tr("数据缓冲为空"));
        return;
    }
    const DefectDataset &dataset = *snapshot;
    const int count = dataset.points.size();

    // 说明：worker 已经在后台线程通过 DataBufferManager::publish() 完成双缓冲发布，
    // 这里只做 UI 侧统计与筛选条件对齐，不再重复写入缓冲区。
    //
    // 筛选条件取舍：启动后第一次成功加载时，套用上次保存的筛选条件；
    // 之后用户重新加载数据则保留当前选择（避免“点一下就丢筛选”）。
    m_filter->reset();
    resetFilterControls();
    populateLayerList();
    if (!m_filterRestored) {
        restoreUiState();
        m_filterRestored = true;
    }

    logSuccess(tr("数据集加载完成：%1 个缺陷点，数据版本 v%2。").arg(count).arg(version));
    logInfo(
        tr("晶圆半径：%1 mm，来源：%2").arg(dataset.waferRadius, 0, 'f', 1).arg(dataset.sourceFile));
    if (dataset.skippedLines > 0) {
        logWarning(tr("解析时跳过 %1 行非法数据。").arg(dataset.skippedLines));
    }

    if (!dataset.columns.isEmpty())
        logInfo(tr("识别到列：%1").arg(dataset.columns.join(QStringLiteral(", "))));
    if (!dataset.ignoredColumns.isEmpty()) {
        logWarning(tr("以下列未参与渲染（已忽略）：%1")
                       .arg(dataset.ignoredColumns.join(QStringLiteral(", "))));
    }
    if (dataset.shortLines > 0 || dataset.invalidValueLines > 0) {
        logInfo(tr("跳过原因统计：列不足 %1 行 / 数值非法 %2 行")
                    .arg(dataset.shortLines)
                    .arg(dataset.invalidValueLines));
    }

    applyFilterAndPushToRenderer(tr("加载后初次渲染"));
    updateStatusCounts();

    m_statusLabel->setText(tr("已加载 %1 个缺陷点").arg(count));
    onResetCameraClicked();
}

void MainWindow::onLoadFailed(const QString &errorMessage)
{
    m_loadButton->setEnabled(true);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(tr("加载失败"));
    m_filterStatLabel->setText(tr("加载失败"));
    m_statusLabel->setText(tr("加载失败"));
    logError(tr("数据集加载失败：%1").arg(errorMessage));

    QMessageBox::critical(this, tr("加载失败"), tr("数据集解析失败：\n%1").arg(errorMessage));
}

void MainWindow::applyFilterAndPushToRenderer(const QString &reason)
{
    DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot || snapshot->isEmpty()) {
        m_renderer->setFilteredDefects({});
        m_viewport->update();
        return;
    }

    const DefectDataset &dataset = *snapshot;

    // 使用数据层 DefectFilter 的真实接口（minSeverity / maxSeverity / types / layers）
    QVector<DefectPoint> filtered;
    m_filter->apply(dataset, &filtered);

    // 不需要在 UI 侧再缓存一份（旧的 m_lastFiltered 没有任何读者，10 万点时会白白多占内存）
    const int filteredCount = filtered.size();

    // 渲染层：一次性上传 VBO 并重建热力图纹理；setDefectData 内部会复用入参的存储
    m_renderer->setDefectData(filtered, dataset.waferRadius, m_state->zScale());
    m_renderer->setSyntheticHeatmapEnabled(false);
    m_viewport->update();

    const QSet<int> &types = m_filter->types();
    const int layerCount = m_filter->layers().size();
    logInfo(tr("[%1] 命中 %2 / %3 个缺陷点 (等级 %4~%5，类型 %6，层 %7)。")
                .arg(reason)
                .arg(filteredCount)
                .arg(dataset.points.size())
                .arg(static_cast<int>(m_filter->minSeverity()))
                .arg(static_cast<int>(m_filter->maxSeverity()))
                .arg(types.isEmpty() ? tr("全部") : tr("%1 种").arg(types.size()))
                .arg(layerCount == 0 ? tr("全部") : tr("%1 层").arg(layerCount)));

    updateStatusCounts();
}

void MainWindow::onSeverityFilterChanged()
{
    const int minLevel = m_severityBox->currentData().toInt();
    const int maxLevel = m_severityMaxBox->currentData().toInt();

    // 上下限互斥：上限低于下限时自动抬升上限，避免出现“空结果”这种反直觉状态
    if (maxLevel < minLevel) {
        QSignalBlocker blocker(m_severityMaxBox);
        for (int i = 0; i < m_severityMaxBox->count(); ++i) {
            if (m_severityMaxBox->itemData(i).toInt() >= minLevel) {
                m_severityMaxBox->setCurrentIndex(i);
                break;
            }
        }
    }

    m_filter->setMinSeverity(static_cast<DefectSeverity>(m_severityBox->currentData().toInt()));
    m_filter->setMaxSeverity(static_cast<DefectSeverity>(m_severityMaxBox->currentData().toInt()));

    applyFilterAndPushToRenderer(tr("按等级筛选"));

    // 筛选变化后清空选中态，避免索引在过滤结果里错位
    m_state->setSelectedDefectId(-1);
    m_renderer->setSelectedIndex(-1);
}

void MainWindow::onTypeFilterChanged()
{
    m_filter->setTypes(checkedTypes());
    applyFilterAndPushToRenderer(tr("按类型筛选"));

    m_state->setSelectedDefectId(-1);
    m_renderer->setSelectedIndex(-1);
}

void MainWindow::onLayerFilterChanged()
{
    m_filter->setLayers(checkedLayers());
    applyFilterAndPushToRenderer(tr("按掩膜层筛选"));

    m_state->setSelectedDefectId(-1);
    m_renderer->setSelectedIndex(-1);
}

void MainWindow::onRangeFilterChanged()
{
    // 编辑输入一致：-1 表示该端不限制（与 DefectFilter 哨兵一致）
    float areaMin = static_cast<float>(m_areaMinSpin->value());
    float areaMax = static_cast<float>(m_areaMaxSpin->value());
    float heightMin = static_cast<float>(m_heightMinSpin->value());
    float heightMax = static_cast<float>(m_heightMaxSpin->value());

    // 上限 < 下限时自动抬升上限（与等级区间的手感保持一致）
    if (areaMax >= 0.0f && areaMin >= 0.0f && areaMax < areaMin) {
        const QSignalBlocker blocker(m_areaMaxSpin);
        m_areaMaxSpin->setValue(m_areaMinSpin->value());
        areaMax = areaMin;
    }
    if (heightMax >= 0.0f && heightMin >= 0.0f && heightMax < heightMin) {
        const QSignalBlocker blocker(m_heightMaxSpin);
        m_heightMaxSpin->setValue(m_heightMinSpin->value());
        heightMax = heightMin;
    }

    // 约束：超大的下限（例如输入 1e9）会让结果为空，这里按信号量级做一次兜底
    if (areaMin > 100000.0f) {
        const QSignalBlocker blocker(m_areaMinSpin);
        m_areaMinSpin->setValue(-1.0);
        areaMin = -1.0f;
    }
    if (heightMin > 10000.0f) {
        const QSignalBlocker blocker(m_heightMinSpin);
        m_heightMinSpin->setValue(-1.0);
        heightMin = -1.0f;
    }

    m_filter->setAreaRange(areaMin, areaMax);
    m_filter->setHeightRange(heightMin, heightMax);
    applyFilterAndPushToRenderer(tr("按面积/高度筛选"));

    m_state->setSelectedDefectId(-1);
    m_renderer->setSelectedIndex(-1);
}

void MainWindow::onLayerOffsetToggled(bool on)
{
    m_filter->setLayerOffsetEnabled(on);
    applyFilterAndPushToRenderer(on ? tr("开启层错开") : tr("关闭层错开"));
}

void MainWindow::resetFilterControls()
{
    // 统一在此处阻塞信号：重置过程中若逐个触发槽函数，
    // 会连续重建热力图（10 万点时明显卡顿）。
    QSignalBlocker sevMinBlocker(m_severityBox);
    QSignalBlocker sevMaxBlocker(m_severityMaxBox);
    QSignalBlocker typeBlocker(m_typeList);
    QSignalBlocker layerBlocker(m_layerList);
    QSignalBlocker offsetBlocker(m_layerOffsetCheck);
    const QSignalBlocker areaMinBlocker(m_areaMinSpin);
    const QSignalBlocker areaMaxBlocker(m_areaMaxSpin);
    const QSignalBlocker heightMinBlocker(m_heightMinSpin);
    const QSignalBlocker heightMaxBlocker(m_heightMaxSpin);

    m_severityBox->setCurrentIndex(0);    // 全部等级（下限 1）
    m_severityMaxBox->setCurrentIndex(0); // 上限 5
    m_layerOffsetCheck->setChecked(false);
    m_areaMinSpin->setValue(-1.0);   // 不限
    m_areaMaxSpin->setValue(-1.0);   // 不限
    m_heightMinSpin->setValue(-1.0); // 不限
    m_heightMaxSpin->setValue(-1.0); // 不限

    // 空集合 = 全部，因此控件上表现为“全部勾选”
    for (int i = 0; i < m_typeList->count(); ++i)
        m_typeList->item(i)->setCheckState(Qt::Checked);
    for (int i = 0; i < m_layerList->count(); ++i)
        m_layerList->item(i)->setCheckState(Qt::Checked);
}

QSet<int> MainWindow::checkedTypes() const
{
    QSet<int> types;
    for (int i = 0; i < m_typeList->count(); ++i) {
        const QListWidgetItem *item = m_typeList->item(i);
        if (item->checkState() == Qt::Checked)
            types.insert(item->data(Qt::UserRole).toInt());
    }
    return types;
}

QSet<int> MainWindow::checkedLayers() const
{
    QSet<int> layers;
    for (int i = 0; i < m_layerList->count(); ++i) {
        const QListWidgetItem *item = m_layerList->item(i);
        if (item->checkState() == Qt::Checked)
            layers.insert(item->data(Qt::UserRole).toInt());
    }
    return layers;
}

void MainWindow::populateLayerList()
{
    // 层号从数据集快照中汇总，只展示真实存在的层，避免列表里出现空层。
    // 10 万点级别时 QSet 会为每个层建一个节点，这里用扁平数组 + 排序更省。
    QVector<int> sortedLayers;
    QVector<int> layerCounts;

    if (const DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot()) {
        QVector<int> allLayers;
        allLayers.reserve(snapshot->points.size());
        for (const DefectPoint &point : snapshot->points)
            allLayers.push_back(point.layer);

        std::sort(allLayers.begin(), allLayers.end());
        // 压缩成「层号 + 该层缺陷数」，同时让列表按层号升序
        for (int i = 0; i < allLayers.size();) {
            const int layer = allLayers.at(i);
            int n = 0;
            while (i < allLayers.size() && allLayers.at(i) == layer) {
                ++i;
                ++n;
            }
            sortedLayers.push_back(layer);
            layerCounts.push_back(n);
        }
    }

    QSignalBlocker blocker(m_layerList);
    m_layerList->clear();
    for (int i = 0; i < sortedLayers.size(); ++i) {
        const int layer = sortedLayers.at(i);
        auto *item = new QListWidgetItem(tr("层 %1 (%2)").arg(layer).arg(layerCounts.at(i)),
                                         m_layerList);
        item->setData(Qt::UserRole, layer);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    m_layerList->setEnabled(!sortedLayers.isEmpty());
    m_layerList->setToolTip(
        sortedLayers.isEmpty()
            ? tr("当前数据集没有有效的层信息")
            : tr("共 %1 个掩膜层，全部勾选表示不过滤").arg(sortedLayers.size()));

    // 全部勾选等价于不按层过滤：DefectFilter 用空集合表示“全部”。
    // 注意：只有「真的全选」才清空集合；若用户在其它数据集上只勾了部分层，
    // 重新加载后这些勾选（restoreUiState）不能被这里的清空覆盖。
    QSet<int> checked;
    for (int i = 0; i < m_layerList->count(); ++i) {
        // 新建列表项默认全选，这里先读当前勾选状态
        if (m_layerList->item(i)->checkState() == Qt::Checked)
            checked.insert(m_layerList->item(i)->data(Qt::UserRole).toInt());
    }
    m_filter->setLayers(checked.size() == sortedLayers.size() ? QSet<int>{} : checked);
}

void MainWindow::onJumpToDefect()
{
    bool ok = false;
    const int id = m_jumpIdEdit->text().trimmed().toInt(&ok);
    if (!ok) {
        logWarning(tr("请输入有效的缺陷 ID（整数）。"));
        return;
    }
    selectDefectById(id);
}

bool MainWindow::selectDefectById(int id)
{
    const DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot || snapshot->isEmpty()) {
        logWarning(tr("尚未加载数据集。"));
        return false;
    }
    if (m_renderer && m_renderer->currentDefectCount() == 0) {
        applyFilterAndPushToRenderer(tr("ID 定位前同步筛选结果"));
    }

    const QVector<DefectPoint> &points = snapshot->points;
    const DefectPoint *hit = nullptr;
    for (const DefectPoint &p : points) {
        if (p.id == id) {
            hit = &p;
            break;
        }
    }
    if (!hit) {
        logWarning(tr("未找到 ID 为 %1 的缺陷点。").arg(id));
        return false;
    }

    const DefectPoint point = *hit;

    // ID 命中的缺陷若被当前筛选隐藏，自动放宽筛选，
    // 否则“定位成功但屏幕上看不到”同样反直觉。
    if (!m_filter->accepts(point) || filteredIndexForId(id) < 0) {
        m_filter->reset();
        resetFilterControls();
        logInfo(tr("ID %1 被当前筛选隐藏，已自动重置筛选条件。").arg(id));
        applyFilterAndPushToRenderer(tr("ID 定位放宽筛选"));
    }

    // 关键：渲染层的选中下标是「过滤后数组」的下标，
    // 不是原始数据集下标（旧实现直接用了数据集下标，筛选非空时会高亮错点）。
    const int filteredIndex = filteredIndexForId(id);
    if (filteredIndex < 0) {
        logWarning(tr("缺陷 #%1 无法进入当前渲染结果，高亮已跳过。").arg(id));
        return false;
    }

    m_state->setSelectedDefectId(id);
    m_renderer->setSelectedIndex(filteredIndex);
    m_viewport->requestInteractiveFrame();

    logSuccess(tr("已定位缺陷 ID %1：类型 %2，等级 %3，坐标 (%4, %5)，层 %6")
                   .arg(id)
                   .arg(defectTypeName(point.type))
                   .arg(static_cast<int>(point.severity))
                   .arg(point.x, 0, 'f', 2)
                   .arg(point.y, 0, 'f', 2)
                   .arg(point.layer));
    return true;
}

int MainWindow::filteredIndexForId(int defectId) const
{
    if (!m_renderer)
        return -1;
    const QVector<DefectPoint> &current = m_renderer->currentDefects();
    for (int i = 0; i < current.size(); ++i) {
        if (current.at(i).id == defectId)
            return i;
    }
    return -1;
}

QString MainWindow::describeFilter() const
{
    QStringList parts;
    parts << tr("等级 %1~%2").arg(static_cast<int>(m_filter->minSeverity()))
                 .arg(static_cast<int>(m_filter->maxSeverity()));

    const QSet<int> &types = m_filter->types();
    if (types.isEmpty()) {
        parts << tr("类型 全部");
    } else {
        QStringList names;
        for (int t : types)
            names << defectTypeName(static_cast<DefectType>(t));
        std::sort(names.begin(), names.end());
        parts << tr("类型 %1").arg(names.join(QLatin1Char('/')));
    }

    const QSet<int> &layers = m_filter->layers();
    if (!layers.isEmpty()) {
        QStringList numbers;
        for (int l : layers)
            numbers << QString::number(l);
        std::sort(numbers.begin(), numbers.end());
        parts << tr("层 %1").arg(numbers.join(QLatin1Char('/')));
    }
    if (m_filter->minArea() >= 0.0f || m_filter->maxArea() >= 0.0f) {
        parts << tr("面积 %1~%2 um²")
                     .arg(m_filter->minArea() >= 0.0f ? QString::number(m_filter->minArea(), 'f', 1)
                                                      : QStringLiteral("∞"))
                     .arg(m_filter->maxArea() >= 0.0f ? QString::number(m_filter->maxArea(), 'f', 1)
                                                      : QStringLiteral("∞"));
    }
    if (m_filter->minHeight() >= 0.0f || m_filter->maxHeight() >= 0.0f) {
        parts << tr("高度 %1~%2 um")
                     .arg(m_filter->minHeight() >= 0.0f ? QString::number(m_filter->minHeight(), 'f', 1)
                                                        : QStringLiteral("-∞"))
                     .arg(m_filter->maxHeight() >= 0.0f ? QString::number(m_filter->maxHeight(), 'f', 1)
                                                        : QStringLiteral("∞"));
    }
    if (m_filter->layerOffsetEnabled())
        parts << tr("层高度错开");

    return parts.join(QStringLiteral("；"));
}

void MainWindow::onExportFilteredClicked()
{
    const DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot || snapshot->isEmpty()) {
        QMessageBox::information(this, tr("暂无数据"), tr("请先加载数据集再导出筛选结果。"));
        return;
    }

    // 导出的是「渲染层当前持有的过滤结果」，与屏幕上看到的完全一致
    const QVector<DefectPoint> &points = m_renderer->currentDefects();
    if (points.isEmpty()) {
        QMessageBox::information(this, tr("结果为空"), tr("当前筛选条件下没有缺陷点可导出。"));
        return;
    }

    const QString base = m_lastLoadedFile.isEmpty()
                             ? QStringLiteral("wafer")
                             : QFileInfo(m_lastLoadedFile).completeBaseName();
    const QString suggested = QDir(QDir::currentPath())
                                  .absoluteFilePath(QStringLiteral("%1_filtered_%2.csv")
                                                        .arg(base,
                                                             QDateTime::currentDateTime().toString(
                                                                 QStringLiteral("yyyyMMdd_HHmmss"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("导出筛选结果"), suggested,
                                                      tr("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;

    // 统计摘要写进注释行：既能被 Excel 直接打开，也不会干扰重新加载
    const DefectStatistics stats = DefectAnalyzer::compute(points, snapshot->waferRadius);
    CsvDatasetExporter::Summary summary;
    summary.datasetName = snapshot->sourceFile;
    summary.totalPoints = snapshot->points.size();
    summary.exportedPoints = points.size();
    summary.filterDescription = describeFilter();
    summary.extra = DefectAnalyzer::summaryText(stats);

    QString error;
    if (!CsvDatasetExporter::exportToFile(path, points, summary, &error)) {
        logError(tr("导出筛选结果失败：%1").arg(error));
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入 CSV：\n%1").arg(error));
        return;
    }

    logSuccess(tr("已导出筛选结果：%1（%2 个缺陷点）").arg(path).arg(points.size()));
    m_statusLabel->setText(tr("已导出 %1 个缺陷点").arg(points.size()));
}

void MainWindow::onShowStatisticsClicked()
{
    const DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot || snapshot->isEmpty()) {
        QMessageBox::information(this, tr("暂无数据"), tr("请先加载数据集。"));
        return;
    }

    if (!m_statsDialog)
        m_statsDialog = new StatisticsDialog(this);

    // 统计口径：当前筛选后的可见缺陷（与渲染内容一致）
    const QVector<DefectPoint> &points = m_renderer->currentDefects();
    const DefectStatistics stats = DefectAnalyzer::compute(points, snapshot->waferRadius);
    m_statsDialog->setStatistics(stats);

    logInfo(tr("统计视图已更新：%1 个可见缺陷，估计良率 %2%")
                .arg(stats.total)
                .arg(stats.estimatedYield, 0, 'f', 1));
}

void MainWindow::onResetFilterClicked()
{
    m_filter->reset();
    resetFilterControls();

    applyFilterAndPushToRenderer(tr("重置筛选"));

    m_state->setSelectedDefectId(-1);
    m_renderer->setSelectedIndex(-1);
    logInfo(tr("筛选条件已重置为显示全部缺陷。"));
}

void MainWindow::updateStatusCounts()
{
    DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
    if (!snapshot || snapshot->isEmpty()) {
        m_filterStatLabel->setText(tr("尚未加载数据"));
        int empty[6] = {0, 0, 0, 0, 0, 0};
        m_severityOverlay->setCounts(empty);
        m_severityOverlay->setTotals(0, 0);
        return;
    }

    m_filterStatLabel->setText(tr("当前显示 %1 / %2 个缺陷点\n(筛选条件与渲染内容保持同步)")
                                   .arg(m_renderer->currentDefectCount())
                                   .arg(snapshot->points.size()));

    // 按等级统计当前可见缺陷（与渲染层持有的过滤结果一致）
    int counts[6] = {0, 0, 0, 0, 0, 0};
    for (const DefectPoint &point : m_renderer->currentDefects()) {
        const int level = static_cast<int>(point.severity);
        if (level >= 1 && level <= 5)
            counts[level]++;
    }

    m_severityOverlay->setCounts(counts);
    m_severityOverlay->setTotals(m_renderer->currentDefectCount(), snapshot->points.size());
}

void MainWindow::onHeatmapToggled(bool on)
{
    m_state->setHeatmapEnabled(on);
    logInfo(on ? tr("已开启缺陷热力云图。") : tr("已关闭缺陷热力云图。"));
}

void MainWindow::onMarkersToggled(bool on)
{
    m_state->setMarkersEnabled(on);
}

void MainWindow::onWaferSurfaceToggled(bool on)
{
    m_state->setWaferSurfaceVisible(on);
}

void MainWindow::onZScaleChanged(int value)
{
    // 滑块 0..100 映射为 0.1x .. 10.0x 高度放缩
    const float scale = 0.1f + static_cast<float>(value) * 0.099f;
    m_zScaleLabel->setText(tr("高度放缩：%1x").arg(scale, 0, 'f', 2));

    // 拖动时合并事件（§2-2）：每个 valueChanged 都会重建标记点几何并重传 VBO，
    // 快速拖动会连续触发几十次。用单次定时器在 30ms 后只应用最后一个值。
    m_pendingZValue = value;
    if (m_zScaleTimer)
        m_zScaleTimer->start();
    else
        m_state->setZScale(scale); // 定时器未就绪（理论上不会发生）时退化为立即应用
}

void MainWindow::onResetCameraClicked()
{
    // 世界坐标已归一化（晶圆半径恒为 1.0），相机距离为固定常量，
    // 无需再按缺陷分布半径推导，直接恢复默认视角即可。
    m_state->resetCamera(1.0f);
    m_viewport->update();
    logInfo(tr("视角已恢复默认。"));
}

void MainWindow::onCaptureModeToggled(bool on)
{
    m_viewport->setCaptureModeActive(on);
    logInfo(on ? tr("框选模式已开启：在视口中按住左键拖拽以选择区域。")
               : tr("框选模式已关闭，左键恢复为旋转/拾取。"));
}

void MainWindow::onExportScreenshotClicked()
{
    // 全视口截图（等价于框选整个画面）
    const QRect fullRect(0, 0, m_viewport->width(), m_viewport->height());
    onCaptureRegionSelected(fullRect);
}

void MainWindow::onCaptureRegionSelected(const QRect &widgetRect)
{
    // 委托给 CaptureController：路径推导 + 离屏 FBO 渲染写文件；
    // 需要弹保存对话框时通过回调注入窗口上下文，控制器本身不依赖具体 UI。
    const bool autoSave = m_autoCaptureCheck->isChecked() && !m_lastLoadedFile.isEmpty();
    const QString baseName =
        m_lastLoadedFile.isEmpty() ? QString() : QFileInfo(m_lastLoadedFile).completeBaseName();

    const CaptureController::CaptureResult result = m_captureController->captureRegion(
        widgetRect, autoSave, baseName, [this](const QString &suggested) {
            return QFileDialog::getSaveFileName(this, tr("保存截图"), suggested,
                                               tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
        });

    if (result.cancelled) {
        logInfo(tr("截图导出已取消。"));
        return;
    }

    if (!result.ok) {
        // 区域过小属于用户误操作，只提示不弹窗
        if (result.error == QStringLiteral("region too small")) {
            logWarning(tr("框选区域过小（需大于 8x8 像素），已忽略。"));
            return;
        }
        logError(tr("离屏截图导出失败：%1").arg(result.error.isEmpty() ? tr("未知错误")
                                                                       : result.error));
        m_statusLabel->setText(tr("截图导出失败"));
        QMessageBox::warning(this, tr("截图失败"),
                             tr("离屏渲染导出失败：\n%1").arg(result.error));
        return;
    }

    const QRect &rect = result.region;
    m_statusLabel->setText(tr("截图已保存：%1").arg(QFileInfo(result.path).fileName()));
    logSuccess(tr("报告截图已导出：%1 (%2x%3 像素)")
                   .arg(result.path)
                   .arg(rect.width())
                   .arg(rect.height()));

    const QSize offscreen = m_renderer->offscreenSize();
    m_perfPanel->updateOffscreenSize(
        QStringLiteral("%1x%2").arg(offscreen.width()).arg(offscreen.height()));
    m_perfPanel->updateHeatmapBuild(m_renderer->lastHeatmapBuildMs());
    m_perfPanel->updateGpuResources(m_renderer->gpuStatisticsText());
}

void MainWindow::onDefectPicked(int defectId, int filteredIndex)
{
    const auto &current = m_renderer->currentDefects();
    if (filteredIndex < 0 || filteredIndex >= current.size()) {
        onPickMissed();
        return;
    }

    const DefectPoint &point = current.at(filteredIndex);
    m_infoDialog->setModal(false);

    // 邻域信息（§3-8）：在当前可见缺陷集合里统计，便于判断簇状异常。
    // 单点暴力扫描 O(N)，只在点击时算一次，不影响交互响应。
    const NeighborInfo neighbor =
        Neighborhood::analyze(current, filteredIndex, Neighborhood::kDefaultRadiusMm,
                              Neighborhood::kDefaultLinkMm);
    m_infoDialog->showDefectWithNeighbors(point, neighbor);

    const QString clusterHint =
        neighbor.clusterSize > 3 ? tr("（成簇：同簇 %1 个）").arg(neighbor.clusterSize) : QString();

    logSuccess(
        tr("命中缺陷 #%1 [%2] 等级 %3，坐标 (%4, %5) mm，面积 %6 um²；半径 %7 mm 内 %8 个缺陷%9。")
            .arg(defectId)
            .arg(defectTypeName(point.type))
            .arg(static_cast<int>(point.severity))
            .arg(point.x, 0, 'f', 3)
            .arg(point.y, 0, 'f', 3)
            .arg(point.area, 0, 'f', 2)
            .arg(Neighborhood::kDefaultRadiusMm, 0, 'f', 0)
            .arg(neighbor.neighborsInRadius)
            .arg(clusterHint));

    m_statusLabel->setText(tr("已选中缺陷 #%1").arg(defectId));
}

void MainWindow::onPickMissed()
{
    logInfo(tr("点击位置未命中缺陷标记点。"));
    m_statusLabel->setText(tr("未命中缺陷"));
}

void MainWindow::onRendererReady(const QString &infoText)
{
    logSuccess(infoText);
    m_statusLabel->setText(tr("渲染器就绪"));

    // 重试成功后恢复被禁用的控件（见 onRendererError）
    if (m_rendererFailed) {
        m_rendererFailed = false;
        for (QWidget *w : {static_cast<QWidget *>(m_heatmapCheck),
                           static_cast<QWidget *>(m_markerCheck),
                           static_cast<QWidget *>(m_waferCheck),
                           static_cast<QWidget *>(m_zScaleSlider),
                           static_cast<QWidget *>(m_captureModeCheck),
                           static_cast<QWidget *>(m_autoCaptureCheck),
                           static_cast<QWidget *>(m_exportButton),
                           static_cast<QWidget *>(m_jumpButton),
                           static_cast<QWidget *>(m_statisticsButton),
                           static_cast<QWidget *>(m_exportFilteredButton)}) {
            w->setEnabled(true);
        }
        logInfo(tr("渲染器已重新初始化，控件恢复可用。"));
    }

    // 渲染器在 initializeGL 后才可用，此时同步初始状态并尝试加载默认数据集
    m_renderer->setHeatmapEnabled(m_state->heatmapEnabled());
    m_renderer->setMarkersEnabled(m_state->markersEnabled());
    m_renderer->setWaferSurfaceVisible(m_state->waferSurfaceVisible());
    m_renderer->setZScale(m_state->zScale());

    logInfo(tr("GPU 资源清单：%1").arg(m_renderer->gpuStatisticsText()));
    m_perfPanel->updateGpuResources(m_renderer->gpuStatisticsText());

    // 若存在默认数据集则自动加载
    if (!m_lastLoadedFile.isEmpty() && QFileInfo::exists(m_lastLoadedFile)
        && m_datasetController->bufferVersion() == 0) {
        loadDatasetFile(m_lastLoadedFile);
    }
}

void MainWindow::onRendererError(const QString &message)
{
    logError(tr("渲染器初始化失败：%1").arg(message));
    m_statusLabel->setText(tr("渲染初始化失败"));

    // 渲染不可用时，依赖 3D 的控件继续可点只会让用户以为是「程序没反应」，
    // 这里统一禁用并提供「重新初始化」按钮（§4：失败后没有重试入口）。
    m_rendererFailed = true;
    for (QWidget *w : {static_cast<QWidget *>(m_heatmapCheck),
                       static_cast<QWidget *>(m_markerCheck),
                       static_cast<QWidget *>(m_waferCheck),
                       static_cast<QWidget *>(m_zScaleSlider),
                       static_cast<QWidget *>(m_captureModeCheck),
                       static_cast<QWidget *>(m_autoCaptureCheck),
                       static_cast<QWidget *>(m_exportButton),
                       static_cast<QWidget *>(m_jumpButton),
                       static_cast<QWidget *>(m_statisticsButton),
                       static_cast<QWidget *>(m_exportFilteredButton)}) {
        w->setEnabled(false);
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(tr("OpenGL 初始化失败"));
    box.setText(tr("无法初始化 3D 渲染环境：\n%1\n\n请确认显卡驱动支持 OpenGL 3.3 Core Profile。")
                    .arg(message));
    QPushButton *retry = box.addButton(tr("重新初始化"), QMessageBox::AcceptRole);
    box.addButton(tr("关闭"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == retry) {
        logInfo(tr("正在尝试重新初始化渲染器..."));
        m_viewport->retryInitialize();
    }
}

void MainWindow::onCameraChanged()
{
    const QVector3D cam = m_state->cameraPosition();
    m_cameraLabel->setText(tr("相机：yaw %1° / pitch %2° / 距离 %3")
                               .arg(m_state->yawDeg(), 0, 'f', 1)
                               .arg(m_state->pitchDeg(), 0, 'f', 1)
                               .arg(cam.length(), 0, 'f', 1));
}

void MainWindow::logInfo(const QString &message)
{
    qCInfo(lcApp).noquote() << message;
}

void MainWindow::logSuccess(const QString &message)
{
    // Qt 没有 Success 级别：用 Info + 固定前缀表达，Logger 端按前缀映射为绿色。
    // 注意这里不能直接写 qCInfo(...) << "[OK] " + message：logSuccess 是普通函数，
    // 参数在调用前就已构造，级别过滤起不到「省掉字符串拼接」的作用。
    if (lcApp().isInfoEnabled())
        qCInfo(lcApp).noquote() << QStringLiteral("[OK] ") + message;
}

void MainWindow::logWarning(const QString &message)
{
    qCWarning(lcApp).noquote() << message;
}

void MainWindow::logError(const QString &message)
{
    qCCritical(lcApp).noquote() << message;
}

void MainWindow::persistUiState()
{
    if (!m_settings)
        return;

    // ---- 相机：直接读 SceneStateManager 的当前姿态（与视口共享同一份状态） ----
    AppSettings::CameraState cam;
    if (m_state) {
        cam.valid = true;
        cam.yawDeg = m_state->yawDeg();
        cam.pitchDeg = m_state->pitchDeg();
        cam.distance = m_state->distance();
        const QVector3D target = m_state->target();
        cam.targetX = target.x();
        cam.targetY = target.y();
        cam.targetZ = target.z();
        m_settings->saveCamera(cam);
    }

    // ---- 筛选 ----
    AppSettings::FilterState filter;
    filter.minSeverity = static_cast<int>(m_filter->minSeverity());
    filter.maxSeverity = static_cast<int>(m_filter->maxSeverity());
    filter.types = m_filter->types();
    filter.layers = m_filter->layers();
    filter.minArea = m_filter->minArea();
    filter.maxArea = m_filter->maxArea();
    filter.minHeight = m_filter->minHeight();
    filter.maxHeight = m_filter->maxHeight();
    filter.layerOffset = m_filter->layerOffsetEnabled();
    m_settings->saveFilter(filter);

    // ---- 渲染开关 ----
    AppSettings::RenderState render;
    render.heatmap = m_state->heatmapEnabled();
    render.markers = m_state->markersEnabled();
    render.wafer = m_state->waferSurfaceVisible();
    render.zScale = m_state->zScale();
    render.captureMode = m_captureModeCheck->isChecked();
    render.autoCapture = m_autoCaptureCheck->isChecked();
    m_settings->saveRender(render);

    // ---- 数据集路径 ----
    if (!m_filePathEdit->text().trimmed().isEmpty())
        m_settings->setLastDatasetPath(m_filePathEdit->text().trimmed());

    m_settings->sync();
}

void MainWindow::restoreUiState()
{
    if (!m_settings)
        return;

    // ---- 数据集路径：优先用上次成功打开的，其次用默认探测结果 ----
    const QString savedPath = m_settings->lastDatasetPath();
    if (!savedPath.isEmpty() && QFileInfo::exists(savedPath))
        m_filePathEdit->setText(savedPath);

    // ---- 相机 ----
    const AppSettings::CameraState cam = m_settings->loadCamera();
    if (cam.valid && m_state) {
        if (m_state->restoreCamera(cam.yawDeg, cam.pitchDeg, cam.distance,
                                   QVector3D(cam.targetX, cam.targetY, cam.targetZ))) {
            logInfo(tr("已恢复上次视角（yaw %1°，pitch %2°，距离 %3）。")
                        .arg(cam.yawDeg, 0, 'f', 1)
                        .arg(cam.pitchDeg, 0, 'f', 1)
                        .arg(cam.distance, 0, 'f', 2));
        }
    }

    // ---- 渲染开关 ----
    const AppSettings::RenderState render = m_settings->loadRender();
    {
        const QSignalBlocker b1(m_heatmapCheck);
        const QSignalBlocker b2(m_markerCheck);
        const QSignalBlocker b3(m_waferCheck);
        m_heatmapCheck->setChecked(render.heatmap);
        m_markerCheck->setChecked(render.markers);
        m_waferCheck->setChecked(render.wafer);
    }
    m_state->setHeatmapEnabled(render.heatmap);
    m_state->setMarkersEnabled(render.markers);
    m_state->setWaferSurfaceVisible(render.wafer);
    {
        // 滑块 0..100 对应 0.1x..10.0x，反算回滑块值
        const int sliderValue = qBound(0, qRound((render.zScale - 0.1f) / 0.099f), 100);
        const QSignalBlocker b(m_zScaleSlider);
        m_zScaleSlider->setValue(sliderValue);
        m_pendingZValue = sliderValue;
        m_zScaleLabel->setText(tr("高度放缩：%1x").arg(render.zScale, 0, 'f', 2));
    }
    m_state->setZScale(render.zScale);
    {
        const QSignalBlocker b(m_captureModeCheck);
        m_captureModeCheck->setChecked(render.captureMode);
    }
    m_viewport->setCaptureModeActive(render.captureMode);
    m_autoCaptureCheck->setChecked(render.autoCapture);

    // ---- 筛选条件 ----
    // 类型列表在构造时已按枚举填好；层列表要等数据集加载后才有内容，
    // 因此这里只把条件写进 DefectFilter，onDatasetReady() 在 populateLayerList()
    // 之后会再次调用本函数对齐层勾选状态。
    const AppSettings::FilterState filter = m_settings->loadFilter();
    m_filter->setMinSeverity(static_cast<DefectSeverity>(qBound(1, filter.minSeverity, 5)));
    m_filter->setMaxSeverity(static_cast<DefectSeverity>(qBound(1, filter.maxSeverity, 5)));
    m_filter->setTypes(filter.types);
    m_filter->setAreaRange(filter.minArea, filter.maxArea);
    m_filter->setHeightRange(filter.minHeight, filter.maxHeight);
    m_filter->setLayerOffsetEnabled(filter.layerOffset);

    for (int i = 0; i < m_severityBox->count(); ++i) {
        if (m_severityBox->itemData(i).toInt() == filter.minSeverity) {
            const QSignalBlocker b(m_severityBox);
            m_severityBox->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_severityMaxBox->count(); ++i) {
        if (m_severityMaxBox->itemData(i).toInt() == filter.maxSeverity) {
            const QSignalBlocker b(m_severityMaxBox);
            m_severityMaxBox->setCurrentIndex(i);
            break;
        }
    }
    for (int i = 0; i < m_typeList->count(); ++i) {
        QListWidgetItem *item = m_typeList->item(i);
        const QSignalBlocker b(m_typeList);
        // 空集合 = 全部勾选；非空时只勾选集合内的类型
        const bool checked =
            filter.types.isEmpty() || filter.types.contains(item->data(Qt::UserRole).toInt());
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    {
        const QSignalBlocker b1(m_areaMinSpin);
        const QSignalBlocker b2(m_areaMaxSpin);
        const QSignalBlocker b3(m_heightMinSpin);
        const QSignalBlocker b4(m_heightMaxSpin);
        const QSignalBlocker b5(m_layerOffsetCheck);
        m_areaMinSpin->setValue(filter.minArea);
        m_areaMaxSpin->setValue(filter.maxArea);
        m_heightMinSpin->setValue(filter.minHeight);
        m_heightMaxSpin->setValue(filter.maxHeight);
        m_layerOffsetCheck->setChecked(filter.layerOffset);
    }

    // ---- 层勾选：此时列表已按真实层号填充 ----
    {
        const QSignalBlocker b(m_layerList);
        for (int i = 0; i < m_layerList->count(); ++i) {
            QListWidgetItem *item = m_layerList->item(i);
            const bool checked = filter.layers.isEmpty()
                                 || filter.layers.contains(item->data(Qt::UserRole).toInt());
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
    m_filter->setLayers(checkedLayers());
    if (!filter.types.isEmpty())
        m_filter->setTypes(filter.types);
}

void MainWindow::onStatsUpdated(const QString &summary)
{
    m_perfPanel->updateSummary(summary);
    m_perfPanel->updateHeatmapBuild(m_renderer->lastHeatmapBuildMs());

    const QSize offscreen = m_renderer->offscreenSize();
    if (offscreen.isValid()) {
        m_perfPanel->updateOffscreenSize(
            QStringLiteral("%1x%2").arg(offscreen.width()).arg(offscreen.height()));
    }
}

} // namespace wafer
