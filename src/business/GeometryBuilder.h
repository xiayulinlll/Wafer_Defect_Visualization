#ifndef GEOMETRYBUILDER_H
#define GEOMETRYBUILDER_H

#include "core/DefectTypes.h"
#include "core/MathUtils.h"

#include <QVector>

namespace wafer {

/** 顶点结构：位置 + 法线 + 热力图 UV + 缺陷等级灰度 */
struct WaferVertex {
    float px, py, pz; // 位置
    float nx, ny, nz; // 法线
    float u, v;       // 热力图纹理坐标（0~1，与晶圆包围盒对齐）
};

/** 缺陷标记点顶点：位置 + 等级灰度 + 半径（用于几何着色/点大小） */
struct MarkerVertex {
    float px, py, pz;
    float intensity; // 0~1
    float radius;    // 世界空间半径
    float padding = 0.0f;
};

/** 简单顶点（网格/坐标轴） */
struct LineVertex {
    float px, py, pz;
    float r, g, b;
};

/**
 * @brief 几何生成器（业务层，纯 CPU）。
 *
 * 负责把业务数据转换为顶点数组，交由渲染层上传 GPU。
 * 不接触任何 OpenGL 对象。
 */
class GeometryBuilder
{
public:
    /**
     * @brief 生成晶圆圆盘网格（顶面 + 底面的三角化扇形，含侧壁）。
     * @param radius   晶圆半径 (mm)
     * @param segments 圆周分段数
     * @param thickness 晶圆厚度（世界单位）
     */
    static void buildWaferDisk(float radius, int segments, float thickness,
                               QVector<WaferVertex> *vertices, QVector<unsigned int> *indices);

    /**
     * @brief 校核晶圆圆盘网格：索引合法、绕序与法线一致（顶面 CCW / 底面与侧壁朝外）。
     *
     * 绕序错误会被 `glCullFace(GL_BACK)` 静默剔除（表现为「从下往上看晶圆消失」或
     * 侧壁透空），在单元测试中显式校验可以避免这类回归。
     * @return 全部检查通过返回 true；失败时 errorMessage 给出第一个不合规三角形
     */
    static bool validateWaferDisk(float radius, int segments, float thickness,
                                  QString *errorMessage = nullptr);

    /**
     * @brief 生成缺陷标记点顶点（难点2：全部打包进单个 VBO 批量绘制）。
     * @param defects 过滤后的缺陷点
     * @param radius  晶圆半径，用于归一化 XY 到 [-1,1] 世界坐标
     * @param zScale  Z 方向放大系数
     * @param out     输出顶点
     * @param worldPositions 可选：同步输出每个缺陷的世界坐标（与 out 一一对应），
     *                    供拾取复用，避免渲染层再逐点映射一次
     */
    static void buildDefectMarkers(const QVector<DefectPoint> &defects, float radius, float zScale,
                                   QVector<MarkerVertex> *out,
                                   QVector<QVector3D> *worldPositions = nullptr);

    /** 把缺陷的 XY/Z 映射到世界坐标（与晶圆网格保持同一映射） */
    static void mapDefectToWorld(const DefectPoint &p, float radius, float zScale,
                                 QVector3D *worldPos);

    /** 生成 XY 平面参考网格线（LineList） */
    static void buildGroundGrid(float halfExtent, int divisions, QVector<LineVertex> *out);

    /** 生成世界坐标轴（LineList，红X 绿Y 蓝Z） */
    static void buildAxes(float length, QVector<LineVertex> *out);

    /** 生成晶圆轮廓圆环线（LineList），用于强调边界 */
    static void buildWaferOutline(float radius, int segments, const QVector3D &color,
                                  QVector<LineVertex> *out);
};

} // namespace wafer

#endif // GEOMETRYBUILDER_H
