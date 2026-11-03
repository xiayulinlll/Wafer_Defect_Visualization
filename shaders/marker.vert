#version 330 core

// 缺陷标记点顶点着色器
// 难点2：所有缺陷点打包进单个 VBO，一次 glDrawArrays(GL_POINTS) 批量绘制
//
// 尺寸约定（与 C++ 侧 RayPicker 严格一致，见 constants::markerPixelSize）：
//   直径 = uPixelScale * (0.65 + 0.9 * intensity) * (0.7 + 14 * radius)
// 其中 uPixelScale 已经包含「基础像素尺寸 × 视口高度缩放 × 选中放大」，
// 由 SceneRenderer 每帧计算，避免着色器里出现第二份常数。

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aIntensity; // 缺陷等级灰度 0~1
layout(location = 2) in float aRadius;    // 面积归一化值 [0,1]（不是世界半径）

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uPixelScale;   // 基础像素尺寸（含视口/选中缩放）
uniform float uCamDistance;  // 相机到焦点距离（世界单位），用于透视衰减归一化
uniform float uSelectedIndex; // 选中缺陷的顶点下标（>= 0 时高亮，-1 表示无）

out float vIntensity;
out float vSelected;

void main()
{
    // 世界坐标已由 CPU 映射完成，uModel 保持单位阵（预留扩展）
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);

    // 点尺寸随缺陷等级/面积变化，并做透视衰减。
    // 用 uCamDistance 归一化，使衰减与场景尺度无关：
    // 物体在焦点处时 persp = 1，靠近相机变大、远离变小。
    float persp = clamp(uCamDistance / max(gl_Position.w, 1e-4), 0.35, 1.6);
    float size = uPixelScale * (0.65 + 0.9 * aIntensity) * (0.7 + 14.0 * aRadius) * persp;
    gl_PointSize = clamp(size, 2.0, 128.0);

    vIntensity = aIntensity;
    vSelected  = (abs(float(gl_VertexID) - uSelectedIndex) < 0.5) ? 1.0 : 0.0;
}
