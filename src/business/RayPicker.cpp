#include "business/RayPicker.h"
#include "business/GeometryBuilder.h"
#include "core/Constants.h"

#include <algorithm>
#include <limits>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace wafer {

namespace {
/** 格子坐标钳制范围：世界坐标归一化后远小于该值，钳制只为防御异常数据 */
constexpr int kCellCoordLimit = 1 << 14;
constexpr int kCellCoordBase = 2 * kCellCoordLimit + 1;
} // namespace

void PickGrid::clear()
{
    m_sortedKeys.clear();
    m_cellStart.clear();
    m_cellItems.clear();
    m_count = 0;
}

int PickGrid::cellCoord(float v) const
{
    const int c = static_cast<int>(std::floor(v / m_cellSize));
    return std::clamp(c, -kCellCoordLimit, kCellCoordLimit);
}

int PickGrid::cellKey(float x, float z) const
{
    // 每轴偏移成非负坐标后做基数组合：查询与构建使用同一个函数，保证键一致
    const int cx = cellCoord(x) + kCellCoordLimit;
    const int cz = cellCoord(z) + kCellCoordLimit;
    return cx * kCellCoordBase + cz;
}

bool PickGrid::cellRange(int key, int *begin, int *end) const
{
    const auto it = std::lower_bound(m_sortedKeys.begin(), m_sortedKeys.end(), key);
    if (it == m_sortedKeys.end() || *it != key)
        return false;
    const size_t k = static_cast<size_t>(it - m_sortedKeys.begin());
    if (begin)
        *begin = m_cellStart[k];
    if (end)
        *end = m_cellStart[k + 1];
    return true;
}

void PickGrid::build(const QVector<QVector3D> &worldPositions, const QVector<DefectPoint> &defects)
{
    clear();
    const int n = std::min(worldPositions.size(), defects.size());
    if (n <= 0)
        return;

    // 格子边长按点数自适应（目标约 1 个点/格），并钳制在合理范围，
    // 使「点很少」与「点极密」两种极端都不会退化成全扫或单格。
    float minX = worldPositions[0].x();
    float maxX = minX;
    float minZ = worldPositions[0].z();
    float maxZ = minZ;
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, worldPositions[i].x());
        maxX = std::max(maxX, worldPositions[i].x());
        minZ = std::min(minZ, worldPositions[i].z());
        maxZ = std::max(maxZ, worldPositions[i].z());
    }
    const int dim = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(n))));
    const float span = std::max(maxX - minX, maxZ - minZ);
    m_cellSize = std::clamp(span / static_cast<float>(dim), 0.005f, 0.5f);

    // 1) 先按哈希桶分组（构建期 O(N)），再转成 CSR：
    //    查询时一次二分定位，格子内下标升序，保证与线性扫描的「先到先得」顺序一致。
    std::unordered_map<int, std::vector<CellRef>> buckets;
    buckets.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const int key = cellKey(worldPositions[i].x(), worldPositions[i].z());
        buckets[key].push_back(CellRef{key, i});
    }

    m_sortedKeys.reserve(buckets.size());
    for (const auto &entry : buckets)
        m_sortedKeys.push_back(entry.first);
    std::sort(m_sortedKeys.begin(), m_sortedKeys.end());

    m_cellStart.assign(m_sortedKeys.size() + 1, 0);
    m_cellItems.reserve(static_cast<size_t>(n));
    for (size_t k = 0; k < m_sortedKeys.size(); ++k) {
        m_cellStart[k] = static_cast<int>(m_cellItems.size());
        const std::vector<CellRef> &bucket = buckets[m_sortedKeys[k]];
        for (const CellRef &ref : bucket)
            m_cellItems.push_back(ref.index);
    }
    m_cellStart[m_sortedKeys.size()] = static_cast<int>(m_cellItems.size());
    m_count = n;
}

math::Ray RayPicker::buildRay(float screenX, float screenY, int viewportW, int viewportH,
                              const QMatrix4x4 &view, const QMatrix4x4 &projection)
{
    return math::screenPointToRay(screenX, screenY, viewportW, viewportH, view, projection);
}

math::Aabb RayPicker::computeBounds(const QVector<DefectPoint> &defects, float radius, float zScale)
{
    math::Aabb box;
    box.reset();
    for (const DefectPoint &d : defects) {
        QVector3D w;
        GeometryBuilder::mapDefectToWorld(d, radius, zScale, &w);
        box.expand(w);
    }
    if (!box.isValid()) {
        box.min = QVector3D(-1.0f, 0.0f, -1.0f);
        box.max = QVector3D(1.0f, 0.0f, 1.0f);
    }
    return box;
}

