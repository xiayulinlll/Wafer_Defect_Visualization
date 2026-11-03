#include "data/CsvDatasetExporter.h"
#include "data/CsvDatasetParser.h"
#include "data/DefectStatistics.h"
#include "data/Neighborhood.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <cmath>

using namespace wafer;

class TestDatasetExport : public QObject
{
    Q_OBJECT

private slots:
    void exportsHeaderAndRows();
    void exportedFileCanBeParsedBack();
    void commentLinesAreSkippedOnReload();
    void statisticsCrossTableAndZones();
    void statisticsYieldIsPoisson();
    void neighborhoodCountsNeighborsAndCluster();
    void neighborhoodHandlesSinglePoint();

private:
    static DefectPoint makePoint(int id, float x, float y, float z, float area, int sev, int layer);
};

DefectPoint TestDatasetExport::makePoint(int id, float x, float y, float z, float area, int sev,
                                         int layer)
{
    DefectPoint p;
    p.id = id;
    p.x = x;
    p.y = y;
    p.z = z;
    p.area = area;
    p.severity = static_cast<DefectSeverity>(sev);
    p.type = DefectType::Particle;
    p.layer = layer;
    return p;
}

void TestDatasetExport::exportsHeaderAndRows()
{
    QVector<DefectPoint> points;
    points << makePoint(1, 10.0f, 20.0f, 0.5f, 3.0f, 2, 1)
           << makePoint(2, -5.0f, 6.0f, 1.5f, 8.0f, 4, 3);

    QString out;
    QTextStream stream(&out);
    CsvDatasetExporter::Summary summary;
    summary.totalPoints = 100;
    summary.exportedPoints = 2;
    summary.filterDescription = QStringLiteral("等级 2~5");
    CsvDatasetExporter::writeTo(stream, points, summary);
    stream.flush();

    // 表头必须与解析器一致（导出文件可直接被本程序重新加载）
    QVERIFY(out.contains(CsvDatasetParser::headerLine()));
    // 统计注释行在前
    QVERIFY(out.startsWith(QLatin1String("#")));
    // 数据行格式：id,x,y,z,area,severity,TYPE,layer
    QVERIFY(out.contains(QStringLiteral("1,10.0000,20.0000,0.500,3.0000,2,PARTICLE,1")));
    QVERIFY(out.contains(QStringLiteral("2,-5.0000,6.0000,1.500,8.0000,4,PARTICLE,3")));
}

void TestDatasetExport::exportedFileCanBeParsedBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("filtered.csv"));

    QVector<DefectPoint> points;
    points << makePoint(11, 1.5f, -2.5f, 0.25f, 4.0f, 3, 2)
           << makePoint(12, -7.5f, 8.5f, 1.25f, 9.0f, 5, 4);

    QString error;
    QVERIFY2(CsvDatasetExporter::exportToFile(path, points, &error), qPrintable(error));

    const CsvDatasetParser::Result result = CsvDatasetParser::parseFile(path);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 2);
    QCOMPARE(result.dataset.points.at(0).id, 11);
    QCOMPARE(result.dataset.points.at(1).id, 12);
    QCOMPARE(static_cast<int>(result.dataset.points.at(1).severity), 5);
    QCOMPARE(result.dataset.points.at(1).layer, 4);
}

void TestDatasetExport::commentLinesAreSkippedOnReload()
{
    // 统计注释行以 '#' 开头：重新加载时应被识别为非法行（而不是被当成表头或数据）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("annotated.csv"));

    QVector<DefectPoint> points;
    points << makePoint(21, 0.0f, 0.0f, 0.0f, 1.0f, 1, 1);

    CsvDatasetExporter::Summary summary;
    summary.datasetName = QStringLiteral("wafer_defects.csv");
    summary.totalPoints = 10;
    summary.exportedPoints = 1;
    summary.extra = QStringLiteral("缺陷总数：10\n估计良率：90.0%");
    QString error;
    QVERIFY2(CsvDatasetExporter::exportToFile(path, points, summary, &error), qPrintable(error));

    const CsvDatasetParser::Result result = CsvDatasetParser::parseFile(path);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 1);
    QCOMPARE(result.dataset.points.first().id, 21);
    QCOMPARE(result.dataset.ignoredColumns.size(), 0);
}

