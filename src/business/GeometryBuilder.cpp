#include "business/GeometryBuilder.h"
#include "core/Constants.h"

#include <cmath>

namespace wafer {

using constants::kDefaultWaferRadiusMm;
using constants::kWorldExtent;
using constants::kZUnitScale;

namespace {
using constants::kAreaNormalizeRef;

/**
 * 三角形绕序是否与给定方向一致（右手定则：normal = (b - a) × (c - a)）。
 * 已用 tst_geometry 实测确认 QVector3D::crossProduct(b - a, c - a) 即右手法线。
 */
inline bool triangleFacesAlongNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c,
                                     const QVector3D &n)
{
    return QVector3D::dotProduct(QVector3D::crossProduct(b - a, c - a), n) > 0.0f;
}

/**
 * XZ 平面上的有符号面积（> 0 表示从 +Y 俯视时为逆时针）。
 *
 * 推导：3D 右侧法线 = (b - a) × (c - a)，其 Y 分量为
 *   (b - a).z * (c - a).x - (b - a).x * (c - a).z
 * 因此这里必须用 (bz-az)*(cx-ax) - (bx-ax)*(cz-az)。
 * 注意它与「把 XZ 当成 2D 直接套用 (x2-x1)(y2-y1)-(y2-y1)(x2-x1)」的符号相反，
 * 因为 XZ 平面的「上」是 -Z，不是 +Z。
 */
inline float signedAreaXZ(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    return 0.5f * ((b.z() - a.z()) * (c.x() - a.x()) - (b.x() - a.x()) * (c.z() - a.z()));
}
} // namespace

bool GeometryBuilder::validateWaferDisk(float radius, int segments, float thickness,
                                        QString *errorMessage)
{
    QVector<WaferVertex> verts;
    QVector<unsigned int> tris;
    buildWaferDisk(radius, segments, thickness, &verts, &tris);

    if (verts.isEmpty() || tris.isEmpty() || tris.size() % 3 != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("晶圆网格为空或索引不是 3 的倍数");
        return false;
    }

    const int seg = std::max(16, segments);
    const float half = thickness * 0.5f;
    const float r = radius > 0.0f ? radius : kDefaultWaferRadiusMm;

    for (int i = 0; i < tris.size(); i += 3) {
        const unsigned int ia = tris[i];
        const unsigned int ib = tris[i + 1];
        const unsigned int ic = tris[i + 2];
        if (ia >= static_cast<unsigned int>(verts.size()) || ib >= static_cast<unsigned int>(verts.size())
            || ic >= static_cast<unsigned int>(verts.size())) {
            if (errorMessage)
                *errorMessage = QStringLiteral("索引越界：%1").arg(i / 3);
            return false;
        }

        const WaferVertex &va = verts[ia];
        const WaferVertex &vb = verts[ib];
        const WaferVertex &vc = verts[ic];
        const QVector3D a(va.px, va.py, va.pz);
        const QVector3D b(vb.px, vb.py, vb.pz);
        const QVector3D c(vc.px, vc.py, vc.pz);

        // 顶点法线求和（仅用于顶/底面的朝向判断，不能用于分类）
        const QVector3D n(va.nx + vb.nx + vc.nx, va.ny + vb.ny + vc.ny, va.nz + vb.nz + vc.nz);

        // 分类必须按几何位置而不是「顶点法线和」：侧壁的 3 个顶点法线都水平朝外，
        // 求和后的 Y 分量同样可能非零（实测 ±1.5），会被误判成顶/底面。
        // 顶点 0/1 是两个面的中心点，因此只要出现中心点就是顶/底面三角形。
        const bool isCapTriangle = (ia < 2 || ib < 2 || ic < 2);

        if (isCapTriangle) { // 顶/底面：法线必须与顶点法线同向，且绕序一致
            if (!triangleFacesAlongNormal(a, b, c, n)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("顶/底面三角形绕序与法线相反：三角形 #%1")
                                        .arg(i / 3);
                return false;
            }
            // 顶面：从 +Y 俯视为逆时针；底面：从 +Y 俯视为顺时针（等于从 -Y 仰视逆时针）
            const float area2d = signedAreaXZ(a, b, c);
            if (n.y() > 0.0f && area2d <= 0.0f) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("顶面绕序不是逆时针：三角形 #%1").arg(i / 3);
                return false;
            }
            if (n.y() < 0.0f && area2d >= 0.0f) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("底面绕序错误（会被背面剔除）：三角形 #%1")
                                        .arg(i / 3);
                return false;
            }
        } else { // 侧壁：法线应朝外，即与「顶点相对圆心的径向」同向
            const unsigned int ring = static_cast<unsigned int>((seg + 1) * 2);
            const bool isRingVertex = (ia >= 2 && ia < 2 + ring) || (ib >= 2 && ib < 2 + ring)
                                      || (ic >= 2 && ic < 2 + ring);
            if (!isRingVertex) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("侧壁三角形出现在非环顶点：三角形 #%1").arg(i / 3);
                return false;
            }
            const QVector3D centroid = (a + b + c) / 3.0f;
            QVector3D radial(centroid.x(), 0.0f, centroid.z());
            if (radial.lengthSquared() > 1e-8f) {
                radial.normalize();
                if (!triangleFacesAlongNormal(a, b, c, radial)) {
                    if (errorMessage)
                        *errorMessage = QStringLiteral("侧壁绕序朝内（会被背面剔除）：三角形 #%1")
                                            .arg(i / 3);
                    return false;
                }
            }
            // 侧壁顶点必须严格位于 ±half 两个高度上
            const float ay = std::fabs(std::fabs(a.y()) - half);
            const float by = std::fabs(std::fabs(b.y()) - half);
            const float cy = std::fabs(std::fabs(c.y()) - half);
            if (ay > 1e-5f || by > 1e-5f || cy > 1e-5f || qFuzzyCompare(r, 0.0f)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("侧壁顶点高度异常：三角形 #%1").arg(i / 3);
                return false;
            }
        }
    }

    return true;
}

