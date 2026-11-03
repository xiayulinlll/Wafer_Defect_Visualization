#include "data/DefectFilter.h"

#include <QTest>

#include <limits>

using namespace wafer;

class TestDefectFilter : public QObject
{
    Q_OBJECT

private slots:
    void passThroughAcceptsAll();
    void filtersBySeverityRange();
    void filtersByTypeSet();
    void emptyTypeSetMeansAll();
    void applyReturnsCount();
    void filtersByAreaRange();
    void filtersByHeightRange();
    void negativeBoundsMeanUnlimited();
    void compareHeightAgainstRawZWhenLayerOffsetEnabled();

private:
    static DefectPoint makePoint(int id, DefectSeverity sev, DefectType type);
};

DefectPoint TestDefectFilter::makePoint(int id, DefectSeverity sev, DefectType type)
{
    DefectPoint p;
    p.id = id;
    p.severity = sev;
    p.type = type;
    p.x = 1.0f;
    p.y = 1.0f;
    p.area = 10.0f;
    return p;
}

void TestDefectFilter::passThroughAcceptsAll()
{
    DefectFilter f;
    QVERIFY(f.isPassThrough());
    QVERIFY(f.accepts(makePoint(1, DefectSeverity::Level1, DefectType::Particle)));
    QVERIFY(f.accepts(makePoint(2, DefectSeverity::Level5, DefectType::Scratch)));
}

void TestDefectFilter::filtersBySeverityRange()
{
    DefectFilter f;
    f.setMinSeverity(DefectSeverity::Level3);
    QVERIFY(!f.isPassThrough());
    QVERIFY(!f.accepts(makePoint(1, DefectSeverity::Level2, DefectType::Particle)));
    QVERIFY(f.accepts(makePoint(2, DefectSeverity::Level3, DefectType::Particle)));
    QVERIFY(f.accepts(makePoint(3, DefectSeverity::Level5, DefectType::Particle)));
}

void TestDefectFilter::filtersByTypeSet()
{
    DefectFilter f;
    f.setTypes(QSet<int>{static_cast<int>(DefectType::Scratch)});
    QVERIFY(!f.accepts(makePoint(1, DefectSeverity::Level3, DefectType::Particle)));
    QVERIFY(f.accepts(makePoint(2, DefectSeverity::Level3, DefectType::Scratch)));
}

void TestDefectFilter::emptyTypeSetMeansAll()
{
    DefectFilter f;
    f.setTypes(QSet<int>{});
    QVERIFY(f.accepts(makePoint(1, DefectSeverity::Level3, DefectType::Particle)));
    QVERIFY(f.accepts(makePoint(2, DefectSeverity::Level3, DefectType::Scratch)));
}

void TestDefectFilter::applyReturnsCount()
{
    DefectDataset ds;
    ds.points.push_back(makePoint(1, DefectSeverity::Level1, DefectType::Particle));
    ds.points.push_back(makePoint(2, DefectSeverity::Level4, DefectType::Scratch));
    ds.points.push_back(makePoint(3, DefectSeverity::Level5, DefectType::Scratch));

    DefectFilter f;
    f.setMinSeverity(DefectSeverity::Level4);

    QVector<DefectPoint> out;
    const int n = f.apply(ds, &out);
    QCOMPARE(n, 2);
    QCOMPARE(out.size(), 2);
    QCOMPARE(out[0].id, 2);
    QCOMPARE(out[1].id, 3);

    // 重复调用应先清空再填充
    QCOMPARE(f.apply(ds, &out), 2);
    QCOMPARE(out.size(), 2);
}

void TestDefectFilter::filtersByAreaRange()
{
    DefectFilter f;
    f.setAreaRange(5.0f, 20.0f);

    DefectPoint small = makePoint(1, DefectSeverity::Level3, DefectType::Particle);
    small.area = 4.99f;
    DefectPoint edgeLow = makePoint(2, DefectSeverity::Level3, DefectType::Particle);
    edgeLow.area = 5.0f;
    DefectPoint mid = makePoint(3, DefectSeverity::Level3, DefectType::Particle);
    mid.area = 12.0f;
    DefectPoint edgeHigh = makePoint(4, DefectSeverity::Level3, DefectType::Particle);
    edgeHigh.area = 20.0f;
    DefectPoint big = makePoint(5, DefectSeverity::Level3, DefectType::Particle);
    big.area = 20.01f;

    QVERIFY(!f.accepts(small));
    QVERIFY(f.accepts(edgeLow));   // 闭区间下界
    QVERIFY(f.accepts(mid));
    QVERIFY(f.accepts(edgeHigh));  // 闭区间上界
    QVERIFY(!f.accepts(big));
    QVERIFY(!f.isPassThrough());
}

void TestDefectFilter::filtersByHeightRange()
{
    DefectFilter f;
    f.setHeightRange(0.0f, 1.0f);

    DefectPoint below = makePoint(1, DefectSeverity::Level3, DefectType::Particle);
    below.z = -0.01f;
    DefectPoint inRange = makePoint(2, DefectSeverity::Level3, DefectType::Particle);
    inRange.z = 0.5f;
    DefectPoint above = makePoint(3, DefectSeverity::Level3, DefectType::Particle);
    above.z = 1.01f;

    QVERIFY(!f.accepts(below));
    QVERIFY(f.accepts(inRange));
    QVERIFY(!f.accepts(above));
}

void TestDefectFilter::negativeBoundsMeanUnlimited()
{
    // -1（UI 的「不限」）与非有限值都不应限制该端
    DefectFilter f;
    f.setAreaRange(-1.0f, -3.0f);
    QCOMPARE(f.minArea(), DefectFilter::kNoLimit);
    QCOMPARE(f.maxArea(), DefectFilter::kNoLimit);
    QVERIFY(f.isPassThrough());

    f.setHeightRange(std::numeric_limits<float>::quiet_NaN(), 5.0f);
    QCOMPARE(f.minHeight(), DefectFilter::kNoLimit);
    QCOMPARE(f.maxHeight(), 5.0f);

    DefectPoint tall = makePoint(1, DefectSeverity::Level3, DefectType::Particle);
    tall.z = 4.0f;
    QVERIFY(f.accepts(tall));
    tall.z = 6.0f;
    QVERIFY(!f.accepts(tall));

    f.reset();
    QVERIFY(f.isPassThrough());
}

void TestDefectFilter::compareHeightAgainstRawZWhenLayerOffsetEnabled()
{
    // 「按层高度错开」会把层号叠加到渲染高度上，但筛选语义应与 CSV 的原始 Z 一致，
    // 否则用户会看到「按高度筛选」的结果随错开开关变化。
    DefectDataset ds;
    DefectPoint p = makePoint(1, DefectSeverity::Level3, DefectType::Particle);
    p.z = 0.5f;
    p.layer = 4;
    ds.points.push_back(p);

    DefectFilter f;
    f.setLayerOffsetEnabled(true);
    f.setLayerHeightUm(8.0f); // 渲染高度 = 0.5 + 32 = 32.5
    f.setHeightRange(0.0f, 1.0f);

    QVector<DefectPoint> out;
    QCOMPARE(f.apply(ds, &out), 1);          // 原始 Z=0.5 在区间内
    QVERIFY(qAbs(out.at(0).z - 32.5f) < 1e-4f); // 输出仍然是错开后的渲染高度

    f.setHeightRange(10.0f, 40.0f);          // 只在渲染高度范围内
    QCOMPARE(f.apply(ds, &out), 0);          // 原始 Z 不在区间 -> 过滤掉
}

QTEST_MAIN(TestDefectFilter)
#include "tst_defectfilter.moc"
