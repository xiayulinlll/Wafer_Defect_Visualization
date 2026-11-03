#version 330 core

// 缺陷标记点片元着色器：与热力图共用同一套伪彩色映射，视觉一致

in float vIntensity;
in float vSelected;

out vec4 FragColor;

vec3 heatmapColor(float t)
{
    t = clamp(t, 0.0, 1.0);

    vec3 green  = vec3(0.00, 0.85, 0.45);
    vec3 yellow = vec3(0.98, 0.82, 0.10);
    vec3 orange = vec3(1.00, 0.48, 0.05);
    vec3 red    = vec3(0.98, 0.10, 0.10);

    if (t < 0.35) {
        return mix(green, yellow, t / 0.35);
    } else if (t < 0.70) {
        return mix(yellow, orange, (t - 0.35) / 0.35);
    }
    return mix(orange, red, (t - 0.70) / 0.30);
}

void main()
{
    // 把方形 point 裁成圆形，并做软边抗锯齿
    vec2 pc = gl_PointCoord * 2.0 - 1.0;
    float d = length(pc);
    if (d > 1.0)
        discard;

    float soft = smoothstep(1.0, 0.72, d);

    // 标记点统一按等级伪彩色着色，与热力图 / 左侧等级图例使用同一色带，
    // 避免「图例说红色=致命，标记点却是青色」的观感割裂。
    vec3 base = heatmapColor(vIntensity);

    // 选中状态：描白圈 + 增亮
    vec3 color = base;
    if (vSelected > 0.5) {
        float ring = smoothstep(0.62, 0.95, d);
        color = mix(vec3(1.0, 1.0, 1.0), base, ring);
        color += vec3(0.25);
    }

    // 中心略亮，形成小高光球感
    color *= (1.15 - 0.35 * d);

    FragColor = vec4(color, soft);
}
