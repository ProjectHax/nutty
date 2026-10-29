// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

// Liquid fill for the battery: two travelling waves (a dim one behind, a
// bright one in front) up to `level`, a vertical gradient and a faint line
// along the surface, all clipped to a rounded rectangle.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 colorTop;
    vec4 colorBottom;
    vec2 itemSize;   // px
    float level;     // 0..1
    float phase;     // radians, animated
    float amplitude; // px
    float radius;    // container corner radius, px
};

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main()
{
    vec2 px = qt_TexCoord0 * itemSize;
    float h = max(itemSize.y, 1.0);
    float y = itemSize.y - px.y;            // distance from the bottom
    float surface = clamp(level, 0.0, 1.0) * h;

    // Calm the waves near empty and full so the liquid doesn't spill.
    float calm = clamp(min(level, 1.0 - level) * 10.0, 0.0, 1.0);
    float amp = amplitude * calm;
    float k = 6.2831853 / max(itemSize.x, 1.0);

    float front = surface + amp * sin(px.x * k * 1.25 + phase)
                          + amp * 0.35 * sin(px.x * k * 2.7 - phase * 1.6);
    float back = surface + amp * 0.9 * sin(px.x * k * 1.05 - phase * 0.75 + 2.1);

    float inFront = clamp(front - y + 0.5, 0.0, 1.0);
    float inBack = clamp(back - y + 0.5, 0.0, 1.0);

    float t = clamp(y / max(surface, 1.0), 0.0, 1.0);
    vec4 base = mix(colorBottom, colorTop, t);

    // A faint line along the surface.
    float depth = front - y;
    float line = clamp(1.0 - abs(depth - 1.0) / 1.5, 0.0, 1.0) * 0.12;
    vec3 frontRgb = min(base.rgb + vec3(line), vec3(1.0));
    vec3 backRgb = base.rgb * 0.72;

    float alpha = max(inFront, inBack * 0.55) * base.a;
    vec3 rgb = mix(backRgb, frontRgb, inFront);

    float d = roundedBox(px - itemSize * 0.5, itemSize * 0.5, radius);
    float mask = clamp(0.5 - d, 0.0, 1.0);

    float a = alpha * mask;
    fragColor = vec4(rgb * a, a) * qt_Opacity;
}
