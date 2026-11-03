#include "data/DataBufferManager.h"

#include <QTest>

#include <thread>
#include <vector>

using namespace wafer;

class TestDataBuffer : public QObject
{
    Q_OBJECT

private slots:
    void publishesAndSnapshots();
    void versionIncrementsOnEachPublish();
    void snapshotIsStableAcrossPublishes();
    void clearResetsCount();

private:
    static DefectDataset makeDataset(int pointCount, int idBase);
};

DefectDataset TestDataBuffer::makeDataset(int pointCount, int idBase)
{
    DefectDataset ds;
    ds.points.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i) {
        DefectPoint p;
        p.id = idBase + i;
        p.x = float(i);
        p.y = float(-i);
        ds.points.push_back(p);
    }
    return ds;
}

void TestDataBuffer::publishesAndSnapshots()
{
    DataBufferManager mgr;
    QVERIFY(mgr.snapshot().isNull());
    QCOMPARE(mgr.pointCount(), 0);

    mgr.publish(makeDataset(10, 100));
    auto snap = mgr.snapshot();
    QVERIFY(!snap.isNull());
    QCOMPARE(snap->points.size(), 10);
    QCOMPARE(mgr.pointCount(), 10);
}

void TestDataBuffer::versionIncrementsOnEachPublish()
{
    DataBufferManager mgr;
    QCOMPARE(mgr.version(), 0);
    mgr.publish(makeDataset(1, 0));
    QCOMPARE(mgr.version(), 1);
    mgr.publish(makeDataset(1, 1));
    QCOMPARE(mgr.version(), 2);
}

void TestDataBuffer::snapshotIsStableAcrossPublishes()
{
    DataBufferManager mgr;
    mgr.publish(makeDataset(5, 0));

    // 消费者持有旧快照，生产者发布新数据 -> 旧快照必须保持不变（双缓冲语义）
    auto oldSnap = mgr.snapshot();
    mgr.publish(makeDataset(1000, 5000));

    QCOMPARE(oldSnap->points.size(), 5);
    QCOMPARE(oldSnap->points[0].id, 0);

    auto newSnap = mgr.snapshot();
    QCOMPARE(newSnap->points.size(), 1000);
    QCOMPARE(newSnap->points[0].id, 5000);
}

void TestDataBuffer::clearResetsCount()
{
    DataBufferManager mgr;
    mgr.publish(makeDataset(7, 0));
    QCOMPARE(mgr.pointCount(), 7);
    mgr.clear();
    QCOMPARE(mgr.pointCount(), 0);
    QVERIFY(mgr.snapshot().isNull());
}

QTEST_MAIN(TestDataBuffer)
#include "tst_databuffer.moc"
