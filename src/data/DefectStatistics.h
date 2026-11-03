#ifndef DEFECTSTATISTICS_H
#define DEFECTSTATISTICS_H

#include "core/Constants.h"
#include "core/DefectTypes.h"

#include <QString>
#include <QVector>

namespace wafer {

/** 按半径划分的分区（Zone）统计结果 */
struct ZoneStats {
    int index = 0;            // 0 表示最内圈
    float innerRadiusMm = 0.0f;
    float outerRadiusMm = 0.0f;
    int total = 0;            // 该区缺陷总数
    int critical = 0;         // 其中等级 >= 4 的数量
    int countBySeverity[6] = {0, 0, 0, 0, 0, 0}; // 下标 1~5 有效
    /** 密度：每 100 mm^2 的缺陷数，便于不同面积的分区横向比较 */
    double densityPer100mm2 = 0.0;
};

/** 整体统计结果 */
struct DefectStatistics {
    int total = 0;
    float waferRadiusMm = 0.0f;
    /** 交叉表：typeCounts[类型][等级]，等级下标 1~5 */
    int typeCounts[6][6] = {};
    QVector<ZoneStats> zones;
    /** 良率估算：每片晶圆含任一「等级 >= 4」缺陷即判为失效片。 */
    double estimatedYield = 100.0;
    /** 每片晶圆平均严重缺陷数（用于良率模型的泊松项） */
    double criticalPerDie = 0.0;
    int criticalTotal = 0;
};

/**
 * @brief 检测数据统计分析（数据/业务层，纯 CPU，无 UI 依赖）。
 *
 * 提供检测数据叠加最常用的三类统计：
 *  1. 「类型 × 等级」交叉表；
 *  2. 按半径分环（Zone）的缺陷分布与密度；
 *  3. 基于泊松模型的良率估算（P(0 个严重缺陷) = exp(-λ)）。
 *
 * 结果仅依赖传入的点集，不持有状态，便于单元测试。
 */
class DefectAnalyzer
{
public:
    /** 计算统计结果；radiusMm <= 0 时按点集包围盒推导 */
    static DefectStatistics compute(const QVector<DefectPoint> &points, float radiusMm,
                                    int zoneCount = constants::kDefaultZoneCount);

    /** 单行文本摘要（供日志/导出使用） */
    static QString summaryText(const DefectStatistics &stats);
};

} // namespace wafer

#endif // DEFECTSTATISTICS_H
