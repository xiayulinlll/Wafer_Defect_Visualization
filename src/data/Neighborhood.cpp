#include "data/Neighborhood.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wafer {

namespace {
float distanceSq(const DefectPoint &a, const DefectPoint &b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/** 并查集（簇连通性） */
class UnionFind
{
public:
    explicit UnionFind(int n) : m_parent(static_cast<size_t>(std::max(0, n)))
    {
        for (int i = 0; i < n; ++i)
            m_parent[static_cast<size_t>(i)] = i;
    }

    int find(int x)
    {
        while (m_parent[static_cast<size_t>(x)] != x) {
            const int parent = m_parent[static_cast<size_t>(x)];
            m_parent[static_cast<size_t>(x)] = m_parent[static_cast<size_t>(parent)];
            x = m_parent[static_cast<size_t>(x)];
        }
        return x;
    }

    void unite(int a, int b)
    {
        const int ra = find(a);
        const int rb = find(b);
        if (ra != rb)
            m_parent[static_cast<size_t>(rb)] = ra;
    }

private:
    QVector<int> m_parent;
};
} // namespace

NeighborInfo Neighborhood::analyze(const QVector<DefectPoint> &points, int centerIndex,
                                   float radiusMm, float linkMm)
{
    NeighborInfo info;
    if (centerIndex < 0 || centerIndex >= points.size())
        return info;

    const DefectPoint &center = points.at(centerIndex);
    const float radius = std::max(0.0f, radiusMm);
    const float radiusSq = radius * radius;
    const float linkSq = std::max(0.0f, linkMm) * std::max(0.0f, linkMm);

    float nearestSq = std::numeric_limits<float>::max();

    // 1) 半径内统计 + 最近邻
    for (int i = 0; i < points.size(); ++i) {
        if (i == centerIndex)
            continue;
        const DefectPoint &p = points.at(i);
        const float d2 = distanceSq(center, p);

        if (d2 <= radiusSq) {
            ++info.neighborsInRadius;
            if (p.type == center.type)
                ++info.typeMatesInRadius;
        }
        if (d2 < nearestSq) {
            nearestSq = d2;
            info.nearestId = p.id;
        }
    }
    if (nearestSq < std::numeric_limits<float>::max())
        info.nearestDistanceMm = std::sqrt(nearestSq);

    // 2) 成簇：在「连接距离」图里做连通分量，只关心中心点所在簇。
    //    为控制 N 较大时的开销，先按半径链接过滤（距离 <= linkMm 才建边）。
    UnionFind uf(points.size());
    for (int i = 0; i < points.size(); ++i) {
        for (int j = i + 1; j < points.size(); ++j) {
            if (distanceSq(points.at(i), points.at(j)) <= linkSq)
                uf.unite(i, j);
        }
    }
    const int root = uf.find(centerIndex);
    int members = 0;
    for (int i = 0; i < points.size(); ++i) {
        if (uf.find(i) == root)
            ++members;
    }
    info.clusterSize = members;
    return info;
}

} // namespace wafer
