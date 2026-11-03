#include "render/SceneRenderer.h"

#include "business/PerformanceStats.h"
#include "business/SceneStateManager.h"
#include "data/DataBufferManager.h"
#include "render/FboRenderer.h"
#include "render/GpuResourceManager.h"
#include "render/HeatmapRenderer.h"
#include "render/ShaderManager.h"

#include <QElapsedTimer>
#include <QFont>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QPen>
#include <QRect>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wafer {

namespace {
// 资源名常量：与 GpuResourceManager 的登记名保持一致
const char *const kNameWaferVao = "Wafer.VAO";
const char *const kNameWaferVbo = "Wafer.VBO";
const char *const kNameWaferEbo = "Wafer.EBO";
const char *const kNameMarkerVao = "Marker.VAO";
const char *const kNameMarkerVbo = "Marker.VBO";
const char *const kNameSelectedVao = "SelectedMarker.VAO";
const char *const kNameSelectedVbo = "SelectedMarker.VBO";
const char *const kNameLineVao = "Line.VAO";
const char *const kNameLineVbo = "Line.VBO";

// 注意：几何/热力参数统一取自 core/Constants.h，这里不再重复定义同名常量，
// 否则「改一处不生效」的问题会反复出现。
using constants::kAxisLength;
using constants::kGridDivisions;
using constants::kGridHalfExtent;
using constants::kHeatmapResolution;
using constants::kHeatmapShaderGain;
using constants::kWaferSegments;
using constants::kWaferThickness;

constexpr float kMarkerMinPixelSize = 2.0f;
constexpr float kMarkerMaxPixelSize = 128.0f;

/** 导出图片时叠加的标注颜色（与框选矩形/悬浮框配色一致） */
const QColor kCaptureAccent(90, 200, 255);

/**
 * 把「线性灰度」量化成 8bit 时使用的软饱和曲线，与热力纹理完全一致，
 * 保证「无筛选时自动生成的热力图」与「有筛选时的真实热力图」观感一致。
 */
inline unsigned char quantizeHeat(float v)
{
    const float s = 1.0f - std::exp(-v * constants::kHeatmapSaturation);
    return static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, s * 255.0f)));
}
} // namespace

SceneRenderer::SceneRenderer()
    : m_perfStats(std::make_unique<PerformanceStats>())
{
}

SceneRenderer::~SceneRenderer()
{
    // 资源应已通过 shutdown() 释放；此处仅确保 unique_ptr 顺序析构。
    // 注意：不要在无上下文时调用 OpenGL API。
}

bool SceneRenderer::initialize(const QString &shaderDir, QString *errorLog)
{
    QString log;

    if (!initializeOpenGLFunctions()) {
        if (errorLog)
            *errorLog += QStringLiteral("[SceneRenderer] OpenGL 3.3 函数指针初始化失败\n");
        return false;
    }
    m_glInitialized = true;

    m_resources = std::make_unique<GpuResourceManager>();
    if (!m_resources->initialize(&log)) {
        if (errorLog)
            *errorLog += log;
        return false;
    }

    m_shaders = std::make_unique<ShaderManager>();
    if (!m_shaders->initialize(shaderDir, &log)) {
        if (errorLog)
            *errorLog += log;
        return false;
    }

    m_heatmap = std::make_unique<HeatmapRenderer>();
    if (!m_heatmap->initialize(m_resources.get(), QSize(kHeatmapResolution, kHeatmapResolution),
                               &log)) {
        if (errorLog)
            *errorLog += log;
        return false;
    }

    m_fbo = std::make_unique<FboRenderer>();
    if (!m_fbo->initialize(m_resources.get(), m_viewportSize, &log)) {
        if (errorLog)
            *errorLog += log;
        return false;
    }

    buildStaticGeometry();

    // 无数据时也先准备一张空热力纹理：保证 uHeatmap 始终绑定有效纹理对象，
    // 避免部分驱动在「采样器未绑定」时给出未定义颜色。
    m_syntheticHeatmap = true;
    rebuildHeatmap();

    m_initialized = true;
    return true;
}

