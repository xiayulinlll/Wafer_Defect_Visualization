#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cmath>

namespace wafer {
namespace constants {

// ---------------- 晶圆几何 / 数据尺度 ----------------
/** 数据空间默认晶圆半径 (mm)。当 CSV 未提供半径且无法推导时使用。 */
inline constexpr float kDefaultWaferRadiusMm = 150.0f;
/** 数据空间最小可接受晶圆半径 (mm)，避免除零。 */
inline constexpr float kMinWaferRadiusMm = 10.0f;
/** 世界坐标归一化半径：晶圆半径恒映射为 1.0 世界单位。 */
inline constexpr float kWorldExtent = 1.0f;
/** 缺陷高度 Z 的单位换算：1 um -> 世界单位。 */
inline constexpr float kZUnitScale = 0.004f;
/** 热力图喷溅时用于面积归一化的参考面积 (um^2)。 */
inline constexpr float kAreaNormalizeRef = 60.0f;
/** 晶圆半径自动推导时的安全边距（数据最大半跨度 * 该系数）。 */
inline constexpr float kDerivedRadiusMargin = 1.08f;

// ---------------- 缺陷标记点 ----------------
/** 标记点基础世界半径（与拾取半径保持一致，单点定义）。 */
inline constexpr float kMarkerBaseRadius = 0.008f;
/** 标记点半径随缺陷面积增长的最大增量。 */
inline constexpr float kMarkerAreaRadiusScale = 0.016f;
/** 拾取时叠加的屏幕空间容差（CSS 像素），与 DPI 无关。 */
inline constexpr float kPickTolerancePx = 6.0f;

// ---------------- 缺陷标记点（像素尺度，渲染与拾取共用） ----------------
/** 视口高度不变时，标记点基础直径（CSS 像素）。 */
inline constexpr float kMarkerBasePixelSize = 11.0f;
/** 基准视口高度：低于该高度时点会按比例缩小（避免小窗口里点挤成一片）。 */
inline constexpr float kMarkerReferenceViewportHeight = 760.0f;
/** 选中标记点的放大倍数（相对基础像素尺寸）。 */
inline constexpr float kSelectedMarkerScale = 1.8f;
/**
 * 标记点屏幕半径的下限（CSS 像素）。拾取时额外保证至少命中这么大的区域，
 * 保证「看得见的小点也点得中」。
 */
inline constexpr float kMinPickRadiusPx = 4.0f;

/**
 * @brief 单个缺陷标记点的世界半径（渲染与拾取共用，避免公式重复）。
 * @param area 缺陷面积 (um^2)
 */
inline float markerRadiusFor(float area)
{
    const float areaNorm = std::fmin(1.0f, std::sqrt(std::fmax(0.0f, area)) / kAreaNormalizeRef);
    return kMarkerBaseRadius + kMarkerAreaRadiusScale * areaNorm;
}

// ---------------- 渲染常量 ----------------
/** 晶圆几何分段数（晶圆主体 + 轮廓线共用）。 */
inline constexpr int kWaferSegments = 96;
/** 晶圆圆盘厚度（世界单位）。 */
inline constexpr float kWaferThickness = 0.018f;
/** 地面参考网格半边长（世界单位）。 */
inline constexpr float kGridHalfExtent = 1.35f;
/** 地面参考网格分段数。 */
inline constexpr int kGridDivisions = 24;
/** 世界坐标轴长度（世界单位）。 */
inline constexpr float kAxisLength = 0.9f;
/** 晶圆轮廓线颜色（与热力图蓝色强调色一致）。 */
inline constexpr float kOutlineColorR = 0.35f;
inline constexpr float kOutlineColorG = 0.80f;
inline constexpr float kOutlineColorB = 1.00f;

// ---------------- 热力图 ----------------
/** 热力图纹理分辨率（正方形）。 */
inline constexpr int kHeatmapResolution = 1024;
/** 热力 splat 的基础 sigma（像素 @512 分辨率）。 */
inline constexpr float kHeatmapBaseSigmaPx = 3.0f;
/** 热力 splat 随缺陷面积增长的最大 sigma 增量（像素 @512 分辨率）。 */
inline constexpr float kHeatmapAreaSigmaPx = 12.0f;
/** 热力纹理量化时的软饱和系数（1 - exp(-v * k)）。 */
inline constexpr float kHeatmapSaturation = 0.85f;
/** 片元着色器里热力纹理的整体增益。 */
inline constexpr float kHeatmapShaderGain = 1.35f;

// ---------------- 统计分析 ----------------
/** 默认半径分区数（同心环，由内到外等宽）。 */
inline constexpr int kDefaultZoneCount = 4;
/** 判定为「严重缺陷」的等级下限（>= 该等级视为可能致死）。 */
inline constexpr int kCriticalSeverity = 4;

// ---------------- FBO / 截图 ----------------
/** 离屏导出图像的长边上限（像素），防止超大窗口导出时显存爆掉。 */
inline constexpr int kMaxExportDimension = 4096;

// ---------------- 相机 ----------------
/** 默认相机距离（以晶圆半径为单位）。 */
inline constexpr float kDefaultCameraDistance = 3.2f;
/** 相机俯仰角限制，避免万向节翻转。 */
inline constexpr float kMaxPitchDeg = 89.0f;
/** 默认垂直视场角（度）。 */
inline constexpr float kDefaultFovDeg = 45.0f;
/** 垂直视场角的正切值（渲染与拾取共用，保证屏幕尺度换算一致）。 */
inline constexpr float kFovTanHalf = 0.4142136f; // tan(45° / 2)

// ---------------- 缺陷标记点（像素换算：渲染与拾取的唯一真值来源） ----------------

/**
 * @brief 标记点在屏幕上的直径（CSS 像素，未含透视衰减）。
 *
 * marker.vert 用该尺寸设置 gl_PointSize，RayPicker 用它反推世界半径，
 * 二者共用同一公式，避免出现「看得见却点不中」。
 *
 * @param area    缺陷面积 (um^2)
 * @param severity 缺陷等级（1~5）
 * @param viewportHeight 视口高度（CSS 像素），用于小窗口下等比缩小
 * @param selected 是否为选中高亮标记（放大 kSelectedMarkerScale 倍）
 */
inline float markerPixelSize(float area, int severity, float viewportHeight, bool selected = false)
{
    const float areaNorm = std::fmin(1.0f, std::sqrt(std::fmax(0.0f, area)) / kAreaNormalizeRef);
    const float intensity = std::fmin(1.0f, std::fmax(0.0f, static_cast<float>(severity) / 5.0f));
    const float heightScale =
        std::fmin(1.0f, std::fmax(0.35f, viewportHeight / kMarkerReferenceViewportHeight));
    const float size = kMarkerBasePixelSize * (0.65f + 0.9f * intensity)
                       * (0.7f + 14.0f * areaNorm);
    return size * heightScale * (selected ? kSelectedMarkerScale : 1.0f);
}

