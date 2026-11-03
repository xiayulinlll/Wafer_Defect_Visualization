#ifndef SCENERENDERER_H
#define SCENERENDERER_H

#include "business/GeometryBuilder.h"
#include "business/PerformanceStats.h"
#include "business/RayPicker.h"
#include "core/DefectTypes.h"

#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QSize>
#include <QVector>
#include <QVector3D>

#include <memory>

class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class QOpenGLBuffer;

namespace wafer {

class ShaderManager;
class GpuResourceManager;
class HeatmapRenderer;
class FboRenderer;
class SceneStateManager;
class DataBufferManager;

/** 一次 GPU 计时查询结果缓存（异步回读，避免 stall） */
struct GpuTimerState {
    unsigned int queryIds[2] = {0, 0};
    int writeIndex = 0;
    bool inFlight[2] = {false, false};
    double lastElapsedMs = -1.0;
    bool available = false;
};

/**
 * @brief 场景渲染器（渲染层核心）。
 *
 * 每帧渲染流程（对应架构 2.3）：
 *   清除缓冲 -> 更新 uniform 矩阵 -> 绘制参考网格 -> 绘制晶圆主体(采样热力纹理)
 *   -> 绘制缺陷标记点 -> 可选：FBO 离屏渲染
 *
 * 所有 OpenGL 资源（VAO/VBO/纹理/FBO/Program）都在本层创建与销毁，
 * 业务层与 UI 层不直接触碰 OpenGL 对象。
 */
class SceneRenderer : protected QOpenGLFunctions_3_3_Core
{
public:
    SceneRenderer();
    ~SceneRenderer() override;

    SceneRenderer(const SceneRenderer &) = delete;
    SceneRenderer &operator=(const SceneRenderer &) = delete;

    /**
     * @brief 初始化全部渲染资源。
     * @param shaderDir  着色器目录（":/shaders" 或绝对路径）
     * @param errorLog   输出错误
     */
    bool initialize(const QString &shaderDir, QString *errorLog = nullptr);

    /** 释放全部 GPU 资源（aboutToBeDestroyed 中调用） */
    void shutdown();

    // ---------------- 数据更新接口（由 UI/业务层调用，内部转投到渲染线程） ----------------

    /**
     * @brief 更新缺陷数据与晶圆半径。
     * 会重新计算几何、重建热力纹理，并释放旧的 GPU 缓冲（难点5）。
     * @param defects 已过滤的缺陷点集合
     * @param waferRadius 晶圆半径 (mm)
     * @param zScale Z 放大系数
     */
    void setDefectData(const QVector<DefectPoint> &defects, float waferRadius, float zScale);

    /** 仅更新 Z 放大系数（无需重建热力纹理，只更新标记点几何） */
    void setZScale(float zScale);

    /** 仅切换筛选结果（等价 setDefectData 的轻量版本，会重建热力图） */
    void setFilteredDefects(const QVector<DefectPoint> &defects);

    void setHeatmapEnabled(bool on);
    void setMarkersEnabled(bool on);
    void setWaferSurfaceVisible(bool on);

    /**
     * @brief 无筛选时是否自动生成热力图（关闭后「一键隐藏热力」不会留下旧的热区）。
     */
    void setSyntheticHeatmapEnabled(bool on) { m_syntheticHeatmap = on; }

    /**
     * @brief 设置选中项（按过滤结果下标）。
     *
     * 会立即同步单点高亮 VBO，因此「点中 → 高亮」在同一帧可见，
     * 不需要等下一次筛选/缩放触发几何重建。
     */
    void setSelectedIndex(int index);

    void setSelectedDefectId(int id) { m_selectedDefectId = id; }
    int selectedDefectId() const { return m_selectedDefectId; }

    /**
     * @brief 注入数据缓冲（可选）。
     *
     * 用于「尚未应用筛选时自动生成热力图」：渲染器只在启动早期读取一次快照，
     * 不持有所有权，因此不会引入跨线程生命周期问题。
     */
    void setDataBufferManager(DataBufferManager *bufferManager) { m_bufferManager = bufferManager; }

    /** 视口缩放比（物理像素 / CSS 像素），用于把 CSS 像素尺寸换算成 gl_PointSize */
    void setDevicePixelRatio(float dpr);

    /** 设置视口尺寸（resizeGL 中调用），内部会同步 FBO 尺寸 */
    void setViewportSize(const QSize &size);

    // ---------------- 渲染 ----------------

    /**
     * @brief 渲染一帧到当前绑定目标（默认帧缓冲）。
     * @param state 相机与开关状态
     */
    void renderFrame(SceneStateManager *state);

    /**
     * @brief 流程4：框选区域导出（屏幕像素 1:1 + FBO 重绘叠加矢量元素）。
     *
     * 实现要点（旧版把「整个场景」按选区宽高比重绘，导致导出内容与框选不符）：
     *  1. 先把当前帧 flush 到默认帧缓冲，再从后缓冲按 **区域坐标** `glReadPixels`
     *     （只读选区像素，不再整屏读回），得到与屏幕 100% 一致的底图；
     *  2. 再用 FBO 以「区域同尺寸、相机与投影完全不变」重绘一次，作为
     *     离屏管线可用性的兜底校验；
     *  3. 最后用 QPainter 把选区边框、尺寸标签与标题水印画到图片上再落盘。
     *
     * @param region       选中区域（widget 像素坐标，原点左上）
     * @param viewportSize 当前视口尺寸
     * @param outFile      输出文件路径
     * @param state        相机状态
     * @param errorMessage 失败原因
     * @param caption      可选：写到图片顶部的水印文字（通常是数据集名 + 时间）
     * @param dpr          设备像素比（HiDPI 下底图按物理像素读回）
     * @return 是否成功保存
     */
    bool captureRegionToFile(const QRect &region, const QSize &viewportSize, const QString &outFile,
                             SceneStateManager *state, QString *errorMessage,
                             const QString &caption = QString(), float dpr = 1.0f);