void SceneRenderer::buildStaticGeometry()
{
    // ---------------- 晶圆主体（VAO + VBO + EBO） ----------------
    QVector<WaferVertex> waferVerts;
    QVector<unsigned int> waferIndices;
    GeometryBuilder::buildWaferDisk(1.0f, kWaferSegments, kWaferThickness, &waferVerts,
                                    &waferIndices);

    m_waferVao = m_resources->createVertexArray(QString::fromLatin1(kNameWaferVao));
    m_waferVbo = m_resources->createArrayBuffer(QString::fromLatin1(kNameWaferVbo));
    m_waferEbo = m_resources->createIndexBuffer(QString::fromLatin1(kNameWaferEbo));

    if (m_waferVao && m_waferVbo && m_waferEbo) {
        m_waferVao->bind();

        m_waferVbo->bind();
        m_waferVbo->allocate(waferVerts.constData(), waferVerts.size() * int(sizeof(WaferVertex)));

        m_waferEbo->bind();
        m_waferEbo->allocate(waferIndices.constData(),
                             waferIndices.size() * int(sizeof(unsigned int)));

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaferVertex),
                              reinterpret_cast<void *>(offsetof(WaferVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WaferVertex),
                              reinterpret_cast<void *>(offsetof(WaferVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WaferVertex),
                              reinterpret_cast<void *>(offsetof(WaferVertex, u)));

        m_waferVao->release();
        m_waferVbo->release();
        m_waferEbo->release();

        m_waferIndexCount = waferIndices.size();
    }

    // ---------------- 线段（网格 / 坐标轴 / 轮廓）统一放一个 VBO ----------------
    QVector<LineVertex> grid, axes, outline;
    GeometryBuilder::buildGroundGrid(kGridHalfExtent, kGridDivisions, &grid);
    GeometryBuilder::buildAxes(kAxisLength, &axes);
    GeometryBuilder::buildWaferOutline(1.0f, kWaferSegments,
                                       QVector3D(constants::kOutlineColorR,
                                                 constants::kOutlineColorG,
                                                 constants::kOutlineColorB),
                                       &outline);

    QVector<LineVertex> lineVerts;
    lineVerts << grid << axes << outline;

    m_gridVertexCount = grid.size();
    m_axisVertexCount = axes.size();
    m_outlineVertexCount = outline.size();

    m_lineVao = m_resources->createVertexArray(QString::fromLatin1(kNameLineVao));
    m_lineVbo = m_resources->createArrayBuffer(QString::fromLatin1(kNameLineVbo));

    if (m_lineVao && m_lineVbo && !lineVerts.isEmpty()) {
        m_lineVao->bind();
        m_lineVbo->bind();
        m_lineVbo->allocate(lineVerts.constData(), lineVerts.size() * int(sizeof(LineVertex)));

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                              reinterpret_cast<void *>(offsetof(LineVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                              reinterpret_cast<void *>(offsetof(LineVertex, r)));

        m_lineVao->release();
        m_lineVbo->release();
    }

    // ---------------- 标记点 VAO（VBO 在 rebuildDefectGeometry 中填充/重建） ----------------
    m_markerVao = m_resources->createVertexArray(QString::fromLatin1(kNameMarkerVao));
    m_markerVbo = m_resources->createArrayBuffer(QString::fromLatin1(kNameMarkerVbo));

    if (m_markerVao && m_markerVbo) {
        m_markerVao->bind();
        m_markerVbo->bind();
        // 预分配 1 个顶点，保证 attrib 指针有效；后续用 allocate 重分配
        m_markerVbo->allocate(nullptr, int(sizeof(MarkerVertex)));

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, intensity)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, radius)));

        m_markerVao->release();
        m_markerVbo->release();
    }

    // ---------------- 选中高亮标记 VAO（单个点） ----------------
    m_selectedVao = m_resources->createVertexArray(QString::fromLatin1(kNameSelectedVao));
    m_selectedVbo = m_resources->createArrayBuffer(QString::fromLatin1(kNameSelectedVbo));

    if (m_selectedVao && m_selectedVbo) {
        m_selectedVao->bind();
        m_selectedVbo->bind();
        m_selectedVbo->allocate(nullptr, int(sizeof(MarkerVertex)));

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, intensity)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(MarkerVertex),
                              reinterpret_cast<void *>(offsetof(MarkerVertex, radius)));

        m_selectedVao->release();
        m_selectedVbo->release();
    }

    // ---------------- GPU 计时查询对象（异步回读，避免管线 stall） ----------------
    GLuint queries[2] = {0, 0};
    glGenQueries(2, queries);
    m_gpuTimer.queryIds[0] = queries[0];
    m_gpuTimer.queryIds[1] = queries[1];
    m_gpuTimer.available = (queries[0] != 0 && queries[1] != 0);
}

