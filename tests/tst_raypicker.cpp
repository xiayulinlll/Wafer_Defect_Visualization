#include "business/RayPicker.h"

#include <QTest>

#include <cmath>

using namespace wafer;

class TestRayPicker : public QObject
{
    Q_OBJECT

private slots:
    void picksNearestDefectAlongRay();
    void missesWhenRayPointsAway();
    void pickFromWorldPositionsMatchesIndex();
    void worldPickRadiusScalesWithDepth();
    void pickWorksAfterCameraZoomOut();
    void computeBoundsCoversAllPoints();
    void computeBoundsOfEmptySetFallsBackToDefaults();
    void gridPickMatchesLinearScan();
    void gridPickMatchesLinearScanWhenRadiusExceedsCell();

private:
    static DefectPoint makeDefect(int id, float x, float y, float z);
    /** 构造一批世界坐标 + 对应缺陷（散布在晶圆平面上） */
    static void buildScatter(int count, QVector<DefectPoint> *defects,
                             QVector<QVector3D> *worldPositions);
};

DefectPoint TestRayPicker::makeDefect(int id, float x, float y, float z)
{
    DefectPoint p;
    p.id = id;
    p.x = x;
    p.y = y;
    p.z = z;
    p.area = 20.0f;
    return p;
}

void TestRayPicker::picksNearestDefectAlongRay()
{
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(7, 0.0f, 0.0f, 0.0f));
    defects.push_back(makeDefect(8, 60.0f, 0.0f, 0.0f));

    // 射线沿 -Z 看向原点（世界坐标下缺陷 7 正在原点）
    math::Ray ray;
    ray.origin = QVector3D(0.0f, 0.0f, 3.0f);
    ray.direction = QVector3D(0.0f, 0.0f, -1.0f);

    const PickResult r = RayPicker::pick(ray, defects, 150.0f, 1.0f, PickParams{});
    QVERIFY(r.hit);
    QCOMPARE(r.defectId, 7);
    QCOMPARE(r.index, 0);
    QVERIFY(r.distance > 0.0f);
}

void TestRayPicker::missesWhenRayPointsAway()
{
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(7, 0.0f, 0.0f, 0.0f));

    math::Ray ray;
    ray.origin = QVector3D(0.0f, 0.0f, 3.0f);
    ray.direction = QVector3D(0.0f, 0.0f, 1.0f); // 背向缺陷

    const PickResult r = RayPicker::pick(ray, defects, 150.0f, 1.0f, PickParams{});
    QVERIFY(!r.hit);
    QCOMPARE(r.defectId, -1);
    QCOMPARE(r.index, -1);
}

void TestRayPicker::pickFromWorldPositionsMatchesIndex()
{
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(1, 0.0f, 0.0f, 0.0f));
    defects.push_back(makeDefect(2, 0.0f, 0.0f, 0.0f));

    QVector<QVector3D> worldPositions;
    worldPositions.push_back(QVector3D(0.8f, 0.0f, 0.0f));
    worldPositions.push_back(QVector3D(-0.8f, 0.0f, 0.0f));

    math::Ray ray;
    ray.origin = QVector3D(-0.8f, 0.0f, 3.0f);
    ray.direction = QVector3D(0.0f, 0.0f, -1.0f);

    const PickResult r =
        RayPicker::pickFromWorldPositions(ray, worldPositions, defects, PickParams{});
    QVERIFY(r.hit);
    QCOMPARE(r.index, 1);
    QCOMPARE(r.defectId, 2);
}

