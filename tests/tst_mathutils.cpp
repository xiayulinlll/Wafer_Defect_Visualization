#include "core/MathUtils.h"

#include <QTest>

using namespace wafer;
using namespace wafer::math;

class TestMathUtils : public QObject
{
    Q_OBJECT

private slots:
    void degToRadConverts();
    void clampfBounds();
    void aabbExpandAndCenter();
    void rayIntersectsAabbHitAndMiss();
    void rayIntersectsSphereHitAndMiss();
    void screenPointToRayPointsIntoScene();
};

void TestMathUtils::degToRadConverts()
{
    QVERIFY(qFuzzyCompare(degToRad(0.0f), 0.0f));
    QVERIFY(qFuzzyCompare(degToRad(180.0f), kPi));
    QVERIFY(qFuzzyCompare(degToRad(90.0f), kPi * 0.5f));
}

void TestMathUtils::clampfBounds()
{
    QCOMPARE(clampf(5.0f, 0.0f, 10.0f), 5.0f);
    QCOMPARE(clampf(-3.0f, 0.0f, 10.0f), 0.0f);
    QCOMPARE(clampf(42.0f, 0.0f, 10.0f), 10.0f);
}

void TestMathUtils::aabbExpandAndCenter()
{
    // 默认构造的 Aabb：min == max == (0,0,0)，按 isValid() 定义（max >= min）视为有效；
    // 只有调用 reset() 后（min=+inf, max=-inf）才是「无效」状态。
    Aabb empty;
    QVERIFY(empty.isValid());

    Aabb box;
    box.reset(); // 进入无效状态，等待 expand()
    QVERIFY(!box.isValid());

    box.expand(QVector3D(-1.0f, -2.0f, -3.0f));
    box.expand(QVector3D(1.0f, 2.0f, 3.0f));

    QVERIFY(box.isValid());
    QCOMPARE(box.center(), QVector3D(0.0f, 0.0f, 0.0f));
    QCOMPARE(box.extent(), QVector3D(2.0f, 4.0f, 6.0f));
}

void TestMathUtils::rayIntersectsAabbHitAndMiss()
{
    Aabb box;
    box.reset();
    box.expand(QVector3D(-1.0f, -1.0f, -1.0f));
    box.expand(QVector3D(1.0f, 1.0f, 1.0f));

    Ray hit;
    hit.origin = QVector3D(0.0f, 0.0f, 5.0f);
    hit.direction = QVector3D(0.0f, 0.0f, -1.0f);
    float t = -1.0f;
    QVERIFY(rayIntersectsAabb(hit, box, &t));
    QVERIFY(qFuzzyCompare(t, 4.0f));

    Ray miss;
    miss.origin = QVector3D(10.0f, 10.0f, 5.0f);
    miss.direction = QVector3D(0.0f, 0.0f, -1.0f);
    QVERIFY(!rayIntersectsAabb(miss, box, nullptr));
}

void TestMathUtils::rayIntersectsSphereHitAndMiss()
{
    Ray ray;
    ray.origin = QVector3D(0.0f, 0.0f, 5.0f);
    ray.direction = QVector3D(0.0f, 0.0f, -1.0f);

    float t = -1.0f;
    QVERIFY(rayIntersectsSphere(ray, QVector3D(0.0f, 0.0f, 0.0f), 1.0f, &t));
    QVERIFY(qFuzzyCompare(t, 4.0f));

    QVERIFY(!rayIntersectsSphere(ray, QVector3D(10.0f, 0.0f, 0.0f), 1.0f, nullptr));
}

void TestMathUtils::screenPointToRayPointsIntoScene()
{
    QMatrix4x4 view;
    view.lookAt(QVector3D(0.0f, 0.0f, 5.0f), QVector3D(0.0f, 0.0f, 0.0f),
                QVector3D(0.0f, 1.0f, 0.0f));
    QMatrix4x4 proj;
    proj.perspective(45.0f, 1.0f, 0.1f, 100.0f);

    // 屏幕中心（视口 100x100）应得到一条大致指向原点的射线
    const Ray ray = screenPointToRay(50.0f, 50.0f, 100, 100, view, proj);
    QVERIFY(!ray.direction.isNull());
    QVERIFY(ray.direction.z() < 0.0f);
    QVERIFY(qFuzzyCompare(ray.direction.length(), 1.0f));
}

QTEST_MAIN(TestMathUtils)
#include "tst_mathutils.moc"
