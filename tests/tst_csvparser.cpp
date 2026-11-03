#include "data/CsvDatasetParser.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace wafer;

class TestCsvDatasetParser : public QObject
{
    Q_OBJECT

private slots:
    void parsesValidLine();
    void rejectsMalformedLine();
    void parsesFileAndSkipsHeader();
    void mapsColumnsFromReorderedHeader();
    void recognizesChineseHeaderAliases();
    void reportsShortAndInvalidLinesSeparately();
    void skipsCommentLinesBeforeHeader();
    void derivesRadiusFromBounds();
    void respectsExplicitRadius();
    void handlesHeaderOnlyFile();
    void reportsMissingFile();
    void honorsCancellation();

private:
    QString writeTempCsv(const QString &content);
    QTemporaryDir m_dir;
};

QString TestCsvDatasetParser::writeTempCsv(const QString &content)
{
    const QString path = m_dir.filePath(QStringLiteral("sample.csv"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(content.toUtf8());
    f.close();
    return path;
}

void TestCsvDatasetParser::parsesValidLine()
{
    DefectPoint p;
    QVERIFY(CsvDatasetParser::parseLine(
        QStringLiteral("12,3.5,-4.25,0.8,55.0,4,3,2"), &p));
    QCOMPARE(p.id, 12);
    QVERIFY(qFuzzyCompare(p.x, 3.5f));
    QVERIFY(qFuzzyCompare(p.y, -4.25f));
    QVERIFY(qFuzzyCompare(p.z, 0.8f));
    QVERIFY(qFuzzyCompare(p.area, 55.0f));
    QCOMPARE(static_cast<int>(p.severity), 4);
    QCOMPARE(static_cast<int>(p.type), 3);
    QCOMPARE(p.layer, 2);
}

void TestCsvDatasetParser::rejectsMalformedLine()
{
    DefectPoint p;
    // 空行 / 纯空白：直接拒绝
    QVERIFY(!CsvDatasetParser::parseLine(QStringLiteral(""), &p));
    QVERIFY(!CsvDatasetParser::parseLine(QStringLiteral("   "), &p));
    // 字段数少于 3：拒绝
    QVERIFY(!CsvDatasetParser::parseLine(QStringLiteral("1,2"), &p));
    // ID 非法（负数 / 非数字）：拒绝
    QVERIFY(!CsvDatasetParser::parseLine(QStringLiteral("-5,1,2"), &p));
    QVERIFY(!CsvDatasetParser::parseLine(QStringLiteral("abc,1,2"), &p));
    // 注："1,2,3" 是合法行（id=1, x=2, y=3，其余字段取默认值）
    QVERIFY(CsvDatasetParser::parseLine(QStringLiteral("1,2,3"), &p));
    QCOMPARE(p.id, 1);
}

void TestCsvDatasetParser::parsesFileAndSkipsHeader()
{
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"
        "1,10,10,0.5,20.0,3,1,1\n"
        "2,-30,5,1.2,80.0,5,2,2\n"
        "3,0,0,0.0,10.0,1,1,1\n"));

    const auto result = CsvDatasetParser::parseFile(path, 150.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 3);
    QCOMPARE(result.dataset.points[0].id, 1);
    QCOMPARE(result.dataset.points[2].id, 3);
}

void TestCsvDatasetParser::mapsColumnsFromReorderedHeader()
{
    // 列顺序打乱（Y 在 X 之前、Layer 提前）：应按表头名字映射，而不是按位置猜
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,Y,Layer,X,Z,Area,Severity,Type\n"
        "1,20,3,10,0.5,20.0,4,2\n"));

    const auto result = CsvDatasetParser::parseFile(path, 150.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 1);

    const DefectPoint &p = result.dataset.points.first();
    QCOMPARE(p.id, 1);
    QVERIFY(qFuzzyCompare(p.x, 10.0f));
    QVERIFY(qFuzzyCompare(p.y, 20.0f));
    QVERIFY(qFuzzyCompare(p.z, 0.5f));
    QCOMPARE(p.layer, 3);
    QCOMPARE(static_cast<int>(p.severity), 4);
    QCOMPARE(static_cast<int>(p.type), 2);
}

void TestCsvDatasetParser::recognizesChineseHeaderAliases()
{
    // 注意：用 QString::fromUtf8 构造中文表头，避免源文件编码（/utf-8 开关）影响测试数据
    const QString header = QString::fromUtf8("\xE7\xBC\xBA\xE9\x99\xB7ID,X\xE5\x9D\x90\xE6\xA0\x87,"
                                             "Y\xE5\x9D\x90\xE6\xA0\x87,Z\xE9\xAB\x98\xE5\xBA\xA6,"
                                             "\xE9\x9D\xA2\xE7\xA7\xAF,\xE7\xAD\x89\xE7\xBA\xA7,"
                                             "\xE7\xB1\xBB\xE5\x9E\x8B,\xE5\xB1\x82\n");
    const QString path = writeTempCsv(header + QStringLiteral("5,1,2,3,40,5,1,2\n"));

    const auto result = CsvDatasetParser::parseFile(path, 150.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 1);

    const DefectPoint &p = result.dataset.points.first();
    QCOMPARE(p.id, 5);
    QVERIFY(qFuzzyCompare(p.x, 1.0f));
    QVERIFY(qFuzzyCompare(p.y, 2.0f));
    QVERIFY(qFuzzyCompare(p.z, 3.0f));
    QVERIFY(qFuzzyCompare(p.area, 40.0f));
    QCOMPARE(static_cast<int>(p.severity), 5);
    QCOMPARE(p.layer, 2);
}