    /**
     * @brief 把屏幕像素尺寸换算成「该深度处的世界半径」。
     *
     * 透视投影下，世界半径 r 在视深 z 处投影出的屏幕直径约为
     *   r * projScale * viewportHeight / (2z)   个像素，
     * 因此反解得到 r = pixelSize * z / (projScale * viewportHeight) * 2 ... 见实现。
     *
     * @param pixelSize       屏幕直径（CSS 像素）
     * @param viewDepth       命中点沿视线方向的深度（视空间 -z，必须 > 0）
     * @param projScale       投影矩阵 (1,1)：2 / tan(fovY / 2)
     * @param viewportHeight  视口高度（CSS 像素）
     */
    inline float pixelSizeToWorldRadius(float pixelSize, float viewDepth, float projScale,
                                        float viewportHeight)
    {
        if (viewDepth <= 1e-4f || projScale <= 1e-4f || viewportHeight <= 1.0f)
            return 0.0f;
        // ndcPerWorld = projScale / z；pixelsPerWorld = ndcPerWorld * viewportHeight / 2
        const float pixelsPerWorld = projScale * viewportHeight / (2.0f * viewDepth);
        return 0.5f * pixelSize / pixelsPerWorld;
    }

} // namespace constants
} // namespace wafer

#endif // CONSTANTS_H