void SceneRenderer::setDefectData(const QVector<DefectPoint> &defects, float waferRadius,
                                  float zScale)
{
    m_syntheticHeatmap = false;
    m_defects = defects;
    m_waferRadius = waferRadius > 0.0f ? waferRadius : constants::kDefaultWaferRadiusMm;
    m_zScale = zScale;

    // 选中项若已不在当前过滤结果中，清空选中
    m_selectedIndex = -1;
    for (int i = 0; i < m_defects.size(); ++i) {
        if (m_defects[i].id == m_selectedDefectId) {
            m_selectedIndex = i;
            break;
        }
    }

    rebuildDefectGeometry();
    rebuildHeatmap();
}

void SceneRenderer::setFilteredDefects(const QVector<DefectPoint> &defects)
{
    setDefectData(defects, m_waferRadius, m_zScale);
}

void SceneRenderer::setZScale(float zScale)
{
    if (qFuzzyCompare(m_zScale + 1.0f, zScale + 1.0f))
        return;

    // Z 缩放只影响世界 Y 坐标，与热力纹理无关：这里只重建标记点几何，
    // 不再触发 1024x1024 热力纹理重建（拖动滑块时的卡顿主要来源）。
    m_zScale = zScale;
    rebuildDefectGeometry();
}

void SceneRenderer::setSelectedIndex(int index)
{
    if (index < -1 || index >= m_defects.size())
        index = -1;
    if (m_selectedIndex == index)
        return;

    m_selectedIndex = index;
    // 立即刷新单点高亮，保证「点中即亮」，而不是等下一次几何重建
    rebuildSelectedMarker();
}

void SceneRenderer::setDevicePixelRatio(float dpr)
{
    if (dpr > 0.0f)
        m_devicePixelRatio = dpr;
}

void SceneRenderer::rebuildDefectGeometry()
{
    if (!m_markerVao || !m_markerVbo || !m_resources)
        return;

    // 只构建一次：世界坐标由 GeometryBuilder 同时写入位置缓存与 VBO 顶点，
    // 拾取直接复用 m_worldPositions（避免逐点二次映射）
    m_worldPositions.clear();
    m_worldPositions.reserve(m_defects.size());
    GeometryBuilder::buildDefectMarkers(m_defects, m_waferRadius, m_zScale, &m_markers,
                                        &m_worldPositions);

    m_markerVao->bind();
    m_markerVbo->bind();
    if (m_markers.isEmpty()) {
        m_markerVbo->allocate(nullptr, int(sizeof(MarkerVertex)));
        m_markerCount = 0;
    } else {
        // 一次性 allocate + 上传，保证所有缺陷点在同一个 VBO，
        // 绘制时只需一次 glDrawArrays（难点2：低 DrawCall）
        m_markerVbo->allocate(m_markers.constData(),
                              m_markers.size() * int(sizeof(MarkerVertex)));
        m_markerCount = m_markers.size();
    }
    m_markerVbo->release();
    m_markerVao->release();

    // 同步构建拾取索引（与 worldPositions 同序），点击时只探查射线经过的格子。
    // 点数为 0 时直接释放，避免保留空索引让调用方误判「已就绪」。
    if (m_worldPositions.isEmpty()) {
        m_pickGrid.reset();
    } else {
        if (!m_pickGrid)
            m_pickGrid = std::make_unique<PickGrid>();
        m_pickGrid->build(m_worldPositions, m_defects);
    }

    // 更新选中高亮点几何（含清空场景：上传 0 顶点即可隐藏）
    rebuildSelectedMarker();
}

