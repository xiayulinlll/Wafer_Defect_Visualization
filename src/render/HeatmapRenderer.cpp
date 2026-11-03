#include "render/HeatmapRenderer.h"
#include "core/Constants.h"
#include "render/GpuResourceManager.h"

#include <QElapsedTimer>
#include <QOpenGLTexture>

#include <algorithm>
#include <cmath>

namespace wafer {

namespace {
/** 热力图纹理资源名（GpuResourceManager 中登记使用） */
const char *const kHeatmapTextureName = "DefectHeatmapTexture";
const char *const kHeatmapTextureLegacy = "DefectHeatmapTexture@legacy";
} // namespace

HeatmapRenderer::HeatmapRenderer() = default;

HeatmapRenderer::~HeatmapRenderer()
{
    // 纹理所有权在 GpuResourceManager，必须在其 destroyAll() 时释放，
    // 这里只置空指针，避免上下文失效后重复删除。
    m_texture = nullptr;
}

bool HeatmapRenderer::initialize(GpuResourceManager *resourceManager, const QSize &resolution,
                                 QString *errorLog)
{
    if (!resourceManager) {
        if (errorLog)
            *errorLog += QStringLiteral("[HeatmapRenderer] resourceManager 为空\n");
        return false;
    }

    if (!initializeOpenGLFunctions()) {
        if (errorLog)
            *errorLog += QStringLiteral("[HeatmapRenderer] OpenGL 3.3 函数指针初始化失败\n");
        return false;
    }

    m_resources = resourceManager;
    m_resolution = QSize(std::max(64, resolution.width()), std::max(64, resolution.height()));
    // QVector(QList) 没有 assign()，用 fill() 填充
    m_cpuBuffer.fill(0.0f, m_resolution.width() * m_resolution.height());
    m_grayBuffer.fill(0, m_resolution.width() * m_resolution.height());
    m_dirtyX1 = -1;
    m_dirtyY1 = -1;
    m_initialized = true;
    return true;
}

float HeatmapRenderer::falloffKernel(float d2, float radiusSq)
{
    const float t = 1.0f - d2 / radiusSq; // 归一化半径 [0,1]
    return t * t;                         // 平滑衰减，边缘为 0，避免硬边
}

void HeatmapRenderer::splat(QVector<float> &buffer, float nrmX, float nrmY, float intensity,
                            float sigma)
{
    // nrmX/nrmY 为 [0,1] 归一化纹理坐标
    const int w = m_resolution.width();
    const int h = m_resolution.height();

    const float cx = nrmX * static_cast<float>(w - 1);
    const float cy = nrmY * static_cast<float>(h - 1);

    // 3 sigma 半径
    const float radiusPx = sigma * 3.0f;
    const float radiusSq = radiusPx * radiusPx;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radiusPx)));
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + radiusPx)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radiusPx)));
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + radiusPx)));
    if (x0 > x1 || y0 > y1)
        return;

    // 记录脏矩形，下一次重建只需清零这一块（而不是整个 4 MB 缓冲）
    if (m_dirtyX1 < 0) {
        m_dirtyX0 = x0;
        m_dirtyY0 = y0;
        m_dirtyX1 = x1;
        m_dirtyY1 = y1;
    } else {
        m_dirtyX0 = std::min(m_dirtyX0, x0);
        m_dirtyY0 = std::min(m_dirtyY0, y0);
        m_dirtyX1 = std::max(m_dirtyX1, x1);
        m_dirtyY1 = std::max(m_dirtyY1, y1);
    }

    for (int y = y0; y <= y1; ++y) {
        const float dy = static_cast<float>(y) - cy;
        const float dy2 = dy * dy;
        float *row = buffer.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        for (int x = x0; x <= x1; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float d2 = dx * dx + dy2;
            if (d2 > radiusSq)
                continue;

            // 平滑衰减核：中心最强、边缘平滑归零，避免出现硬边"马赛克"
            row[x] += intensity * falloffKernel(d2, radiusSq);
        }
    }
}