float RayPicker::worldPickRadius(const DefectPoint &defect, const QVector3D &worldPos,
                                 const math::Ray &ray, const PickParams &params)
{
    // 命中深度 = 标记点中心在视线方向上的投影长度（点精灵本身是一个正对视线的圆盘）。
    const float depth = QVector3D::dotProduct(worldPos - ray.origin, ray.direction);
    const float centerDepth = std::max(depth, 1e-3f);

    const float pixelSize =
        constants::markerPixelSize(defect.area, static_cast<int>(defect.severity),
                                   params.viewportHeight);
    float radius = constants::pixelSizeToWorldRadius(pixelSize, centerDepth, params.projScale,
                                                     params.viewportHeight);

    // 额外容差：把「差一点点」也算命中，用同样的像素->世界换算保证手感一致。
    radius += constants::pixelSizeToWorldRadius(params.tolerancePx, centerDepth, params.projScale,
                                                params.viewportHeight);

    // 下限：以中心深度算出的最小可点击半径（等价于 kMinPickRadiusPx 像素）。
    const float minRadius = constants::pixelSizeToWorldRadius(
        constants::kMinPickRadiusPx * 2.0f, centerDepth, params.projScale, params.viewportHeight);
    return std::max(radius, minRadius);
}

PickResult RayPicker::pick(const math::Ray &ray, const QVector<DefectPoint> &defects, float radius,
                           float zScale, const PickParams &params)
{
    PickResult best;
    best.distance = std::numeric_limits<float>::max();

    if (defects.isEmpty())
        return best;

    const float r = radius > 0.0f ? radius : constants::kDefaultWaferRadiusMm;

    for (int i = 0; i < defects.size(); ++i) {
        const DefectPoint &d = defects[i];

        QVector3D w;
        GeometryBuilder::mapDefectToWorld(d, r, zScale, &w);

        const float pickRadius = worldPickRadius(d, w, ray, params);

        float t = 0.0f;
        if (!math::rayIntersectsSphere(ray, w, pickRadius, &t))
            continue;

        if (t < best.distance) {
            best.distance = t;
            best.hit = true;
            best.defectId = d.id;
            best.index = i;
            best.worldPos = w;
        }
    }

    return best;
}

PickResult RayPicker::pickFromWorldPositions(const math::Ray &ray,
                                             const QVector<QVector3D> &worldPositions,
                                             const QVector<DefectPoint> &defects,
                                             const PickParams &params)
{
    PickResult best;
    best.distance = std::numeric_limits<float>::max();

    const int n = std::min(worldPositions.size(), defects.size());
    for (int i = 0; i < n; ++i) {
        const DefectPoint &d = defects[i];
        const float pickRadius = worldPickRadius(d, worldPositions[i], ray, params);

        float t = 0.0f;
        if (!math::rayIntersectsSphere(ray, worldPositions[i], pickRadius, &t))
            continue;

        if (t < best.distance) {
            best.distance = t;
            best.hit = true;
            best.defectId = d.id;
            best.index = i;
            best.worldPos = worldPositions[i];
        }
    }

    return best;
}