void SceneRenderer::rebuildSelectedMarker()
{
    if (!m_selectedVao || !m_selectedVbo)
        return;

    // 选中项不在当前集合中：上传 0 个顶点实现「隐藏」，而不是保留旧的高亮点。
    const bool valid = m_selectedIndex >= 0 && m_selectedIndex < m_markers.size();
    if (!valid) {
        m_selectedVbo->bind();
        m_selectedVbo->allocate(nullptr, 0);
        m_selectedVbo->release();
        return;
    }

    const MarkerVertex sel = m_markers.at(m_selectedIndex);
    m_selectedVbo->bind();
    m_selectedVbo->allocate(&sel, int(sizeof(MarkerVertex)));
    m_selectedVbo->release();
}

void SceneRenderer::rebuildHeatmap()
{
    if (!m_heatmap)
        return;

    // 没有筛选结果时先放一张全黑纹理，
    // 保证 shader 采样始终有有效纹理绑定，避免黑屏/未定义行为。
    m_heatmap->rebuild(m_defects, m_waferRadius);

    // 尚未应用任何筛选（例如刚启动、数据集仍在解析）：用全部缺陷再叠加一次，
    // 否则用户会看到「已加载 3619 个缺陷点，但晶圆上一个热区都没有」。
    // 注意：m_bufferManager 是可选注入的（单元测试/无 UI 场景可能为空），
    // 这里必须判空 —— 否则第一帧就会解引用空指针。
    if (m_syntheticHeatmap && m_defects.isEmpty() && m_bufferManager) {
        DataBufferManager::DatasetPtr snapshot = m_bufferManager->snapshot();
        if (snapshot && !snapshot->isEmpty())
            m_heatmap->rebuild(snapshot->points, snapshot->waferRadius);
    }
}

void SceneRenderer::setViewportSize(const QSize &size)
{
    m_viewportSize = size;
    if (m_fbo) {
        QString err;
        // FBO 尺寸跟随视口，保证框选区域按 1:1 渲染
        m_fbo->ensureSize(size, &err);
    }
}

void SceneRenderer::setHeatmapEnabled(bool on)
{
    m_heatmapEnabled = on;
}

void SceneRenderer::setMarkersEnabled(bool on)
{
    m_markersEnabled = on;
}

void SceneRenderer::setWaferSurfaceVisible(bool on)
{
    m_waferVisible = on;
}

float SceneRenderer::markerBasePixelSize() const
{
    // 与 constants::markerPixelSize() 完全同构：
    //   实际直径 = uPixelScale * (0.65 + 0.9i) * (0.7 + 14r) * persp
    // 因此 uPixelScale = 基础像素尺寸 × 视口高度缩放 × DPR（可选再乘选中放大）。
    const float heightScale = std::fmin(
        1.0f, std::fmax(0.35f, static_cast<float>(m_viewportSize.height())
                                 / constants::kMarkerReferenceViewportHeight));
    return constants::kMarkerBasePixelSize * heightScale * m_devicePixelRatio;
}

