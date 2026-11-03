#version 330 core

// 将 FBO 颜色附件纹理拷贝到默认帧缓冲，可附带后处理

in vec2 vTexCoord;

uniform sampler2D uSource;
uniform bool  uFlipY;      // FBO 纹理与屏幕方向相反时翻转
uniform bool  uGrayscale;
uniform float uExposure;

out vec4 FragColor;

void main()
{
    vec2 uv = uFlipY ? vec2(vTexCoord.x, 1.0 - vTexCoord.y) : vTexCoord;
    vec4 c = texture(uSource, uv);

    c.rgb *= uExposure;

    if (uGrayscale) {
        float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
        c.rgb = vec3(g);
    }

    FragColor = vec4(c.rgb, 1.0);
}
