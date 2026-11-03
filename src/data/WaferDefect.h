#ifndef WAFERDEFECT_H
#define WAFERDEFECT_H

#include "core/DefectTypes.h"

#include <QObject>
#include <QString>

namespace wafer {

/**
 * @brief 为 DefectPoint 提供 UI 侧便捷属性（颜色、等级文案）。
 * 保持为纯函数集合，避免给 POD 类型增加虚表/重量。
 */
struct DefectPresentation {
    /** 等级对应伪彩色（与着色器色带保持一致，用于图例与详情面板） */
    static QColor severityColor(DefectSeverity s)
    {
        switch (s) {
        case DefectSeverity::Level1: return QColor(0, 200, 120);   // 绿
        case DefectSeverity::Level2: return QColor(140, 220, 40);  // 黄绿
        case DefectSeverity::Level3: return QColor(240, 200, 30);  // 黄
        case DefectSeverity::Level4: return QColor(245, 120, 20);  // 橙
        case DefectSeverity::Level5: return QColor(230, 30, 30);   // 红
        }
        return QColor(160, 160, 160);
    }

    /** 等级文案 */
    static QString severityText(DefectSeverity s)
    {
        switch (s) {
        case DefectSeverity::Level1: return QObject::tr("1 - 轻微");
        case DefectSeverity::Level2: return QObject::tr("2 - 一般");
        case DefectSeverity::Level3: return QObject::tr("3 - 中等");
        case DefectSeverity::Level4: return QObject::tr("4 - 严重");
        case DefectSeverity::Level5: return QObject::tr("5 - 致命");
        }
        return QObject::tr("未知");
    }

    /** 等级归一化到 [0, 1]，供热力纹理灰度值使用 */
    static float severityNormalized(DefectSeverity s)
    {
        return static_cast<float>(static_cast<int>(s)) / 5.0f;
    }
};

} // namespace wafer

#endif // WAFERDEFECT_H