void TestCsvDatasetParser::reportsShortAndInvalidLinesSeparately()
{
    // 2 行合法 + 1 行列数不足 + 1 行 ID 非数字
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"
        "1,10,10,0.5,20.0,3,1,1\n"
        "2,-30,5,1.2,80.0,5,2,2\n"
        "3,1\n"
        "abc,1,2,0.0,1.0,1,1,1\n"));

    const auto result = CsvDatasetParser::parseFile(path, 150.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 2);
    QCOMPARE(result.dataset.skippedLines, 2);
    QCOMPARE(result.dataset.shortLines, 1);
    QCOMPARE(result.dataset.invalidValueLines, 1);
    // 诊断字段之和必须等于跳过总数
    QCOMPARE(result.dataset.shortLines + result.dataset.invalidValueLines,
             result.dataset.skippedLines);
}

void TestCsvDatasetParser::derivesRadiusFromBounds()
{
    // X 跨度 100（-50~50），Y 跨度 120（-60~60）
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"
        "1,-50,60,0.0,20.0,3,1,1\n"
        "2,50,-60,0.0,20.0,3,1,1\n"));

    // waferRadius = 0 时按「最大半跨度 * 1.08」推导：max(50, 60) * 1.08 = 64.8
    const auto result = CsvDatasetParser::parseFile(path, 0.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QVERIFY(result.dataset.waferRadius >= 60.0f);
    QVERIFY(qFuzzyCompare(result.dataset.waferRadius, 64.8f));
}

void TestCsvDatasetParser::respectsExplicitRadius()
{
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"
        "1,10,10,0.5,20.0,3,1,1\n"));

    // 显式传入正半径时应原样保留，不自动推导
    const auto result = CsvDatasetParser::parseFile(path, 200.0f);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QVERIFY(qFuzzyCompare(result.dataset.waferRadius, 200.0f));
}

void TestCsvDatasetParser::handlesHeaderOnlyFile()
{
    const QString path = writeTempCsv(QStringLiteral(
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"));
    const auto result = CsvDatasetParser::parseFile(path, 150.0f);
    // 仅有表头时没有任何数据点，解析器视为失败并给出错误信息
    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
    QCOMPARE(result.dataset.points.size(), 0);
}

void TestCsvDatasetParser::reportsMissingFile()
{
    const auto result = CsvDatasetParser::parseFile(
        m_dir.filePath(QStringLiteral("does_not_exist.csv")), 150.0f);
    QVERIFY(!result.ok);
    QVERIFY(!result.errorMessage.isEmpty());
}

void TestCsvDatasetParser::honorsCancellation()
{
    // 构造超过 4096 行的文件，使取消回调在解析途中被调用
    QString content = QStringLiteral("DefectId,X,Y,Z,Area,Severity,Type,Layer\n");
    for (int i = 0; i < 9000; ++i) {
        content += QStringLiteral("%1,%2,%3,0.0,20.0,3,1,1\n")
                       .arg(i)
                       .arg(i % 100)
                       .arg(i % 70);
    }
    const QString path = writeTempCsv(content);

    // 首次回调即返回 true，模拟用户中止
    const auto result = CsvDatasetParser::parseFile(path, 150.0f,
                                                    []() { return true; });
    QVERIFY(!result.ok);
    QVERIFY(result.errorMessage.contains(QStringLiteral("取消")));
}

void TestCsvDatasetParser::skipsCommentLinesBeforeHeader()
{
    // 导出的筛选结果会在表头前写若干 '#' 统计注释行；
    // 重新加载时必须跳过它们（而不是把第一行注释当成表头，导致「缺少必需列」）。
    const QString content = QStringLiteral(
        "# Wafer Defect Export | 2026-10-18 22:00:00\n"
        "# Dataset: wafer_defects.csv\n"
        "# Exported: 2 / 3619 points\n"
        "DefectId,X,Y,Z,Area,Severity,Type,Layer\n"
        "1,1.0,2.0,0.5,3.0,4,PARTICLE,2\n"
        "2,-1.0,-2.0,0.6,5.0,5,SCRATCH,3\n");
    const QString path = writeTempCsv(content);
    QVERIFY(!path.isEmpty());

    const CsvDatasetParser::Result result = CsvDatasetParser::parseFile(path);
    QVERIFY2(result.ok, qPrintable(result.errorMessage));
    QCOMPARE(result.dataset.points.size(), 2);
    QCOMPARE(result.dataset.points.at(0).id, 1);
    QCOMPARE(result.dataset.points.at(1).id, 2);
    // 注释行既不算数据也不算跳过行
    QCOMPARE(result.dataset.skippedLines, 0);
}

QTEST_MAIN(TestCsvDatasetParser)
#include "tst_csvparser.moc"
