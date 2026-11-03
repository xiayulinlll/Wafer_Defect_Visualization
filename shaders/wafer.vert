#version 330 core

// 晶圆主体顶点着色器
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);

    vWorldPos = worldPos.xyz;
    vNormal   = normalize(uNormalMatrix * aNormal);
    // 晶圆本体用固定 UV 采样缺陷热力纹理（与数据平面 XY 对齐）
    vTexCoord = aTexCoord;

    gl_Position = uProjection * uView * worldPos;
}
