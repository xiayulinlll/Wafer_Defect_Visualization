#include "data/DefectStatistics.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace wafer {

namespace {
/** 把等级下标安全映射到 1..5（越界归入 1，避免数组越界访问） */
int severityIndex(DefectSeverity s)
{
    const int v = static_cast<int>(s);
    return (v >= 1 && v <= 5) ? v : 1;
}

/** 把类型下标安全映射到 0..5 */
int typeIndex(DefectType t)
{
    const int v = static_cast<int>(t);
    return (v >= 0 && v <= 5) ? v : static_cast<int>(DefectType::Other);
}
} // namespace

DefectStatistics DefectAnalyzer::compute(const QVector<DefectPoint> &points, float radiusMm,
                                          int zoneCount)
{
    DefectStatistics stats;
    stats.total = points.size();

    float r = radiusMm;
    if (!(r > 0.0f)) {
        // 未显式给出半径时用包围盒推导，保证 Zone 划分与渲染层一致
        float maxSpan = 0.0f;
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;
        bool init = false;
        for (const DefectPoint &p : points) {
            if (!init) {
                minX = maxX = p.x;
                minY = maxY = p.y;
                init = true;
            } else {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
        }
        if (init) {
            maxSpan = 0.5f * std::max(maxX - minX, maxY - minY);
            r = maxSpan * constants::kDerivedRadiusMargin;
        }
        if (!(r > 0.0f))
            r = constants::kDefaultWaferRadiusMm;
    }
    stats.waferRadiusMm = r;

    zoneCount = std::max(1, zoneCount);
    const float zoneWidth = r / static_cast<float>(zoneCount);
    stats.zones.reserve(zoneCount);
    for (int i = 0; i < zoneCount; ++i) {
        ZoneStats z;
        z.index = i;
        z.innerRadiusMm = zoneWidth * static_cast<float>(i);
        z.outerRadiusMm = (i == zoneCount - 1) ? r : zoneWidth * static_cast<float>(i + 1);
        stats.zones.push_back(z);
    }

    for (const DefectPoint &p : points) {
        const int sev = severityIndex(p.severity);
        const int type = typeIndex(p.type);

        stats.typeCounts[type][sev] += 1;
        if (sev >= constants::kCriticalSeverity)
            ++stats.criticalTotal;

        // 归一化半径 -> 分区下标（落在晶圆外的点归入最外圈）
        const float norm = std::sqrt(p.x * p.x + p.y * p.y) / r;
        int zone = static_cast<int>(norm * static_cast<float>(zoneCount));
        zone = std::clamp(zone, 0, zoneCount - 1);

        ZoneStats &z = stats.zones[zone];
        ++z.total;
        ++z.countBySeverity[sev];
        if (sev >= constants::kCriticalSeverity)
            ++z.critical;
    }

    // 密度按「环带面积」归一化到每 100 mm^2
    for (ZoneStats &z : stats.zones) {
        const double inner = static_cast<double>(z.innerRadiusMm);
        const double outer = static_cast<double>(z.outerRadiusMm);
        const double area = M_PI * (outer * outer - inner * inner);
        z.densityPer100mm2 = area > 1e-6 ? static_cast<double>(z.total) / area * 100.0 : 0.0;
    }

    // 良率估算：把每个严重缺陷视为独立随机失效事件（泊松模型），
    // P(该晶圆无严重缺陷) = exp(-λ)，λ = 严重缺陷数。
    // 这是业界最常用的「缺陷密度 -> 良率」粗估模型（Poisson yield model）。
    const double lambda = static_cast<double>(stats.criticalTotal);
    stats.criticalPerDie = lambda;
    stats.estimatedYield = 100.0 * std::exp(-lambda);
    return stats;
}

QString DefectAnalyzer::summaryText(const DefectStatistics &stats)
{
    QStringList lines;
    lines << QStringLiteral("缺陷总数：%1，严重缺陷（等级 >= %2）：%3")
                 .arg(stats.total)
                 .arg(constants::kCriticalSeverity)
                 .arg(stats.criticalTotal);
    lines << QStringLiteral("晶圆半径：%1 mm，估计良率：%2%（泊松模型，λ=%3）")
                 .arg(stats.waferRadiusMm, 0, 'f', 1)
                 .arg(stats.estimatedYield, 0, 'f', 1)
                 .arg(stats.criticalPerDie, 0, 'f', 2);
    return lines.join(QLatin1Char('\n'));
}

} // namespace wafer
