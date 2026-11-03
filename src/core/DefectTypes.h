#ifndef DEFECTTYPES_H
#define DEFECTTYPES_H

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace wafer {

/**
 * @brief 缺陷等级。与 CSV 中 "等级" 字段（1~5）一一对应。
 * 等级越高，代表缺陷越严重，热力云图着色越偏红。
 */
enum class DefectSeverity : int {
    Level1 = 1, // 轻微
    Level2 = 2, // 一般
    Level3 = 3, // 中等
    Level4 = 4, // 严重
    Level5 = 5  // 致命
};

/**
 * @brief 缺陷类型。使用枚举而非字符串，便于筛选与统计。
 */
enum class DefectType : int {
    Particle = 0,   // 颗粒污染
    Scratch = 1,    // 划伤
    Pattern = 2,    // 图形缺陷
    Void = 3,       // 空洞
    Residue = 4,    // 残留
    Other = 5       // 其他
};

namespace detail {
inline const char *const kDefectTypeNames[] = {
    "颗粒污染", "划伤", "图形缺陷", "空洞", "残留", "其他"};
} // namespace detail

/** 缺陷类型英文名（用于 UI 下拉框与日志） */
inline QString defectTypeName(DefectType t)
{
    const int idx = static_cast<int>(t);
    if (idx < 0 || idx >= 6)
        return QStringLiteral("未知");
    return QString::fromUtf8(detail::kDefectTypeNames[idx]);
}

/** 缺陷类型短名（ASCII，用于 CSV 字段匹配，如 PARTICLE） */
inline QString defectTypeKey(DefectType t)
{
    static const char *const keys[] = {"PARTICLE", "SCRATCH", "PATTERN",
                                       "VOID",     "RESIDUE", "OTHER"};
    const int idx = static_cast<int>(t);
    if (idx < 0 || idx >= 6)
        return QStringLiteral("OTHER");
    return QString::fromLatin1(keys[idx]);
}

/**
 * @brief 单个缺陷记录。Plain-Old-Data 风格，可安全地在后台线程填充后
 * 通过双缓冲传到渲染线程读取（仅含值语义成员）。
 */
struct DefectPoint {
    int id = -1;                         // 缺陷ID
    float x = 0.0f;                      // X坐标 (mm)
    float y = 0.0f;                      // Y坐标 (mm)
    float z = 0.0f;                      // Z坐标 / 高度 (um)
    float area = 0.0f;                   // 缺陷面积 (um^2)
    DefectSeverity severity = DefectSeverity::Level1; // 缺陷等级
    DefectType type = DefectType::Other; // 缺陷类型
    int layer = 0;                       // 掩膜层号
    int extraA = 0;                      // 可选扩展列 1（CSV 中存在该列时解析，否则 0）
    int extraB = 0;                      // 可选扩展列 2
};

/** 一次解析的完整数据集快照（只读共享，双缓冲的载体） */
struct DefectDataset {
    QVector<DefectPoint> points; // 缺陷点集合
    float waferRadius = 150.0f;  // 晶圆半径 (mm)，由数据范围推导
    QString sourceFile;          // 来源文件路径
    int skippedLines = 0;        // 解析时跳过的非法行数
    int totalLines = 0;          // 文件总行数（不含表头）
    int shortLines = 0;          // 其中「列数不足」的行数
    int invalidValueLines = 0;   // 其中「数值非法」的行数
    QStringList columns;         // 实际解析使用的列顺序（来自表头，便于 UI 展示/排查）
    QStringList ignoredColumns;  // 已识别但未使用的列名

    bool isEmpty() const { return points.isEmpty(); }
};

} // namespace wafer

Q_DECLARE_METATYPE(wafer::DefectDataset)

#endif // DEFECTTYPES_H