void SceneRenderer::renderFrame(SceneStateManager *state)
{
    if (!m_initialized || !state)
        return;

    QElapsedTimer cpuTimer;
    cpuTimer.start();

    const QMatrix4x4 view = state->viewMatrix();
    const QMatrix4x4 proj = state->projectionMatrix(m_viewportSize.width(),
                                                    m_viewportSize.height());

    // 1) 清除缓冲与固定渲染状态
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 启用面剔除，减少不必要片元计算（扩展优化：面剔除）
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // 点渲染需要开启程序点大小控制
    glEnable(GL_PROGRAM_POINT_SIZE);

    beginGpuTimer();

    renderSceneContent(view, proj, state);

    endGpuTimer();

    // 5) 每帧上报性能统计
    PerformanceStats::FrameSample sample;
    sample.cpuFrameMs = static_cast<double>(cpuTimer.nsecsElapsed()) / 1.0e6;
    sample.gpuTimeMs = m_gpuTimer.lastElapsedMs;
    sample.triangles = m_waferVisible ? m_waferIndexCount / 3 : 0;
    sample.points = m_markersEnabled ? m_markerCount : 0;
    sample.lines = m_gridVertexCount + m_axisVertexCount + m_outlineVertexCount;
    sample.drawCalls = (m_waferVisible ? 1 : 0) + (m_markersEnabled ? 1 : 0) + 3;
    sample.defectCount = m_defects.size();
    m_perfStats->submitFrame(sample);

    glDisable(GL_CULL_FACE);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void SceneRenderer::renderSceneContent(const QMatrix4x4 &view, const QMatrix4x4 &proj,
                                       SceneStateManager *state)
{
    // 绘制顺序是画面观感的组成部分，屏幕渲染与 FBO 离屏重绘共用本函数，
    // 保证「导出的图」与「看到的图」在同一个状态下完全一致。
    glDisable(GL_CULL_FACE);
    drawGrid(view, proj);
    glEnable(GL_CULL_FACE);

    if (m_waferVisible)
        drawWafer(view, proj, state);

    if (m_markersEnabled)
        drawMarkers(view, proj, state);
}

void SceneRenderer::drawGrid(const QMatrix4x4 &view, const QMatrix4x4 &proj)
{
    if (!m_lineVao || m_lineVbo == nullptr)
        return;

    QOpenGLShaderProgram *prog = m_shaders->program(ShaderManager::Program::Line);
    if (!prog)
        return;

    prog->bind();
    prog->setUniformValue("uModel", QMatrix4x4());
    prog->setUniformValue("uView", view);
    prog->setUniformValue("uProjection", proj);

    m_lineVao->bind();

    int offset = 0;
    const int counts[3] = {m_gridVertexCount, m_axisVertexCount, m_outlineVertexCount};
    const float alphas[3] = {0.55f, 0.95f, 0.95f};

    for (int i = 0; i < 3; ++i) {
        if (counts[i] <= 0)
            continue;
        prog->setUniformValue("uAlpha", alphas[i]);
        glLineWidth(i == 0 ? 1.0f : 2.0f);
        glDrawArrays(GL_LINES, offset, counts[i]);
        offset += counts[i];
    }

    glLineWidth(1.0f);
    m_lineVao->release();
    prog->release();
}

void SceneRenderer::drawWafer(const QMatrix4x4 &view, const QMatrix4x4 &proj,
                              SceneStateManager *state)
{
    if (!m_waferVao || m_waferIndexCount <= 0 || m_waferEbo == nullptr)
        return;

    QOpenGLShaderProgram *prog = m_shaders->program(ShaderManager::Program::Wafer);
    if (!prog)
        return;

    prog->bind();

    QMatrix4x4 model;
    // 晶圆几何按单位半径生成，这里缩放到实际半径（半径已归一化到 1.0 世界单位，
    // 因此 model 保持单位阵；若需要展示真实 mm 尺寸可直接乘 m_waferRadius）。
    model.setToIdentity();

    prog->setUniformValue("uModel", model);
    prog->setUniformValue("uView", view);
    prog->setUniformValue("uProjection", proj);
    prog->setUniformValue("uNormalMatrix", model.normalMatrix());
    prog->setUniformValue("uHeatmapEnabled", m_heatmapEnabled);
    prog->setUniformValue("uWireframe", false);
    prog->setUniformValue("uHeatmapGain", kHeatmapShaderGain);
    prog->setUniformValue("uBaseColor", QVector3D(0.62f, 0.66f, 0.72f));
    prog->setUniformValue("uLightDir", QVector3D(-0.42f, -0.78f, -0.46f));
    prog->setUniformValue("uCameraPos", state->cameraPosition());
    prog->setUniformValue("uAlpha", 1.0f);

    // 绑定热力纹理到纹理单元 0
    prog->setUniformValue("uHeatmap", 0);
    if (m_heatmap) {
        m_heatmap->bind(0);
    }

    m_waferVao->bind();
    glDrawElements(GL_TRIANGLES, m_waferIndexCount, GL_UNSIGNED_INT, nullptr);
    m_waferVao->release();

    prog->release();
}

void SceneRenderer::drawMarkers(const QMatrix4x4 &view, const QMatrix4x4 &proj,
                                SceneStateManager *state)
{
    QOpenGLShaderProgram *prog = m_shaders->program(ShaderManager::Program::Marker);
    if (!prog)
        return;

    prog->bind();
    prog->setUniformValue("uModel", QMatrix4x4());
    prog->setUniformValue("uView", view);
    prog->setUniformValue("uProjection", proj);
    prog->setUniformValue("uSelectedIndex", static_cast<float>(m_selectedIndex));
    // 透视衰减的归一化基准：世界坐标已归一化，必须传相机距离本身，
    // 否则着色器里的常数会与场景尺度失配（点会全部被钳到最大尺寸）。
    prog->setUniformValue("uCamDistance", state ? state->distance() : 1.0f);

    const bool hasSelection = m_selectedIndex >= 0 && m_selectedIndex < m_markers.size();

    // 5) 全部缺陷点：单次 DrawCall 批量绘制（难点2）。
    //    gl_PointSize 与 RayPicker 的像素半径共用 constants::markerPixelSize()，
    //    因此这里只需要传入「基础像素尺寸 × DPR」，面积/等级项由顶点属性驱动。
    if (m_markerVao && m_markerCount > 0) {
        prog->setUniformValue("uPixelScale", markerBasePixelSize());
        m_markerVao->bind();
        glDrawArrays(GL_POINTS, 0, m_markerCount);
        m_markerVao->release();
    }

    // 6) 选中缺陷高亮：复用同一个 VBO，只改变「顶点起始偏移」与像素尺寸，
    //    不需要额外的选中 VBO / VAO（旧的 index 与 gl_VertexID 对不上会导致高亮错位）。
    if (hasSelection) {
        prog->setUniformValue("uPixelScale", markerBasePixelSize() * constants::kSelectedMarkerScale);
        drawSelectedMarker(prog, m_selectedIndex);
    }

    prog->release();
}

void SceneRenderer::drawSelectedMarker(QOpenGLShaderProgram *prog, int index)
{
    if (!prog || !m_markerVao || index < 0 || index >= m_markerCount)
        return;

    m_markerVao->bind();
    glDrawArrays(GL_POINTS, index, 1);
    m_markerVao->release();
}

void SceneRenderer::beginGpuTimer()
{
    if (!m_gpuTimer.available)
        return;

    // 双查询对象轮转：本帧开始计时前，先读取上上帧的结果（此时必已完成，
    // 不会造成管线 stall），再重新开始当前帧计时。
    const int idx = m_gpuTimer.writeIndex;
    if (m_gpuTimer.inFlight[idx]) {
        GLuint done = 0;
        glGetQueryObjectuiv(m_gpuTimer.queryIds[idx], GL_QUERY_RESULT_AVAILABLE, &done);
        if (done != 0) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(m_gpuTimer.queryIds[idx], GL_QUERY_RESULT, &ns);
            m_gpuTimer.lastElapsedMs = static_cast<double>(ns) / 1.0e6;
            m_gpuTimer.inFlight[idx] = false;
        }
    }

    if (!m_gpuTimer.inFlight[idx]) {
        glBeginQuery(GL_TIME_ELAPSED, m_gpuTimer.queryIds[idx]);
        m_gpuTimer.inFlight[idx] = true;
    }
}