PickResult RayPicker::pickWithGrid(const math::Ray &ray, const QVector<QVector3D> &worldPositions,
                                   const QVector<DefectPoint> &defects, const PickParams &params,
                                   const PickGrid *grid)
{
    // 没有索引（点数少 / 索引失效）时退回线性扫描，保证正确性优先。
    if (!grid || grid->isEmpty() || !grid->matches(worldPositions.size(), defects.size()))
        return pickFromWorldPositions(ray, worldPositions, defects, params);

    PickResult best;
    best.distance = std::numeric_limits<float>::max();

    const int n = std::min(worldPositions.size(), defects.size());
    if (n <= 0)
        return best;

    // 1) 求射线与「点云 XZ 包围盒」的重叠区间 [tMin, tMax]。
    //    包围盒向外扩一格，保证沿边界的点不会因为浮点误差被跳过。
    float minX = worldPositions[0].x();
    float maxX = minX;
    float minZ = worldPositions[0].z();
    float maxZ = minZ;
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, worldPositions[i].x());
        maxX = std::max(maxX, worldPositions[i].x());
        minZ = std::min(minZ, worldPositions[i].z());
        maxZ = std::max(maxZ, worldPositions[i].z());
    }
    const float pad = grid->cellSize();
    minX -= pad;
    maxX += pad;
    minZ -= pad;
    maxZ += pad;

    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    bool parallelMiss = false;

    auto slab = [&](float origin, float dir, float lo, float hi) {
        if (std::fabs(dir) < 1e-8f) {
            if (origin < lo || origin > hi)
                parallelMiss = true;
            return;
        }
        float t0 = (lo - origin) / dir;
        float t1 = (hi - origin) / dir;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
    };
    slab(ray.origin.x(), ray.direction.x(), minX, maxX);
    slab(ray.origin.z(), ray.direction.z(), minZ, maxZ);

    if (parallelMiss || tMax < tMin)
        return best; // 射线不与点云平面区域相交，必然无命中

    // 2) 沿射线做 2D DDA，按顺序访问格子 —— 顺序即「由近到远」，
    //    因此第一次命中就是最近命中，可以提前结束（无需比较全部候选）。
    const float cell = grid->cellSize();
    const float startT = tMin - cell;
    const float endT = tMax + cell;

    const float px0 = ray.origin.x() + ray.direction.x() * startT;
    const float pz0 = ray.origin.z() + ray.direction.z() * startT;

    int cx = static_cast<int>(std::floor(px0 / cell));
    int cz = static_cast<int>(std::floor(pz0 / cell));

    const int stepX = ray.direction.x() > 1e-8f ? 1 : (ray.direction.x() < -1e-8f ? -1 : 0);
    const int stepZ = ray.direction.z() > 1e-8f ? 1 : (ray.direction.z() < -1e-8f ? -1 : 0);

    // 到下一条格线的参数增量；方向分量为 0 时用无穷大（永不跨越该轴）
    const float tDeltaX = (stepX != 0) ? std::fabs(cell / ray.direction.x())
                                       : std::numeric_limits<float>::infinity();
    const float tDeltaZ = (stepZ != 0) ? std::fabs(cell / ray.direction.z())
                                       : std::numeric_limits<float>::infinity();
    const float nextEdgeX = static_cast<float>(stepX > 0 ? cx + 1 : cx) * cell;
    const float nextEdgeZ = static_cast<float>(stepZ > 0 ? cz + 1 : cz) * cell;
    float tMaxX = (stepX != 0) ? startT + std::fabs((nextEdgeX - px0) / ray.direction.x())
                               : std::numeric_limits<float>::infinity();
    float tMaxZ = (stepZ != 0) ? startT + std::fabs((nextEdgeZ - pz0) / ray.direction.z())
                               : std::numeric_limits<float>::infinity();

    // 3) 逐格检查。限制最大步数，避免异常输入（方向几乎为 0）导致长循环。
    //
    //    正确性要点：拾取半径按深度换算成世界半径，可能远大于格子边长
    //    （相机贴得很近时可达数格）。因此不能「第一次命中就返回」，
    //    而是把每个格子向外扩 R 圈、汇总所有可能相交的点后取最近命中，
    //    结果与线性扫描严格一致（tst_raypicker 中有逐点比对回归）。
    const float cellRadius = std::max(2.0f, params.maxPickRadius / cell);
    const int expand = std::min(12, static_cast<int>(std::ceil(cellRadius)));

    constexpr int kMaxSteps = 4096;
    std::vector<int> visited;
    visited.reserve(256);

    int steps = 0;
    while (steps++ < kMaxSteps) {
        for (int dz = -expand; dz <= expand; ++dz) {
            for (int dx = -expand; dx <= expand; ++dx) {
                const int gx = cx + dx;
                const int gz = cz + dz;
                const int key = grid->cellKey(static_cast<float>(gx) * cell + cell * 0.5f,
                                              static_cast<float>(gz) * cell + cell * 0.5f);
                int begin = 0;
                int end = 0;
                if (grid->cellRange(key, &begin, &end))
                    visited.insert(visited.end(), grid->itemsBegin() + begin,
                                   grid->itemsBegin() + end);
            }
        }

        // 走出包围盒（含外扩）即可结束
        const float traveled = std::min(tMaxX, tMaxZ);
        if (traveled > endT)
            break;

        if (tMaxX <= tMaxZ) {
            cx += stepX;
            tMaxX += tDeltaX;
        } else {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (tMaxX == std::numeric_limits<float>::infinity()
            && tMaxZ == std::numeric_limits<float>::infinity())
            break; // 两个方向都无法前进（理论上不会发生）
    }

    // 去重（外扩窗口会把同一格子重复收集多次），再统一做权威的 ray-sphere 判定
    std::sort(visited.begin(), visited.end());
    visited.erase(std::unique(visited.begin(), visited.end()), visited.end());

    for (int i : visited) {
        if (i < 0 || i >= n)
            continue;
        const QVector3D &w = worldPositions[i];
        const float pickRadius = worldPickRadius(defects[i], w, ray, params);
        float t = 0.0f;
        if (!math::rayIntersectsSphere(ray, w, pickRadius, &t))
            continue;
        if (t < best.distance) {
            best.distance = t;
            best.hit = true;
            best.defectId = defects[i].id;
            best.index = i;
            best.worldPos = w;
        }
    }

    return best;
}

} // namespace wafer
