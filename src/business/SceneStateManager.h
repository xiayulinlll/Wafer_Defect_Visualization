#ifndef SCENESTATEMANAGER_H
#define SCENESTATEMANAGER_H

#include "core/Constants.h"
#include "core/MathUtils.h"

#include <QMatrix4x4>
#include <QObject>
#include <QVector3D>

namespace wafer {

/**
 * 世界坐标采用"归一化单位"：晶圆半径恒为 1.0，缺陷 XY 映射到 [-1, 1]。
 * 默认相机距离取晶圆半径的 3.2 倍，可在 45° FOV 下完整框住晶圆并留出边距。
 * （数值定义已收敛到 core/Constants.h，此处保留别名以兼容既有调用。）
 */
constexpr float kDefaultDistance = constants::kDefaultCameraDistance;

class SceneStateManager : public QObject
{
    Q_OBJECT

public:
    explicit SceneStateManager(QObject *parent = nullptr);

    // ---------- 相机（球坐标：偏航/俯仰/距离围绕焦点） ----------
    void orbitBy(float deltaYawDeg, float deltaPitchDeg);
    void zoomBy(float factor);            // factor < 1 拉近
    void panByScreenDelta(float dxPixels, float dyPixels);

    /**
     * @brief 设置视口高度（CSS 像素），用于把像素位移换算成世界位移。
     * 不设置时使用 800 的默认值，保证平移手感与窗口高度一致（1:1 跟手）。
     */
    void setViewportHeight(int cssHeight);
    int viewportHeight() const { return m_viewportHeightPx; }
    void resetCamera(float waferRadius);

    /**
     * @brief 直接恢复到指定相机姿态（用于从配置恢复上次视角）。
     * 参数会被夹到合法区间（俯仰 ±kMaxPitchDeg、距离 [min,max]）。
     * @return 参数是否可用（非有限值会被拒绝）
     */
    bool restoreCamera(float yawDeg, float pitchDeg, float distance, const QVector3D &target);

    float yawDeg() const { return m_yawDeg; }
    float pitchDeg() const { return m_pitchDeg; }
    float distance() const { return m_distance; }
    QVector3D target() const { return m_target; }
    QVector3D cameraPosition() const;

    /** 视图矩阵 */
    QMatrix4x4 viewMatrix() const;
    /** 透视投影矩阵 */
    QMatrix4x4 projectionMatrix(int viewportW, int viewportH) const;

    // ---------- 渲染开关 ----------
    bool heatmapEnabled() const { return m_heatmapEnabled; }
    bool markersEnabled() const { return m_markersEnabled; }
    bool waferSurfaceVisible() const { return m_waferSurfaceVisible; }
    /** 缺陷点的世界空间 Z 放大系数（Z 单位 um -> 可视高度） */
    float zScale() const { return m_zScale; }

    // ---------- 选中状态 ----------
    int selectedDefectId() const { return m_selectedDefectId; }

public slots:
    void setHeatmapEnabled(bool on);
    void setMarkersEnabled(bool on);
    void setWaferSurfaceVisible(bool on);
    void setZScale(float scale);
    void setSelectedDefectId(int id);
    // 注意：参数名不要用 near/far —— Windows SDK 的 <windows.h> 把它们定义为宏
    void setNearPlane(float nearPlane) { m_nearPlane = nearPlane; }
    void setFarPlane(float farPlane) { m_farPlane = farPlane; }
    /** 请求恢复默认视角 */
    void requestResetCamera();

signals:
    void cameraChanged();
    void heatmapEnabledChanged(bool on);
    void markersEnabledChanged(bool on);
    void waferSurfaceVisibleChanged(bool on);
    void selectedDefectIdChanged(int id);
    void zScaleChanged(float scale);
    void resetCameraRequested();

private:
    // 相机参数
    // 注意：世界坐标采用"归一化单位"—— 晶圆半径恒为 1.0，
    // 缺陷 XY 由 GeometryBuilder 按 kWorldExtent/radius 映射到 [-1,1]。
    // 因此相机距离也必须是归一化量级（约 3 个世界单位），
    // 早期误用 mm 量级的 420 会导致晶圆在屏幕上只有几个像素（表现为"全黑+中间一个小点"）。
    float m_yawDeg = -35.0f;
    float m_pitchDeg = 35.0f;
    float m_distance = kDefaultDistance;
    QVector3D m_target{0.0f, 0.0f, 0.0f};
    float m_minDistance = 0.15f;
    float m_maxDistance = 40.0f;

    // 裁剪面（与归一化单位匹配；过大的 near/far 会严重损失深度精度）
    float m_fovDeg = constants::kDefaultFovDeg;
    float m_nearPlane = 0.02f;
    float m_farPlane = 16.0f;

    // 视口高度（CSS 像素）：平移时把像素位移换算成世界位移，实现 1:1 跟手
    int m_viewportHeightPx = 800;

    // 状态
    bool m_heatmapEnabled = true;
    bool m_markersEnabled = true;
    bool m_waferSurfaceVisible = true;
    float m_zScale = 1.0f;
    int m_selectedDefectId = -1;
};

} // namespace wafer

#endif // SCENESTATEMANAGER_H