void GeometryBuilder::mapDefectToWorld(const DefectPoint &p, float radius, float zScale,
                                        QVector3D *worldPos)
{
    if (!worldPos)
        return;

    const float r = radius > 0.0f ? radius : kDefaultWaferRadiusMm;
    const float k = kWorldExtent / r;
    // 数据 X/Y -> 世界 XZ 平面（Y 轴向上，符合 OpenGL 常规俯视观感）
    worldPos->setX(p.x * k);
    worldPos->setY(p.z * kZUnitScale * zScale);
    worldPos->setZ(-p.y * k);
}

// 三角形 (a, b, c) 在 XZ 平面上的二维叉积：>0 表示从 +Y 方向看是逆时针。
// 晶圆几何同时用于顶面/底面/侧壁，绕序错误会被背面剔除吞掉，这里显式对外暴露供单测覆盖。
float signedAreaXZ(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    return (b.x() - a.x()) * (c.z() - a.z()) - (c.x() - a.x()) * (b.z() - a.z());
}

// 期望朝向由「顶点法线平均值」与「几何法线」的点积判断：
// 点积 > 0 说明绕序与法线同向，即从该法线一侧看是逆时针（正面）。
bool triangleFacesAlongNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c,
                              const QVector3D &normal)
{
    const QVector3D n = QVector3D::crossProduct(b - a, c - a);
    return QVector3D::dotProduct(n, normal) > 0.0f;
}

void GeometryBuilder::buildWaferDisk(float radius, int segments, float thickness,
                                     QVector<WaferVertex> *vertices,
                                     QVector<unsigned int> *indices)
{
    if (!vertices || !indices)
        return;
    vertices->clear();
    indices->clear();

    segments = std::max(16, segments);
    const float half = thickness * 0.5f;
    const float r = radius > 0.0f ? radius : kDefaultWaferRadiusMm;

    // 顶点布局：0 = 顶面中心, 1 = 底面中心, 然后 顶面环 + 底面环
    // 顶面中心
    vertices->push_back({0.0f, half, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f});
    // 底面中心
    vertices->push_back({0.0f, -half, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f});

    const unsigned int topCenter = 0;
    const unsigned int bottomCenter = 1;

    for (int i = 0; i <= segments; ++i) {
        const float a = 2.0f * math::kPi * static_cast<float>(i) / static_cast<float>(segments);
        // XZ 平面上的角度参数化：必须取 (sin a, cos a)。
        // 若写成 (cos a, sin a)，点在 XZ 平面上是**顺时针**排列（因为 XZ 的「上」是 -Z），
        // 顶面扇形会得到顺时针绕序并被 glCullFace(GL_BACK) 剔除
        // （表现为「从上方看不到晶圆顶面」）。tst_geometry 会显式校验绕序。
        const float sx = std::sin(a);
        const float cz = std::cos(a);

        // UV 与纹理对齐：纹理覆盖晶圆外接正方形，中心 0.5
        const float u = 0.5f + 0.5f * sx;
        const float v = 0.5f + 0.5f * cz;

        vertices->push_back({r * sx, half, r * cz, 0.0f, 1.0f, 0.0f, u, v});
        vertices->push_back({r * sx, -half, r * cz, 0.0f, -1.0f, 0.0f, u, v});
    }

    // 索引
    for (int i = 0; i < segments; ++i) {
        const unsigned int t0 = 2 + i * 2;     // 顶面环当前
        const unsigned int t1 = 2 + (i + 1) * 2;
        const unsigned int b0 = t0 + 1;
        const unsigned int b1 = t1 + 1;

        // 顶面扇形（法线 +Y）：环顶点在 XZ 平面上按逆时针排列，
        // 因此 (center, t0, t1) 即为从 +Y 俯视的逆时针绕序。
        indices->push_back(topCenter);
        indices->push_back(t0);
        indices->push_back(t1);

        // 底面扇形（法线 -Y）：与顶面绕序相反。
        // 写成 (bCenter, b0, b1) 会得到 +Y 法线，与顶点法线相反，被背面剔除
        // （表现为「从下往上看晶圆消失」）。
        indices->push_back(bottomCenter);
        indices->push_back(b1);
        indices->push_back(b0);

        // 侧壁（两个三角形）。法线必须朝外（与顶点相对圆心的径向同向）。
        // 推导：T0=(sin a0, +h, cos a0), T1=(sin a1, +h, cos a1), B0=(sin a0, -h, cos a0)。
        // (T0 - c) × (T1 - c) 与径向 (sin a0, 0, cos a0) 反向，因此必须写成 (T0, B0, T1)；
        // 若写成 (T0, T1, B0)，侧壁法线朝内，会被背面剔除而「透空」。
        // 第二个三角形同理为 (T1, B0, B1)。
        indices->push_back(t0);
        indices->push_back(b0);
        indices->push_back(t1);

        indices->push_back(t1);
        indices->push_back(b0);
        indices->push_back(b1);
    }
}