    /** 性能统计（渲染层每帧上报，业务层负责聚合与上报 UI） */
    PerformanceStats *performanceStats() { return m_perfStats.get(); }

    /** GPU 资源统计文本 */
    QString gpuStatisticsText() const;

    /** 当前是否已成功初始化 */
    bool isInitialized() const { return m_initialized; }

    /** 当前缺陷数量（渲染层内部持有的过滤后集合） */
    int currentDefectCount() const { return m_defects.size(); }

    /** 当前世界坐标缓存（供拾取使用，避免业务层重复映射） */
    const QVector<QVector3D> &worldPositions() const { return m_worldPositions; }
    const QVector<DefectPoint> &currentDefects() const { return m_defects; }

    /**
     * @brief 拾取用的 XZ 空间索引（与 worldPositions 同序）。
     *
     * 每次几何重建时同步构建，因此用户点击时不需要任何额外准备；
     * 用 `nullptr` 则表示索引尚未就绪（调用方会退回线性扫描）。
     */
    const PickGrid *pickGrid() const { return m_pickGrid ? m_pickGrid.get() : nullptr; }

    /** 最近一次热力图 CPU 构建耗时 */
    double lastHeatmapBuildMs() const;

    /** FBO 尺寸（供 UI 显示离屏渲染分辨率） */
    QSize offscreenSize() const;

private:
    // ---- 子步骤 ----
    void buildStaticGeometry();
    void rebuildDefectGeometry();
    void rebuildHeatmap();
    void rebuildSelectedMarker();
    void drawGrid(const QMatrix4x4 &view, const QMatrix4x4 &proj);
    void drawWafer(const QMatrix4x4 &view, const QMatrix4x4 &proj, SceneStateManager *state);
    void drawMarkers(const QMatrix4x4 &view, const QMatrix4x4 &proj, SceneStateManager *state);

    /**
     * @brief 在导出图上叠加选区框、尺寸标签与标题水印。
     * @param image   已裁好的区域图像
     * @param caption 顶部标题（可为空）
     * @param dpr     设备像素比，用于按物理像素调整线宽/字号
     */
    static QImage annotateCapture(QImage image, const QString &caption, float dpr);

    /** 以「当前相机 + 指定 viewport」绘制完整场景（屏幕与 FBO 共用同一份绘制顺序） */
    void renderSceneContent(const QMatrix4x4 &view, const QMatrix4x4 &proj,
                            SceneStateManager *state);

    /** 标记点基础像素尺寸（= 基础像素 × 视口高度缩放 × DPR，与 RayPicker 共用公式） */
    float markerBasePixelSize() const;

    /** 以顶点偏移方式绘制单个选中标记（复用同一 VBO，避免额外 VAO/拷贝） */
    void drawSelectedMarker(QOpenGLShaderProgram *prog, int index);

    void beginGpuTimer();
    void endGpuTimer();

    // ---- 资源 ----
    std::unique_ptr<ShaderManager> m_shaders;
    std::unique_ptr<GpuResourceManager> m_resources;
    std::unique_ptr<HeatmapRenderer> m_heatmap;
    std::unique_ptr<FboRenderer> m_fbo;
    std::unique_ptr<PerformanceStats> m_perfStats;
    DataBufferManager *m_bufferManager = nullptr; // 不持有所有权；仅启动早期读取一次快照

    // 晶圆网格
    QOpenGLVertexArrayObject *m_waferVao = nullptr;
    QOpenGLBuffer *m_waferVbo = nullptr;
    QOpenGLBuffer *m_waferEbo = nullptr;
    int m_waferIndexCount = 0;

    // 缺陷标记点（单个 VBO 批量绘制）
    QOpenGLVertexArrayObject *m_markerVao = nullptr;
    QOpenGLBuffer *m_markerVbo = nullptr;
    int m_markerCount = 0;
    QOpenGLVertexArrayObject *m_selectedVao = nullptr;
    QOpenGLBuffer *m_selectedVbo = nullptr;

    // 线段（网格 + 坐标轴 + 轮廓）
    QOpenGLVertexArrayObject *m_lineVao = nullptr;
    QOpenGLBuffer *m_lineVbo = nullptr;
    int m_gridVertexCount = 0;
    int m_axisVertexCount = 0;
    int m_outlineVertexCount = 0;

    // ---- 状态 ----
    QVector<DefectPoint> m_defects;         // 当前渲染的缺陷集合
    QVector<MarkerVertex> m_markers;        // 与 m_defects 一一对应的标记点顶点（CPU 侧缓存）
    QVector<QVector3D> m_worldPositions;    // 与 m_defects 一一对应的世界坐标缓存
    std::unique_ptr<PickGrid> m_pickGrid;   // 与 m_worldPositions 同序的空间索引
    float m_waferRadius = 150.0f;
    float m_zScale = 1.0f;
    int m_selectedIndex = -1;
    int m_selectedDefectId = -1;
    QSize m_viewportSize{1280, 800};
    float m_devicePixelRatio = 1.0f;

    bool m_heatmapEnabled = true;
    bool m_markersEnabled = true;
    bool m_waferVisible = true;
    bool m_syntheticHeatmap = false; // 渲染器自动生成的数据没有筛选结果
    bool m_initialized = false;

    GpuTimerState m_gpuTimer;
    bool m_glInitialized = false;
};

} // namespace wafer

#endif // SCENERENDERER_H