void TestRayPicker::worldPickRadiusScalesWithDepth()
{
    // 标记点是固定像素大小的点精灵：同样的屏幕覆盖在不同深度应换算成
    // 不同大小的世界半径（正比于视深）。固定世界容差会在相机拉远后「点不中」。
    const DefectPoint defect = makeDefect(1, 0.0f, 0.0f, 0.0f);
    const PickParams params;

    math::Ray nearRay;
    nearRay.origin = QVector3D(0.0f, 0.0f, 2.0f);
    nearRay.direction = QVector3D(0.0f, 0.0f, -1.0f);

    math::Ray farRay;
    farRay.origin = QVector3D(0.0f, 0.0f, 8.0f);
    farRay.direction = QVector3D(0.0f, 0.0f, -1.0f);

    const float rNear = RayPicker::worldPickRadius(defect, QVector3D(0, 0, 0), nearRay, params);
    const float rFar = RayPicker::worldPickRadius(defect, QVector3D(0, 0, 0), farRay, params);

    QVERIFY(rNear > 0.0f);
    QVERIFY(rFar > rNear);
    // 深度比 4:1 => 世界半径也应近似 4:1（像素尺寸与容差相同）
    QVERIFY(qAbs(rFar / rNear - 4.0f) < 0.001f);
}

void TestRayPicker::pickWorksAfterCameraZoomOut()
{
    // 相机拉远后，离点的屏幕中心稍偏一点仍应能命中（容差按像素换算）。
    // 世界半径 0.01 在深度 10 处只覆盖约 1~2 像素，这正是旧实现漏判的场景。
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(1, 0.0f, 0.0f, 0.0f));

    QVector<QVector3D> worldPositions;
    worldPositions.push_back(QVector3D(0.0f, 0.0f, 0.0f));

    PickParams params;
    const float halfView = 0.01f; // 世界空间横向偏移（约 1~2 像素）
    math::Ray ray;
    ray.origin = QVector3D(halfView, 0.0f, 10.0f);
    ray.direction = QVector3D(0.0f, 0.0f, -1.0f);

    const PickResult r =
        RayPicker::pickFromWorldPositions(ray, worldPositions, defects, params);
    QVERIFY(r.hit);
    QCOMPARE(r.defectId, 1);
}

void TestRayPicker::computeBoundsCoversAllPoints()
{
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(1, -150.0f, 0.0f, 0.0f));
    defects.push_back(makeDefect(2, 150.0f, 0.0f, 0.0f));
    defects.push_back(makeDefect(3, 0.0f, 150.0f, 0.0f));

    const math::Aabb box = RayPicker::computeBounds(defects, 150.0f, 1.0f);
    QVERIFY(box.isValid());
    // 数据 X -> 世界 X：-150 -> -1.0，+150 -> +1.0
    QVERIFY(box.min.x() <= -1.0f);
    QVERIFY(box.max.x() >= 1.0f);
    // 数据 Y -> 世界 -Z：y=150 -> z=-1.0（世界 Y 恒为 0，因为所有点高度 z=0）
    QVERIFY(box.min.z() <= -1.0f);
    QVERIFY(qFuzzyCompare(box.min.y(), 0.0f));
    QVERIFY(qFuzzyCompare(box.max.y(), 0.0f));
}

void TestRayPicker::computeBoundsOfEmptySetFallsBackToDefaults()
{
    // 空集合：不应返回「未初始化」包围盒，而是回退到默认 1.0 世界单位包围盒，
    // 保证相机适配等下游逻辑始终拿到可用的边界。
    const math::Aabb box = RayPicker::computeBounds({}, 150.0f, 1.0f);
    QVERIFY(box.isValid());
    QCOMPARE(box.min, QVector3D(-1.0f, 0.0f, -1.0f));
    QCOMPARE(box.max, QVector3D(1.0f, 0.0f, 1.0f));
}

void TestRayPicker::buildScatter(int count, QVector<DefectPoint> *defects,
                                 QVector<QVector3D> *worldPositions)
{
    // 用确定性的伪随机（LCG）散布，保证测试可重复且不依赖 Qt 随机数实现
    quint32 seed = 12345u;
    auto nextUnit = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24);
    };

    for (int i = 0; i < count; ++i) {
        const float x = (nextUnit() * 2.0f - 1.0f) * 0.95f;
        const float z = (nextUnit() * 2.0f - 1.0f) * 0.95f;
        const float y = 0.002f * static_cast<float>(i % 7);

        DefectPoint p = makeDefect(i + 1, x, z, 0.0f);
        p.area = 1.0f + 40.0f * nextUnit();
        p.severity = static_cast<DefectSeverity>(1 + (i % 5));
        defects->push_back(p);
        worldPositions->push_back(QVector3D(x, y, z));
    }
}