void GeometryBuilder::buildDefectMarkers(const QVector<DefectPoint> &defects, float radius,
                                         float zScale, QVector<MarkerVertex> *out,
                                         QVector<QVector3D> *worldPositions)
{
    if (!out)
        return;
    out->clear();
    out->reserve(defects.size());
    if (worldPositions) {
        worldPositions->clear();
        worldPositions->reserve(defects.size());
    }

    const float r = radius > 0.0f ? radius : kDefaultWaferRadiusMm;

    for (const DefectPoint &d : defects) {
        QVector3D w;
        mapDefectToWorld(d, r, zScale, &w);

        MarkerVertex mv;
        mv.px = w.x();
        mv.py = w.y();
        mv.pz = w.z();
        mv.intensity = static_cast<float>(static_cast<int>(d.severity)) / 5.0f;

        // 标记半径：与缺陷面积正相关（面积越大标记越大），限制在合理区间。
        // 公式统一放在 constants::markerRadiusFor()，保证渲染与拾取完全一致。
        mv.radius = constants::markerRadiusFor(d.area);

        out->push_back(mv);
        if (worldPositions)
            worldPositions->push_back(w);
    }
}

void GeometryBuilder::buildGroundGrid(float halfExtent, int divisions, QVector<LineVertex> *out)
{
    if (!out)
        return;
    out->clear();

    divisions = std::max(2, divisions);
    const float step = (halfExtent * 2.0f) / static_cast<float>(divisions);
    const float minor = 0.16f;
    const float major = 0.24f;

    for (int i = 0; i <= divisions; ++i) {
        const float p = -halfExtent + step * static_cast<float>(i);
        const bool isAxisLine = std::fabs(p) < step * 0.5f;
        const float c = isAxisLine ? major : minor;

        // 平行 Z 轴的线
        out->push_back({p, 0.0f, -halfExtent, c, c, c});
        out->push_back({p, 0.0f, halfExtent, c, c, c});
        // 平行 X 轴的线
        out->push_back({-halfExtent, 0.0f, p, c, c, c});
        out->push_back({halfExtent, 0.0f, p, c, c, c});
    }
}

void GeometryBuilder::buildAxes(float length, QVector<LineVertex> *out)
{
    if (!out)
        return;
    out->clear();
    out->reserve(6);

    out->push_back({0.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.25f}); // X 红
    out->push_back({length, 0.0f, 0.0f, 1.0f, 0.25f, 0.25f});

    out->push_back({0.0f, 0.0f, 0.0f, 0.25f, 1.0f, 0.35f}); // Y 绿
    out->push_back({0.0f, length, 0.0f, 0.25f, 1.0f, 0.35f});

    out->push_back({0.0f, 0.0f, 0.0f, 0.35f, 0.55f, 1.0f}); // Z 蓝
    out->push_back({0.0f, 0.0f, length, 0.35f, 0.55f, 1.0f});
}

void GeometryBuilder::buildWaferOutline(float radius, int segments, const QVector3D &color,
                                        QVector<LineVertex> *out)
{
    if (!out)
        return;
    out->clear();
    out->reserve((segments + 1) * 2);

    const float r = radius > 0.0f ? radius : 1.0f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * math::kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * math::kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        out->push_back({r * std::cos(a0), 0.0f, r * std::sin(a0), color.x(), color.y(), color.z()});
        out->push_back({r * std::cos(a1), 0.0f, r * std::sin(a1), color.x(), color.y(), color.z()});
    }
}

} // namespace wafer
