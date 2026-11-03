#ifndef CSVDATASETEXPORTER_H
#define CSVDATASETEXPORTER_H

#include "core/DefectTypes.h"

#include <QString>

class QTextStream;

namespace wafer {

/**
 * @brief 当前筛选结果导出为 CSV（§3-6：报告与二次分析）。
 *
 * 设计要点：
 *  - 表头复用 `CsvDatasetParser::headerLine()`，导出的文件可直接被本程序重新加载；
 *  - 以 `#` 开头的行是**统计注释行**，加载时会被解析器识别为非法行并跳过，
 *    因此「带统计摘要的导出文件」仍然是合法数据集；
 *  - 输出 UTF-8（带 BOM），避免 Excel 打开中文时乱码。
 */
class CsvDatasetExporter
{
public:
    /** 统计摘要（可为空；非空时写成前导注释行） */
    struct Summary {
        QString datasetName;
        int totalPoints = 0;     // 数据集总点数
        int exportedPoints = 0;  // 本次导出点数
        QString filterDescription;
        QString extra;           // 额外多行文本（如统计结果）
    };

    /**
     * @param filePath 目标文件（后缀为空时自动补 .csv）
     * @param points   要导出的缺陷点
     * @param summary  可选统计摘要
     * @param errorMessage 失败原因
     */
    static bool exportToFile(const QString &filePath, const QVector<DefectPoint> &points,
                             const Summary &summary, QString *errorMessage = nullptr);

    /** 简化重载：不写统计摘要 */
    static bool exportToFile(const QString &filePath, const QVector<DefectPoint> &points,
                             QString *errorMessage = nullptr)
    {
        return exportToFile(filePath, points, Summary(), errorMessage);
    }

    /** 写出到任意流（便于单元测试） */
    static void writeTo(QTextStream &stream, const QVector<DefectPoint> &points,
                        const Summary &summary);

    /** 单行 CSV（与解析器的列顺序一致） */
    static QString lineFor(const DefectPoint &point);
};

} // namespace wafer

#endif // CSVDATASETEXPORTER_H
