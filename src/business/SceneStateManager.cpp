#include "business/SceneStateManager.h"

#include <algorithm>
#include <cmath>

using wafer::math::clampf;
using wafer::math::degToRad;
namespace wafer {

SceneStateManager::SceneStateManager(QObject *parent)
    : QObject(parent)
{
}

QVector3D SceneStateManager::cameraPosition() const
{
    // 球坐标 -> 笛卡尔。pitch 限制在 (-89, 89) 避免万向节翻转。
    const float yaw = degToRad(m_yawDeg);
    const float pitch = degToRad(clampf(m_pitchDeg, -constants::kMaxPitchDeg,
                                        constants::kMaxPitchDeg));

    const float cp = std::cos(pitch);
    const QVector3D dir(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
    return m_target + dir * m_distance;
}

QMatrix4x4 SceneStateManager::viewMatrix() const
{
    QMatrix4x4 view;
    view.lookAt(cameraPosition(), m_target, QVector3D(0.0f, 1.0f, 0.0f));
    return view;
}

QMatrix4x4 SceneStateManager::projectionMatrix(int viewportW, int viewportH) const
{
    const float aspect = viewportH > 0 ? static_cast<float>(viewportW) / static_cast<float>(viewportH)
                                       : 1.0f;
    QMatrix4x4 proj;
    proj.perspective(m_fovDeg, aspect, m_nearPlane, m_farPlane);
    return proj;
}

void SceneStateManager::setViewportHeight(int cssHeight)
{
    if (cssHeight > 0)
        m_viewportHeightPx = cssHeight;
}

void SceneStateManager::orbitBy(float deltaYawDeg, float deltaPitchDeg)
{
    if (qFuzzyIsNull(deltaYawDeg) && qFuzzyIsNull(deltaPitchDeg))
        return;

    m_yawDeg = std::fmod(m_yawDeg + deltaYawDeg, 360.0f);
    m_pitchDeg = clampf(m_pitchDeg + deltaPitchDeg, -constants::kMaxPitchDeg,
                        constants::kMaxPitchDeg);
    emit cameraChanged();
}

void SceneStateManager::zoomBy(float factor)
{
    if (factor <= 0.0f)
        return;
    const float next = clampf(m_distance * factor, m_minDistance, m_maxDistance);
    if (qFuzzyCompare(next, m_distance))
        return;
    m_distance = next;
    emit cameraChanged();
}

void SceneStateManager::panByScreenDelta(float dxPixels, float dyPixels)
{
    // 真正的 1:1「跟手」平移：焦点平面上一屏幕高度对应的世界高度为
    //   2 * distance * tan(fov/2)
    // 除以视口高度得到「每像素对应的世界单位」，与 fov/窗口高度无关。
    // 旧实现用固定系数 distance * 0.25，窗口高度一变手感就漂移。
    const float worldPerPixel =
        (2.0f * m_distance * constants::kFovTanHalf) / static_cast<float>(m_viewportHeightPx);

    // 取相机右向量与上向量（世界空间）作为平移基
    const QVector3D forward = (m_target - cameraPosition()).normalized();
    const QVector3D worldUp(0.0f, 1.0f, 0.0f);
    QVector3D right = QVector3D::crossProduct(forward, worldUp);
    if (right.lengthSquared() < 1e-6f)
        right = QVector3D(1.0f, 0.0f, 0.0f);
    right.normalize();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    m_target += (-right * dxPixels + up * dyPixels) * worldPerPixel;
    emit cameraChanged();
}

void SceneStateManager::resetCamera(float waferRadius)
{
    // 入参为"数据空间"半径（mm，例如 150），需换算到归一化世界单位：
    // 世界空间中晶圆半径恒为 1.0，故直接使用常量 kDefaultDistance。
    Q_UNUSED(waferRadius);

    m_yawDeg = -35.0f;
    m_pitchDeg = 35.0f;
    m_distance = kDefaultDistance;
    m_target = QVector3D(0.0f, 0.0f, 0.0f);
    // 归一化单位下的裁剪面：near 取世界半径的 2%，far 取相机距离的 5 倍，
    // 既能框住晶圆，又保留足够的深度精度。
    m_nearPlane = 0.02f;
    m_farPlane = m_distance * 5.0f;
    emit cameraChanged();
    emit resetCameraRequested();
}

bool SceneStateManager::restoreCamera(float yawDeg, float pitchDeg, float distance,
                                      const QVector3D &target)
{
    // 非有限值（配置文件被改坏）直接拒绝，保持默认视角
    if (!std::isfinite(yawDeg) || !std::isfinite(pitchDeg) || !std::isfinite(distance))
        return false;
    if (!std::isfinite(target.x()) || !std::isfinite(target.y()) || !std::isfinite(target.z()))
        return false;
    if (distance < m_minDistance || distance > m_maxDistance)
        return false;

    m_yawDeg = std::fmod(yawDeg, 360.0f);
    m_pitchDeg = clampf(pitchDeg, -constants::kMaxPitchDeg, constants::kMaxPitchDeg);
    m_distance = distance;
    m_target = target;
    m_farPlane = m_distance * 5.0f;
    emit cameraChanged();
    return true;
}

void SceneStateManager::setHeatmapEnabled(bool on)
{
    if (m_heatmapEnabled == on)
        return;
    m_heatmapEnabled = on;
    emit heatmapEnabledChanged(on);
}

void SceneStateManager::setMarkersEnabled(bool on)
{
    if (m_markersEnabled == on)
        return;
    m_markersEnabled = on;
    emit markersEnabledChanged(on);
}

void SceneStateManager::setWaferSurfaceVisible(bool on)
{
    if (m_waferSurfaceVisible == on)
        return;
    m_waferSurfaceVisible = on;
    emit waferSurfaceVisibleChanged(on);
}

void SceneStateManager::setZScale(float scale)
{
    const float s = clampf(scale, 0.0f, 20.0f);
    if (qFuzzyCompare(s, m_zScale))
        return;
    m_zScale = s;
    emit zScaleChanged(s);
}

void SceneStateManager::setSelectedDefectId(int id)
{
    if (m_selectedDefectId == id)
        return;
    m_selectedDefectId = id;
    emit selectedDefectIdChanged(id);
}

void SceneStateManager::requestResetCamera()
{
    emit resetCameraRequested();
}

} // namespace wafer