bool HeatmapRenderer::rebuild(const QVector<DefectPoint> &defects, float radius)
{
    if (!m_initialized || !m_resources)
        return false;

    QElapsedTimer timer;
    timer.start();

    const int w = m_resolution.width();
    const int h = m_resolution.height();

    // 1) 只清零「上一轮写过」的脏矩形：相机/筛选不变时重建几乎零成本。
    if (m_dirtyX1 >= 0 && m_dirtyY1 >= 0) {
        for (int y = m_dirtyY0; y <= m_dirtyY1; ++y) {
            float *row = m_cpuBuffer.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            std::fill(row + m_dirtyX0, row + m_dirtyX1 + 1, 0.0f);
        }
    }
    m_dirtyX1 = m_dirtyY1 = -1;

    const float r = radius > 0.0f ? radius : constants::kDefaultWaferRadiusMm;

    // 2) 每个缺陷做一次喷溅，累积"严重程度"。
    //    sigma 与缺陷面积相关：大缺陷影响范围更广。
    for (const DefectPoint &d : defects) {
        const float nrmX = 0.5f + 0.5f * (d.x / r);
        const float nrmY = 0.5f - 0.5f * (d.y / r); // 纹理 Y 向下

        const float areaNorm =
            std::min(1.0f, std::sqrt(std::max(0.0f, d.area)) / constants::kAreaNormalizeRef);
        const float sigmaPx =
            (constants::kHeatmapBaseSigmaPx + constants::kHeatmapAreaSigmaPx * areaNorm)
            * (static_cast<float>(w) / 512.0f);
        const float intensity = static_cast<float>(static_cast<int>(d.severity)) / 5.0f;

        // 不再丢弃「圆心在 [0,1] 之外」的点：贴边缺陷的喷溅本来就会被 splat() 裁剪到
        // 纹理内，提前丢弃只会让晶圆边缘的热区被硬生生切掉一截。
        splat(m_cpuBuffer, nrmX, nrmY, intensity, sigmaPx);
    }

    // 3) 量化到 8bit 灰度（R8）。使用软饱和 1 - exp(-v) 压高光，
    //    让热点区域不会因为叠加而全部饱和成同一片白。
    //    m_grayBuffer 复用同一块内存，避免每次重建都申请/释放 1 MB。
    if (m_grayBuffer.size() != m_cpuBuffer.size())
        m_grayBuffer.resize(m_cpuBuffer.size());
    for (int i = 0; i < m_cpuBuffer.size(); ++i) {
        const float v = m_cpuBuffer[static_cast<size_t>(i)] * m_gain;
        const float s = 1.0f - std::exp(-v * constants::kHeatmapSaturation);
        m_grayBuffer[i] = static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, s * 255.0f)));
    }

    // 4) 上传 GPU。纹理只在首次创建；后续通过 setData() 原地更新，
    //    避免「每次筛选都销毁并重建纹理」造成的资源抖动（难点5）。
    QOpenGLTexture *texture = m_resources->findTexture(QString::fromLatin1(kHeatmapTextureName));
    if (texture) {
        texture->setData(static_cast<QOpenGLTexture::PixelFormat>(QOpenGLTexture::Red),
                         static_cast<QOpenGLTexture::PixelType>(QOpenGLTexture::UInt8),
                         m_grayBuffer.constData());
    } else {
        texture = m_resources->createTexture2D(
            QString::fromLatin1(kHeatmapTextureName), w, h,
            static_cast<unsigned int>(QOpenGLTexture::R8_UNorm),
            static_cast<unsigned int>(QOpenGLTexture::Red),
            static_cast<unsigned int>(QOpenGLTexture::UInt8), m_grayBuffer.constData());
    }
    m_texture = texture;

    m_lastBuildMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    return m_texture != nullptr;
}

bool HeatmapRenderer::bind(int textureUnit) const
{
    if (!m_texture)
        return false;
    m_texture->bind(static_cast<uint>(textureUnit));
    return true;
}

void HeatmapRenderer::destroy()
{
    if (m_resources) {
        m_resources->destroyTexture(QString::fromLatin1(kHeatmapTextureName));
        m_resources->destroyTexture(QString::fromLatin1(kHeatmapTextureLegacy));
    }
    m_texture = nullptr;
}

} // namespace wafer
