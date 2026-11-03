#ifndef MATHUTILS_H
#define MATHUTILS_H

#include <QMatrix4x4>
#include <QVector3D>
#include <cmath>

namespace wafer {
namespace math {

/** 内联常量，避免依赖 M_PI 宏（MSVC 下未定义） */
inline constexpr float kPi = 3.14159265358979323846f;

/** 角度转弧度 */
inline float degToRad(float deg) { return deg * kPi / 180.0f; }

/** 数值裁剪 */
inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * @brief 轴对齐包围盒，用于射线拾取与相机自动适配。
 */
struct Aabb {
    QVector3D min;
    QVector3D max;

    Aabb() : min(0, 0, 0), max(0, 0, 0) {}

    void reset()
    {
        min = QVector3D(std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max());
        max = QVector3D(-std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max());
    }

    void expand(const QVector3D &p)
    {
        min.setX(std::min(min.x(), p.x()));
        min.setY(std::min(min.y(), p.y()));
        min.setZ(std::min(min.z(), p.z()));
        max.setX(std::max(max.x(), p.x()));
        max.setY(std::max(max.y(), p.y()));
        max.setZ(std::max(max.z(), p.z()));
    }

    void expand(const Aabb &other)
    {
        expand(other.min);
        expand(other.max);
    }

    QVector3D center() const { return (min + max) * 0.5f; }
    QVector3D extent() const { return max - min; }

    float radius() const { return extent().length() * 0.5f; }

    bool isValid() const { return max.x() >= min.x() && max.y() >= min.y() && max.z() >= min.z(); }
};

/**
 * @brief 射线（世界空间）。origin 为起点，direction 已归一化。
 */
struct Ray {
    QVector3D origin;
    QVector3D direction;

    QVector3D at(float t) const { return origin + direction * t; }
};

/**
 * @brief 射线 - 轴对齐包围盒相交检测（slab 算法）。
 * @param ray    世界空间射线
 * @param box    待测包围盒
 * @param tHit   输出：命中距离（沿射线方向）
 * @return 是否相交（tHit 位于 [0, +inf)）
 */
inline bool rayIntersectsAabb(const Ray &ray, const Aabb &box, float *tHit = nullptr)
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; ++i) {
        const float o = ray.origin[i];
        const float d = ray.direction[i];
        const float bmin = box.min[i];
        const float bmax = box.max[i];

        if (std::fabs(d) < 1e-8f) {
            // 射线平行于该轴，起点必须落在 slab 内
            if (o < bmin || o > bmax)
                return false;
        } else {
            float t1 = (bmin - o) / d;
            float t2 = (bmax - o) / d;
            if (t1 > t2)
                std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax)
                return false;
        }
    }

    if (tHit)
        *tHit = tMin;
    return true;
}

/**
 * @brief 射线 - 球体相交检测（用于缺陷点拾取，比 AABB 更贴合圆点标记）。
 * @return 是否相交；tHit 输出最近正向交点距离。
 */
inline bool rayIntersectsSphere(const Ray &ray, const QVector3D &center, float radius,
                                float *tHit = nullptr)
{
    const QVector3D oc = ray.origin - center;
    const float b = QVector3D::dotProduct(oc, ray.direction);
    const float c = QVector3D::dotProduct(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f)
        return false;

    const float sq = std::sqrt(disc);
    float t = -b - sq;
    if (t < 0.0f)
        t = -b + sq;
    if (t < 0.0f)
        return false;

    if (tHit)
        *tHit = t;
    return true;
}

/**
 * @brief 从视图/投影矩阵反推屏幕坐标对应的世界空间射线。
 * @param screenX/screenY 屏幕像素坐标（原点左上）
 * @param viewportW/H    视口像素尺寸
 * @param view           视图矩阵
 * @param projection     投影矩阵
 * @return 世界空间射线；若矩阵不可逆则方向为零向量
 */
inline Ray screenPointToRay(float screenX, float screenY, int viewportW, int viewportH,
                            const QMatrix4x4 &view, const QMatrix4x4 &projection)
{
    Ray ray;

    // 屏幕像素 -> NDC [-1, 1]
    const float ndcX = (2.0f * screenX) / std::max(1, viewportW) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screenY) / std::max(1, viewportH);

    const QMatrix4x4 inv = (projection * view).inverted();
    bool ok = false;

    QVector4D nearH = inv * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farH = inv * QVector4D(ndcX, ndcY, 1.0f, 1.0f);

    ok = std::fabs(nearH.w()) > 1e-8f && std::fabs(farH.w()) > 1e-8f;
    if (!ok)
        return ray;

    const QVector3D nearP = nearH.toVector3D() / nearH.w();
    const QVector3D farP = farH.toVector3D() / farH.w();

    ray.origin = nearP;
    ray.direction = (farP - nearP).normalized();
    return ray;
}

} // namespace math
} // namespace wafer

#endif // MATHUTILS_H
