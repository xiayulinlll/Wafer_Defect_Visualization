#version 330 core

// FBO 离屏渲染 / 后处理全屏三角形顶点着色器
// 使用 gl_VertexID 直接生成全屏三角形，无需 VBO

out vec2 vTexCoord;

void main()
{
    // 三个顶点覆盖整个 NDC 空间
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