void SceneRenderer::endGpuTimer()
{
    if (!m_gpuTimer.available)
        return;

    const int idx = m_gpuTimer.writeIndex;
    if (m_gpuTimer.inFlight[idx])
        glEndQuery(GL_TIME_ELAPSED);

    // 切换到另一个查询对象，避免同一对象连续使用造成等待
    m_gpuTimer.writeIndex = (m_gpuTimer.writeIndex + 1) % 2;
}

bool SceneRenderer::captureRegionToFile(const QRect &region, const QSize &viewportSize,
                                        const QString &outFile, SceneStateManager *state,
                                        QString *errorMessage, const QString &caption, float dpr)
{
    if (!m_initialized || !m_fbo || !state) {
        if (errorMessage)
            *errorMessage = QStringLiteral("渲染器未初始化，无法截图");
        return false;
    }

    // 1) 计算有效区域（裁剪到视口内），无效则整体截取
    QRect area = region.intersected(QRect(QPoint(0, 0), viewportSize));
    if (area.width() < 4 || area.height() < 4)
        area = QRect(QPoint(0, 0), viewportSize);

    const float scale = dpr > 0.0f ? dpr : 1.0f;
    const QSize targetSize = viewportSize * scale;

    // 2) 切换到 FBO 作为渲染目标，用与屏幕**完全相同**的相机矩阵与宽高比重绘一次。
    //    这一步保证离屏管线可用，也让导出图不依赖窗口是否被遮挡。
    QString err;
    if (!m_fbo->ensureSize(targetSize, &err)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("FBO 创建失败：%1").arg(err);
        return false;
    }

    const QMatrix4x4 view = state->viewMatrix();
    const QMatrix4x4 proj = state->projectionMatrix(viewportSize.width(), viewportSize.height());

    const float savedDpr = m_devicePixelRatio;
    m_devicePixelRatio = scale; // 保证离屏帧里的点尺寸与屏幕一致

    m_fbo->beginOffscreen();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderSceneContent(view, proj, state);

    const QImage full = m_fbo->readBackOffscreen();
    m_fbo->endOffscreen();
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_CULL_FACE);
    m_devicePixelRatio = savedDpr;
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    if (full.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("FBO 像素读回失败");
        return false;
    }

    // 3) 从「整幅离屏图」里裁出框选区域：投影未变，因此裁剪结果就是屏幕上看到的画面。
    const QRect physical(area.x() * scale, area.y() * scale, area.width() * scale,
                         area.height() * scale);
    const QImage regionImage = full.copy(physical.intersected(full.rect()));
    if (regionImage.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("区域裁剪失败");
        return false;
    }

    // 4) 叠加标注（屏幕上的框选矩形/尺寸提示是 QPainter 覆盖层，不会进入帧缓冲）
    return FboRenderer::saveImage(annotateCapture(regionImage, caption, scale), outFile,
                                  errorMessage);
}