void TestRayPicker::gridPickMatchesLinearScan()
{
    QVector<DefectPoint> defects;
    QVector<QVector3D> worldPositions;
    buildScatter(4000, &defects, &worldPositions);

    PickGrid grid;
    grid.build(worldPositions, defects);
    QCOMPARE(grid.size(), defects.size());
    QVERIFY(!grid.isEmpty());

    PickParams params;
    params.maxPickRadius = 0.1f;

    // 用多组不同角度/位置的射线逐条比对：结果必须与线性扫描完全一致
    int compared = 0;
    int hits = 0;
    for (int a = 0; a < 24; ++a) {
        const float angle = static_cast<float>(a) * 3.14159265f / 12.0f;
        const QVector3D origin(std::cos(angle) * 3.2f, 2.2f, std::sin(angle) * 3.2f);
        for (int s = -3; s <= 3; ++s) {
            const QVector3D target(0.02f * static_cast<float>(s), 0.0f, -0.02f * static_cast<float>(s));
            math::Ray ray;
            ray.origin = origin;
            ray.direction = (target - origin).normalized();

            const PickResult linear =
                RayPicker::pickFromWorldPositions(ray, worldPositions, defects, params);
            const PickResult indexed =
                RayPicker::pickWithGrid(ray, worldPositions, defects, params, &grid);

            QCOMPARE(indexed.hit, linear.hit);
            QCOMPARE(indexed.index, linear.index);
            QCOMPARE(indexed.defectId, linear.defectId);
            QVERIFY(qAbs(indexed.distance - linear.distance) < 1e-5f);
            ++compared;
            if (linear.hit)
                ++hits;
        }
    }
    QVERIFY(compared > 100);
    QVERIFY(hits > 0); // 确保确实覆盖到了命中的情况

    // 空索引/点数不匹配时必须回退线性扫描，而不是漏点
    const PickResult fallback = RayPicker::pickWithGrid(
        math::Ray{QVector3D(0, 3, 0), QVector3D(0, -1, 0)}, worldPositions, defects, params,
        nullptr);
    const PickResult linearFallback = RayPicker::pickFromWorldPositions(
        math::Ray{QVector3D(0, 3, 0), QVector3D(0, -1, 0)}, worldPositions, defects, params);
    QCOMPARE(fallback.index, linearFallback.index);
}

void TestRayPicker::gridPickMatchesLinearScanWhenRadiusExceedsCell()
{
    // 相机贴近时拾取半径（世界单位）会远大于格子边长：
    // 索引必须向外扩圈收集候选，否则会「看得见的点却漏掉」。
    QVector<DefectPoint> defects;
    QVector<QVector3D> worldPositions;
    buildScatter(2000, &defects, &worldPositions);

    PickGrid grid;
    grid.build(worldPositions, defects);

    PickParams params;
    params.maxPickRadius = 0.3f; // 约为格子边长的数十倍
    QVERIFY(params.maxPickRadius > grid.cellSize());

    math::Ray ray;
    ray.origin = QVector3D(0.0f, 0.4f, 0.0f);
    ray.direction = QVector3D(0.0f, -1.0f, 0.0f); // 垂直俯视

    const PickResult linear = RayPicker::pickFromWorldPositions(ray, worldPositions, defects, params);
    const PickResult indexed =
        RayPicker::pickWithGrid(ray, worldPositions, defects, params, &grid);
    QCOMPARE(indexed.hit, linear.hit);
    QCOMPARE(indexed.index, linear.index);
    QVERIFY(qAbs(indexed.distance - linear.distance) < 1e-5f);
}

QTEST_MAIN(TestRayPicker)
#include "tst_raypicker.moc"