void TestDatasetExport::statisticsCrossTableAndZones()
{
    QVector<DefectPoint> points;
    // 4 个分区（默认），半径 100mm：0~25 / 25~50 / 50~75 / 75~100
    points << makePoint(1, 0.0f, 0.0f, 0.0f, 1.0f, 5, 1)  // zone 0，严重
           << makePoint(2, 10.0f, 0.0f, 0.0f, 1.0f, 2, 1)  // zone 0
           << makePoint(3, -40.0f, 0.0f, 0.0f, 1.0f, 4, 1) // zone 1，严重
           << makePoint(4, 0.0f, 60.0f, 0.0f, 1.0f, 1, 1)  // zone 2
           << makePoint(5, 90.0f, 0.0f, 0.0f, 1.0f, 3, 1); // zone 3

    const DefectStatistics stats = DefectAnalyzer::compute(points, 100.0f);
    QCOMPARE(stats.total, 5);
    QCOMPARE(stats.criticalTotal, 2);
    QCOMPARE(stats.zones.size(), 4);
    QCOMPARE(stats.zones.at(0).total, 2);
    QCOMPARE(stats.zones.at(0).critical, 1);
    QCOMPARE(stats.zones.at(1).total, 1);
    QCOMPARE(stats.zones.at(2).total, 1);
    QCOMPARE(stats.zones.at(3).total, 1);

    // 交叉表：类型 Particle(0) × 等级
    QCOMPARE(stats.typeCounts[0][5], 1);
    QCOMPARE(stats.typeCounts[0][2], 1);
    QCOMPARE(stats.typeCounts[0][4], 1);
    QCOMPARE(stats.typeCounts[0][3], 1);
    QCOMPARE(stats.typeCounts[0][1], 1);

    // 密度：内圈面积 π*25²≈1963.5，2 个点 -> ≈0.102 个/100mm²
    QVERIFY(std::fabs(stats.zones.at(0).densityPer100mm2 - 0.1019) < 0.01);
}

void TestDatasetExport::statisticsYieldIsPoisson()
{
    QVector<DefectPoint> points;
    points << makePoint(1, 0.0f, 0.0f, 0.0f, 1.0f, 5, 1)
           << makePoint(2, 1.0f, 0.0f, 0.0f, 1.0f, 4, 1)
           << makePoint(3, 2.0f, 0.0f, 0.0f, 1.0f, 1, 1);

    const DefectStatistics stats = DefectAnalyzer::compute(points, 100.0f);
    QCOMPARE(stats.criticalTotal, 2);
    QVERIFY(std::fabs(stats.estimatedYield - 100.0 * std::exp(-2.0)) < 1e-3);

    const DefectStatistics empty = DefectAnalyzer::compute({}, 100.0f);
    QCOMPARE(empty.total, 0);
    QCOMPARE(empty.criticalTotal, 0);
    QVERIFY(std::fabs(empty.estimatedYield - 100.0) < 1e-6);
    QCOMPARE(empty.waferRadiusMm, 100.0f);
}

void TestDatasetExport::neighborhoodCountsNeighborsAndCluster()
{
    QVector<DefectPoint> points;
    points << makePoint(1, 0.0f, 0.0f, 0.0f, 1.0f, 3, 1)   // 中心
           << makePoint(2, 3.0f, 0.0f, 0.0f, 1.0f, 3, 1)   // 距中心 3mm（同类型）
           << makePoint(3, 6.0f, 0.0f, 0.0f, 1.0f, 3, 1)   // 距中心 6mm，与 2 相距 3mm
           << makePoint(4, 9.0f, 0.0f, 0.0f, 1.0f, 1, 1)   // 10mm 内
           << makePoint(5, 50.0f, 0.0f, 0.0f, 1.0f, 3, 1); // 远处孤立点（距中心 50mm）

    const NeighborInfo info =
        Neighborhood::analyze(points, 0, Neighborhood::kDefaultRadiusMm, 5.0f);

    QCOMPARE(info.neighborsInRadius, 3); // 2、3、4 都在 10mm 内
    QCOMPARE(info.typeMatesInRadius, 3); // 2、3 与 4 都是 Particle（4 只是等级不同）
    QVERIFY(std::fabs(info.nearestDistanceMm - 3.0f) < 1e-4f);
    QCOMPARE(info.nearestId, 2);
    // 连接距离 5mm：1-2-3-4 依次相距 3mm，全部连通；5 距离 50mm 独立
    QCOMPARE(info.clusterSize, 4);
}

void TestDatasetExport::neighborhoodHandlesSinglePoint()
{
    QVector<DefectPoint> points;
    points << makePoint(7, 1.0f, 1.0f, 0.0f, 1.0f, 2, 1);

    const NeighborInfo info = Neighborhood::analyze(points, 0, 10.0f, 5.0f);
    QCOMPARE(info.neighborsInRadius, 0);
    QCOMPARE(info.nearestId, -1);
    QVERIFY(info.nearestDistanceMm < 0.0f);
    QCOMPARE(info.clusterSize, 1);

    // 非法下标不应崩溃，返回全零结果
    const NeighborInfo bad = Neighborhood::analyze(points, 5, 10.0f, 5.0f);
    QCOMPARE(bad.clusterSize, 0);
    QCOMPARE(bad.neighborsInRadius, 0);
}

QTEST_MAIN(TestDatasetExport)
#include "tst_datasetexport.moc"