QImage SceneRenderer::annotateCapture(QImage image, const QString &caption, float dpr)
{
    if (image.isNull())
        return image;

    // 统一转成 ARGB32_Premultiplied，QPainter 才能获得最佳绘制路径
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const float s = dpr > 0.0f ? dpr : 1.0f;
    const QRect full(0, 0, image.width(), image.height());

    // 外侧 1px 强调边框：让导出图的边界一目了然
    QPen border(kCaptureAccent);
    border.setWidthF(std::max(1.0f, 1.5f * s));
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(full).adjusted(0.5 * s, 0.5 * s, -0.5 * s, -0.5 * s));

    QFont font = painter.font();
    font.setPointSizeF(9.0f * s);
    font.setBold(true);
    painter.setFont(font);

    // 左下角：导出尺寸标签（与框选时的提示一致）
    if (full.width() > 120 * s && full.height() > 40 * s) {
        const QString sizeText =
            QStringLiteral("%1 x %2 px").arg(full.width()).arg(full.height());
        const QFontMetrics metrics(font);
        const QRect textRect = metrics.boundingRect(sizeText).adjusted(-6 * s, -3 * s, 6 * s, 3 * s);
        const QRectF chip(full.left() + 8 * s, full.bottom() - textRect.height() - 8 * s,
                          textRect.width(), textRect.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(9, 13, 19, 180));
        painter.drawRoundedRect(chip, 3 * s, 3 * s);
        painter.setPen(kCaptureAccent);
        painter.drawText(chip, Qt::AlignCenter, sizeText);
    }

    // 顶部标题（数据集名 + 时间）：只有在图片足够宽时才画，避免遮挡缺陷点
    if (!caption.isEmpty() && full.width() > 240 * s) {
        font.setPointSizeF(10.0f * s);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QRect capRect = metrics.boundingRect(caption);
        const QRectF band(full.left() + 8 * s, full.top() + 8 * s,
                          std::min<qreal>(capRect.width() + 12 * s, full.width() - 16 * s),
                          capRect.height() + 6 * s);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(9, 13, 19, 170));
        painter.drawRoundedRect(band, 3 * s, 3 * s);
        painter.setPen(QColor(214, 232, 245));
        painter.drawText(band, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(caption, Qt::ElideRight, int(band.width() - 12 * s)));
    }

    painter.end();
    return image;
}

