#ifndef HEATMAPRENDERER_H
#define HEATMAPRENDERER_H

#include "core/DefectTypes.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QSize>

class QOpenGLTexture;

namespace wafer {

class GpuResourceManager;

/**
 * @brief 缺陷热力图子模块（渲染层，难点4：热力图颜色映射）。
 *
 * 流程：
 *  1. CPU 侧以缺陷数据(X/Y/等级)在离屏灰度缓冲中做"喷溅"累积，
 *     生成一张 R8 灰度纹理 —— 灰度值代表该区域缺陷严重程度；
 *  2. 上传到 GPU（单通道，带宽仅为 RGBA 的 1/4）；
 *  3. 片元着色器采样该纹理并做伪彩色映射（红-黄-绿），GPU 并行完成着色。
 *
 * 这样颜色规则改动只改 shader，无需重新生成纹理。
 */
class HeatmapRenderer : protected QOpenGLFunctions_3_3_Core
{
public:
    HeatmapRenderer();
    ~HeatmapRenderer();

    HeatmapRenderer(const HeatmapRenderer &) = delete;
    HeatmapRenderer &operator=(const HeatmapRenderer &) = delete;

    /**
     * @brief 初始化，创建 GL 函数指针与灰度 CPU 缓冲。
     * @param resolution 纹理分辨率（正方形，建议 512~2048）
     */
    bool initialize(GpuResourceManager *resourceManager, const QSize &resolution,
                    QString *errorLog = nullptr);

    /**
     * @brief 重建灰度纹理（流程3）。
     * @param defects 当前筛选后的缺陷点
     * @param radius  晶圆半径，用于 XY 归一化到纹理坐标
     * @return 是否成功（未初始化或无数据返回 false）
     */
    bool rebuild(const QVector<DefectPoint> &defects, float radius);

    /** 绑定纹理到纹理单元；返回是否绑定成功 */
    bool bind(int textureUnit) const;

    /** 释放纹理（在上下文有效时调用） */
    void destroy();

    bool isValid() const { return m_texture != nullptr; }
    QSize resolution() const { return m_resolution; }

    /** 最近一次重建耗时（毫秒），用于性能面板展示 CPU 侧开销 */
    double lastBuildMs() const { return m_lastBuildMs; }

    /** 增益：在 CPU 缓冲上做强度的整体缩放 */
    void setGain(float gain) { m_gain = gain; }

private:
    /**
     * @brief 在灰度累积缓冲上做一次高斯喷溅。
     *
     * 性能要点（10 万点量级）：
     *  - 只遍历 3σ 包围盒内的像素，不触碰缓冲其余区域；
     *  - 用二次核 (1 - d²/r²)² 近似 e^{-d²/2σ²}，避免每个像素一次 std::exp；
     *  - 记录本次写入的矩形，下一次重建只需清零这些矩形（脏矩形）。
     */
    void splat(QVector<float> &buffer, float nrmX, float nrmY, float intensity, float sigma);

    /** 加速核：与高斯核形状接近（中心 1、边缘 0），但只需两次乘加 */
    static float falloffKernel(float d2, float radiusSq);

    GpuResourceManager *m_resources = nullptr; // 不持有所有权
    QOpenGLTexture *m_texture = nullptr;       // 由 GpuResourceManager 拥有
    QSize m_resolution{512, 512};
    QVector<float> m_cpuBuffer;                 // 灰度累积缓冲（float，最后量化到 8bit）
    QVector<unsigned char> m_grayBuffer;        // 量化后的 R8 数据（复用，避免每次重建 new）
    int m_dirtyX0 = 0;                          // 上一轮写过的矩形（用于增量清零）
    int m_dirtyY0 = 0;
    int m_dirtyX1 = -1;
    int m_dirtyY1 = -1;
    float m_gain = 1.0f;
    double m_lastBuildMs = 0.0;
    bool m_initialized = false;
};

} // namespace wafer

#endif // HEATMAPRENDERER_H
