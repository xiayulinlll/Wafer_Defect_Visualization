#ifndef NEIGHBORHOOD_H
#define NEIGHBORHOOD_H

#include "core/DefectTypes.h"

#include <QVector>

namespace wafer {

/** 单个缺陷的邻域信息（§3-8） */
struct NeighborInfo {
    int neighborsInRadius = 0;   // 半径内的其它缺陷数
    float nearestDistanceMm = -1.0f; // 最近邻距离；无其它点时为 -1
    int nearestId = -1;          // 最近邻缺陷 ID
    int clusterSize = 0;         // 通过「连接距离」连通得到的同簇成员数（含自身）
    int typeMatesInRadius = 0;   // 半径内的同类型缺陷数
};

/**
 * @brief 邻域/簇分析（数据/业务层，纯 CPU）。
 *
 * 用于详情面板回答「这个点周围还有多少缺陷、离最近的有多远、是否成簇」。
 * 采用暴力扫描（O(N) 单点查询），调用方按需缓存；点数很大时也只对一个点计算，
 * 不会成为交互瓶颈。
 */
class Neighborhood
{
public:
    /**
     * @param points        待分析的点集（通常是当前筛选后的可见点）
     * @param centerIndex   中心点下标
     * @param radiusMm      统计半径
     * @param linkMm        成簇连接距离（两点距离 <= linkMm 视为同簇）
     */
    static NeighborInfo analyze(const QVector<DefectPoint> &points, int centerIndex,
                                float radiusMm, float linkMm);

    /** 默认统计半径 (mm) 与成簇连接距离 (mm) */
    static constexpr float kDefaultRadiusMm = 10.0f;
    static constexpr float kDefaultLinkMm = 5.0f;
};

} // namespace wafer

#endif // NEIGHBORHOOD_H