void SceneRenderer::shutdown()
{
    if (!m_initialized && !m_glInitialized)
        return;

    // 释放顺序：先释放子模块（纹理/FBO/查询相对独立），
    // 再交给 GpuResourceManager 逆序释放全部 VAO/VBO/纹理/FBO。
    if (m_gpuTimer.available) {
        glDeleteQueries(2, m_gpuTimer.queryIds);
        m_gpuTimer.queryIds[0] = m_gpuTimer.queryIds[1] = 0;
        m_gpuTimer.available = false;
    }

    if (m_fbo)
        m_fbo->destroy();

    if (m_heatmap)
        m_heatmap->destroy();

    if (m_shaders)
        m_shaders->destroy();

    if (m_resources)
        m_resources->destroyAll(true);

    // 注意：VAO 与 VBO/EBO 类型不同，不能写成一条链式赋值
    // （a = b = c 右结合，会把 QOpenGLBuffer* 赋给 QOpenGLVertexArrayObject*）
    m_waferVao = nullptr;
    m_waferVbo = nullptr;
    m_waferEbo = nullptr;

    m_markerVao = nullptr;
    m_markerVbo = nullptr;

    m_selectedVao = nullptr;
    m_selectedVbo = nullptr;

    m_lineVao = nullptr;
    m_lineVbo = nullptr;

    m_initialized = false;
}

QString SceneRenderer::gpuStatisticsText() const
{
    return m_resources ? m_resources->statisticsText() : QStringLiteral("GPU 资源未初始化");
}

double SceneRenderer::lastHeatmapBuildMs() const
{
    return m_heatmap ? m_heatmap->lastBuildMs() : 0.0;
}

QSize SceneRenderer::offscreenSize() const
{
    return m_fbo ? m_fbo->size() : QSize();
}

} // namespace wafer
