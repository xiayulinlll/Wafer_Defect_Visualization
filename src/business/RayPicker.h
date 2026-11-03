#ifndef RAYPICKER_H
#define RAYPICKER_H

#include "core/Constants.h"
#include "core/DefectTypes.h"
#include "core/MathUtils.h"

#include <QMatrix4x4>
#include <QVector>

#include <unordered_map>
#include <vector>

namespace wafer {

/** 拾取结果 */
struct PickResult {
    bool hit = false;
    int defectId = -1;
    int index = -1;         // 在传入缺陷数组中的下标
    float distance = 0.0f;  // 相机到命中点的距离
    QVector3D worldPos;     // 命中点世界坐标
};

/**
 * @brief XZ 平面均匀网格索引（§2-6：10 万点级别点击不再线性全扫）。
 *
 * 为什么是「候选过滤」而不是「只信任网格」：
 *  - 标记点的拾取半径按深度换算，理论上可能远大于一个格子；
 *  - 因此网格只用来快速排除「肯定不相交」的点，命中判定仍走同一条
 *    ray-sphere 代码，保证与线性扫描**结果完全一致**（可单元测试回归）。
 */
class PickGrid
{
public:
    void clear();
    bool isEmpty() const { return m_cellStart.empty(); }
    int size() const { return m_count; }

    /** 构建索引；worldPositions 与 defects 一一对应（下标顺序必须与拾取时一致） */
    void build(const QVector<QVector3D> &worldPositions, const QVector<DefectPoint> &defects);

    /** 世界坐标 -> 网格键（与 build() 内部使用的完全相同） */
    int cellKey(float x, float z) const;
    float cellSize() const { return m_cellSize; }

    /** 格子键对应的下标区间（CSR）；不存在时返回 false */
    bool cellRange(int key, int *begin, int *end) const;

    /** CSR 下标数组起点（供拾取器批量收集候选） */
    const int *itemsBegin() const { return m_cellItems.data(); }

    /** 索引是否与给定数组一致（点数变化后需要重建） */
    bool matches(int positionCount, int defectCount) const
    {
        return m_count == positionCount && m_count == defectCount;
    }

private:
    struct CellRef {
        int cell = 0;
        int index = 0;
    };

    int cellCoord(float v) const;

    std::vector<int> m_sortedKeys; // 有序格子键
    std::vector<int> m_cellStart;  // CSR 起始（末尾多一个 total）
    std::vector<int> m_cellItems;  // CSR 下标（格子内保持升序）
    int m_count = 0;
    float m_cellSize = 0.1f;
};

/**
 * @brief 拾取参数（屏幕空间尺度）。
 *
 * 标记点的屏幕尺寸由 constants::markerPixelSize() 决定，而同一公式在
 * 渲染（marker.vert）与拾取（本类）两处使用。为了让「看到的圆」= 「能点中的圆」，
 * 拾取时把像素半径按命中深度反算成世界半径，而不是使用固定世界容差。
 */
struct PickParams {
    float viewportWidth = 1280.0f;             // 视口宽度（CSS 像素）
    float viewportHeight = 800.0f;             // 视口高度（CSS 像素）
    float projScale = 2.0f / constants::kFovTanHalf; // 投影矩阵 (1,1)
    float tolerancePx = constants::kPickTolerancePx; // 额外容差（CSS 像素）
    /**
     * 本帧最大可能的拾取半径（世界单位）。
     *
     * 由调用方用「最近可见点深度」估算：半径与深度成正比，近处的点半径最大。
     * 空间索引用它决定探查范围，取大一些只会多测几个点，不会漏检。
     */
    float maxPickRadius = 0.1f;
};

/**
 * @brief 射线拾取模块（业务层，难点3：三维场景鼠标拾取）。
 *
 * 思路：不做 GPU 拾取（开销大），也不做纯 2D 屏幕距离比较（旋转后不准）。
 * 而是把屏幕坐标反投影为世界空间射线，再对每个缺陷标记点做
 * 射线-球相交（等价于包围体相交），取最近命中。
 *
 * 尺度一致性（关键）：标记点是**固定像素大小**的点精灵，因此拾取半径必须
 * 按命中点深度换算成世界半径，否则相机拉远/平移后会出现
 * 「明明看到点却点不中」。换算公式与 constants::markerPixelSize() 共用。
 */
class RayPicker
{
public:
    RayPicker() = default;

    /**
     * @brief 构建世界空间射线。
     * @param screenX/screenY 视口内像素坐标（原点左上，CSS 像素即可）
     * @param viewportW/H     视口尺寸（像素，与屏幕坐标同一坐标系）
     * @param view / projection 相机矩阵
     * @return 世界空间射线
     */
    static math::Ray buildRay(float screenX, float screenY, int viewportW, int viewportH,
                              const QMatrix4x4 &view, const QMatrix4x4 &projection);

    /**
     * @brief 在缺陷点集合中做射线拾取（内部先做坐标映射，适合小数据集）。
     * @param params 屏幕尺度参数（视口尺寸 / 投影缩放 / 容差像素）
     */
    static PickResult pick(const math::Ray &ray, const QVector<DefectPoint> &defects,
                           float radius, float zScale, const PickParams &params);

    /**
     * @brief 基于渲染层缓存世界坐标的快速拾取（避免重复做坐标映射）。
     * worldPositions 与 defects 一一对应。
     */
    static PickResult pickFromWorldPositions(const math::Ray &ray,
                                             const QVector<QVector3D> &worldPositions,
                                             const QVector<DefectPoint> &defects,
                                             const PickParams &params);

    /**
     * @brief 带空间索引的拾取（§2-6）。
     *
     * 用 XZ 平面均匀网格把候选点限定在射线经过的格子附近，再走与线性扫描
     * **完全相同**的 ray-sphere 判定，因此结果与 pickFromWorldPositions()
     * 逐字段一致。grid 为空或点数不匹配时自动退回线性扫描。
     */
    static PickResult pickWithGrid(const math::Ray &ray, const QVector<QVector3D> &worldPositions,
                                   const QVector<DefectPoint> &defects, const PickParams &params,
                                   const PickGrid *grid);

    /** 单个标记点的拾取半径（世界单位）：像素尺寸 -> 该深度的世界半径 */
    static float worldPickRadius(const DefectPoint &defect, const QVector3D &worldPos,
                                 const math::Ray &ray, const PickParams &params);

    /** 计算当前缺陷集合的世界空间包围盒（供相机适配/调试） */
    static math::Aabb computeBounds(const QVector<DefectPoint> &defects, float radius, float zScale);
};

} // namespace wafer

#endif // RAYPICKER_H
