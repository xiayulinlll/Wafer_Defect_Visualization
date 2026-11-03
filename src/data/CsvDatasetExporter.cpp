#include "data/CsvDatasetExporter.h"

#include "data/CsvDatasetParser.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace wafer {

QString CsvDatasetExporter::lineFor(const DefectPoint &point)
{
    // 列顺序与 CsvDatasetParser::headerLine() 一致
    return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8")
        .arg(point.id)
        .arg(point.x, 0, 'f', 4)
        .arg(point.y, 0, 'f', 4)
        .arg(point.z, 0, 'f', 3)
        .arg(point.area, 0, 'f', 4)
        .arg(static_cast<int>(point.severity))
        .arg(defectTypeKey(point.type))
        .arg(point.layer);
}

void CsvDatasetExporter::writeTo(QTextStream &stream, const QVector<DefectPoint> &points,
                                 const Summary &summary)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    // 统计注释行：以 '#' 开头，重新加载时会被当作非法行跳过（不污染数据）
    stream << "# Wafer Defect Export | " << stamp << "\n";
    if (!summary.datasetName.isEmpty())
        stream << "# Dataset: " << summary.datasetName << "\n";
    if (summary.totalPoints > 0) {
        stream << "# Exported: " << summary.exportedPoints << " / " << summary.totalPoints
               << " points\n";
    }
    if (!summary.filterDescription.isEmpty())
        stream << "# Filter: " << summary.filterDescription << "\n";
    if (!summary.extra.isEmpty()) {
        const QStringList extraLines = summary.extra.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : extraLines)
            stream << "# " << line << "\n";
    }

    stream << CsvDatasetParser::headerLine() << "\n";
    for (const DefectPoint &p : points)
        stream << lineFor(p) << "\n";
}

bool CsvDatasetExporter::exportToFile(const QString &filePath, const QVector<DefectPoint> &points,
                                      const Summary &summary, QString *errorMessage)
{
    QString path = filePath;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".csv");

    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建目录：%1").arg(dir.absolutePath());
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法写入文件：%1 (%2)").arg(path, file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    // Excel 需要 BOM 才能正确识别 UTF-8 中文
    stream.setGenerateByteOrderMark(true);
    writeTo(stream, points, summary);
    stream.flush();
    file.close();
    return file.error() == QFileDevice::NoError;
}

} // namespace wafer
