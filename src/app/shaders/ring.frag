// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

// An antialiased arc gauge with round caps: a track over `sweep` radians from
// `startAngle` (0 = 12 o'clock, clockwise), and a progress arc over
// `progress` of it, coloured from colorStart to colorEnd along its length.
// Qt hands colours to shaders premultiplied.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 trackColor;
    vec4 colorStart;
    vec4 colorEnd;
    vec2 itemSize;    // px
    float thickness;  // px
    float progress;   // 0..1
    float sweep;      // radians
    float startAngle; // radians
};

const float TAU = 6.2831853;

float angleOf(vec2 p, float a0)
{
    return mod(atan(p.x, -p.y) - a0 + 2.0 * TAU, TAU);
}

float arcDistance(vec2 p, float r, float a0, float len)
{
    float a = angleOf(p, a0);
    if (a <= len)
        return abs(length(p) - r);
    vec2 e0 = r * vec2(sin(a0), -cos(a0));
    vec2 e1 = r * vec2(sin(a0 + len), -cos(a0 + len));
    return min(length(p - e0), length(p - e1));
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize - itemSize * 0.5;
    float r = min(itemSize.x, itemSize.y) * 0.5 - thickness * 0.5 - 1.0;
    float halfThickness = thickness * 0.5;

    float trackD = arcDistance(p, r, startAngle, sweep) - halfThickness;
    float trackCover = clamp(0.5 - trackD, 0.0, 1.0);

    float len = sweep * clamp(progress, 0.0, 1.0);
    float progCover = 0.0;
    vec4 progColor = colorStart;
    if (len > 0.0005) {
        float progD = arcDistance(p, r, startAngle, len) - halfThickness;
        progCover = clamp(0.5 - progD, 0.0, 1.0);
        // Inside the arc: position along it. In a cap: 0 at the start, 1 at the end.
        float a = angleOf(p, startAngle);
        float along = a <= len ? a / max(len, 0.0001) : (a > len + (TAU - len) * 0.5 ? 0.0 : 1.0);
        progColor = mix(colorStart, colorEnd, along);
    }

    // Progress over track ("source over", premultiplied).
    vec4 prog = progColor * progCover;
    vec4 track = trackColor * trackCover;
    fragColor = (prog + track * (1.0 - prog.a)) * qt_Opacity;
}
