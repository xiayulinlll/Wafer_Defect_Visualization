#version 330 core

// 晶圆主体片元着色器：采样灰度热力纹理，并在 GPU 上完成伪彩色映射
// 难点4：CPU 只生成灰度纹理，颜色映射完全在片元着色器并行完成

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uHeatmap;      // 单通道灰度热力纹理 (R8)
uniform bool  uHeatmapEnabled;
uniform bool  uWireframe;
uniform float uHeatmapGain;      // 热力图强度增益
uniform vec3  uBaseColor;        // 晶圆本色
uniform vec3  uLightDir;         // 世界空间平行光方向（已归一化）
uniform vec3  uCameraPos;
uniform float uAlpha;

out vec4 FragColor;

/**
 * 伪彩色映射：绿(低) -> 黄(中) -> 红(高)
 * 与 DefectPresentation::severityColor 保持一致，便于 UI 图例对照。
 */
vec3 heatmapColor(float t)
{
    t = clamp(t, 0.0, 1.0);

    vec3 green  = vec3(0.00, 0.78, 0.47);
    vec3 yellow = vec3(0.94, 0.78, 0.12);
    vec3 orange = vec3(0.96, 0.47, 0.08);
    vec3 red    = vec3(0.90, 0.12, 0.12);

    if (t < 0.35) {
        return mix(green, yellow, t / 0.35);
    } else if (t < 0.70) {
        return mix(yellow, orange, (t - 0.35) / 0.35);
    }
    return mix(orange, red, (t - 0.70) / 0.30);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // 半兰伯特 + 高光，让晶圆表面有金属质感
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.28;
    vec3  H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.35;

    vec3 color = uBaseColor * (ambient + 0.72 * diff) + vec3(spec);

    if (uHeatmapEnabled) {
        float heat = texture(uHeatmap, vTexCoord).r;
        heat = clamp(heat * uHeatmapGain, 0.0, 1.0);

        // 只在顶面（N.y > 0）显著叠加，侧壁保留本色，避免视觉噪声
        float topMask = smoothstep(0.15, 0.75, N.y);
        float amount = heat * topMask;

        // 热力图越强，越"自发光"，弱化光照影响
        vec3 heatCol = heatmapColor(heat);
        color = mix(color, heatCol * (0.65 + 0.55 * heat), amount);
    }

    // Fresnel 边缘亮化，提升立体轮廓
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    color += vec3(0.10, 0.13, 0.18) * fres;

    if (uWireframe) {
        color = vec3(0.55, 0.72, 0.95);
    }

    FragColor = vec4(color, uAlpha);
}
