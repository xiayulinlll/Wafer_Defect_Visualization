#include "business/GeometryBuilder.h"
#include "core/Constants.h"

#include <QTest>

using namespace wafer;

class TestGeometry : public QObject
{
    Q_OBJECT

private slots:
    void markerRadiusGrowsWithAreaAndIsClamped();
    void mapsDefectToWorldWithinUnitRadius();
    void buildsOneMarkerPerDefect();
    void buildsWaferDiskWithValidIndices();
    void validateWaferDiskAcceptsBuiltMesh();
    void validatesMarkersWorldPositionsOneToOne();
    void buildsGroundGridAndAxes();

private:
    static DefectPoint makeDefect(int id, float x, float y, float z, float area);
};

DefectPoint TestGeometry::makeDefect(int id, float x, float y, float z, float area)
{
    DefectPoint p;
    p.id = id;
    p.x = x;
    p.y = y;
    p.z = z;
    p.area = area;
    return p;
}

void TestGeometry::markerRadiusGrowsWithAreaAndIsClamped()
{
    const float small = constants::markerRadiusFor(0.0f);
    const float mid = constants::markerRadiusFor(60.0f);
    const float huge = constants::markerRadiusFor(100000.0f);

    QVERIFY(qFuzzyCompare(small, constants::kMarkerBaseRadius));
    QVERIFY(mid > small);
    QVERIFY(huge >= mid);
    // 上限 = 基础半径 + 最大增量
    QVERIFY(qFuzzyCompare(huge,
                          constants::kMarkerBaseRadius + constants::kMarkerAreaRadiusScale));
}

void TestGeometry::mapsDefectToWorldWithinUnitRadius()
{
    QVector3D world;
    // 数据空间半径 150，点在半径 150 处 -> 世界坐标应归一化到 1.0
    GeometryBuilder::mapDefectToWorld(makeDefect(1, 150.0f, 0.0f, 0.0f, 1.0f), 150.0f, 1.0f,
                                      &world);
    QVERIFY(qFuzzyCompare(world.x(), constants::kWorldExtent));
    QVERIFY(qFuzzyCompare(world.y(), 0.0f));
    QVERIFY(qFuzzyCompare(world.z(), 0.0f));

    // 数据 Y 映射到世界 -Z（俯视观感：+Y 朝屏幕上方）
    GeometryBuilder::mapDefectToWorld(makeDefect(3, 0.0f, 150.0f, 0.0f, 1.0f), 150.0f, 1.0f,
                                      &world);
    QVERIFY(qFuzzyCompare(world.z(), -constants::kWorldExtent));

    // 数据 Z（高度，单位 um）映射到世界 Y，并乘 kZUnitScale * zScale
    GeometryBuilder::mapDefectToWorld(makeDefect(2, 0.0f, 0.0f, 100.0f, 1.0f), 150.0f, 1.0f,
                                      &world);
    QVERIFY(qFuzzyCompare(world.y(), 100.0f * constants::kZUnitScale));

    // zScale 应线性作用于高度轴
    GeometryBuilder::mapDefectToWorld(makeDefect(4, 0.0f, 0.0f, 100.0f, 1.0f), 150.0f, 2.0f,
                                      &world);
    QVERIFY(qFuzzyCompare(world.y(), 200.0f * constants::kZUnitScale));
}

void TestGeometry::buildsOneMarkerPerDefect()
{
    QVector<DefectPoint> defects;
    for (int i = 0; i < 25; ++i)
        defects.push_back(makeDefect(i, float(i), float(-i), 0.5f, float(i * 4)));

    QVector<MarkerVertex> markers;
    GeometryBuilder::buildDefectMarkers(defects, 150.0f, 1.0f, &markers);

    QCOMPARE(markers.size(), defects.size());
    // 半径必须为正值，才能被 OpenGL 点渲染出来
    for (const MarkerVertex &m : markers)
        QVERIFY(m.radius > 0.0f);
}

void TestGeometry::buildsWaferDiskWithValidIndices()
{
    QVector<WaferVertex> vertices;
    QVector<unsigned int> indices;
    GeometryBuilder::buildWaferDisk(150.0f, constants::kWaferSegments,
                                    constants::kWaferThickness, &vertices, &indices);

    QVERIFY(!vertices.isEmpty());
    QVERIFY(!indices.isEmpty());
    QVERIFY(indices.size() % 3 == 0); // 三角形列表

    for (unsigned int idx : indices)
        QVERIFY(idx < static_cast<unsigned int>(vertices.size()));
}

void TestGeometry::validateWaferDiskAcceptsBuiltMesh()
{
    // 绕序与法线必须自洽：错误绕序的三角形会被 glCullFace(GL_BACK) 整片剔除
    // （表现为「从下往上看晶圆消失」或侧壁透空）。
    QString error;
    QVERIFY2(GeometryBuilder::validateWaferDisk(150.0f, constants::kWaferSegments,
                                                constants::kWaferThickness, &error),
             qPrintable(error));

    // 分段数低于下限时会被钳位，仍需通过校验
    error.clear();
    QVERIFY2(GeometryBuilder::validateWaferDisk(10.0f, 3, constants::kWaferThickness, &error),
             qPrintable(error));
}

void TestGeometry::validatesMarkersWorldPositionsOneToOne()
{
    // worldPositions 与 markers 必须一一对应（拾取直接复用这份缓存）
    QVector<DefectPoint> defects;
    defects.push_back(makeDefect(1, 0.0f, 0.0f, 0.0f, 1.0f));
    defects.push_back(makeDefect(2, 75.0f, 0.0f, 10.0f, 4.0f));

    QVector<MarkerVertex> markers;
    QVector<QVector3D> worldPositions;
    GeometryBuilder::buildDefectMarkers(defects, 150.0f, 1.0f, &markers, &worldPositions);

    QCOMPARE(markers.size(), defects.size());
    QCOMPARE(worldPositions.size(), defects.size());
    for (int i = 0; i < markers.size(); ++i) {
        QVERIFY(qFuzzyCompare(markers[i].px, worldPositions[i].x()));
        QVERIFY(qFuzzyCompare(markers[i].py, worldPositions[i].y()));
        QVERIFY(qFuzzyCompare(markers[i].pz, worldPositions[i].z()));
    }
}

void TestGeometry::buildsGroundGridAndAxes()
{
    QVector<LineVertex> grid;
    GeometryBuilder::buildGroundGrid(constants::kGridHalfExtent, constants::kGridDivisions, &grid);
    QVERIFY(!grid.isEmpty());
    QVERIFY(grid.size() % 2 == 0); // LineList

    QVector<LineVertex> axes;
    GeometryBuilder::buildAxes(1.0f, &axes);
    QVERIFY(axes.size() >= 6); // 三根轴 = 6 个端点

    QVector<LineVertex> outline;
    GeometryBuilder::buildWaferOutline(1.0f, constants::kWaferSegments,
                                       QVector3D(1.0f, 1.0f, 1.0f), &outline);
    QVERIFY(!outline.isEmpty());
    QVERIFY(outline.size() % 2 == 0);
}

QTEST_MAIN(TestGeometry)
#include "tst_geometry.moc"
