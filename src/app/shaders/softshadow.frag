// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ProjectHax LLC

// Two-layer soft shadow for a rounded box, from its signed distance field:
// a tight contact shadow plus a wide, offset ambient one. The item is the
// box grown by a margin on every side; the box itself sits at its centre.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 color;        // shadow colour, premultiplied (as Qt passes it)
    vec2 itemSize;     // this item, in px
    vec2 boxSize;      // the casting box, in px
    float radius;      // box corner radius, px
    float blur;        // ambient softness, px
    float offsetY;     // ambient vertical offset, px
    float strength;    // 0..1, scales both layers (hover lifts it)
};

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main()
{
    vec2 p = qt_TexCoord0 * itemSize - itemSize * 0.5;
    vec2 halfSize = boxSize * 0.5;

    float contact = roundedBox(p - vec2(0.0, 1.0), halfSize, radius);
    float contactA = 1.0 - smoothstep(-1.0, 3.0, contact);

    float ambient = roundedBox(p - vec2(0.0, offsetY), halfSize - vec2(blur * 0.25), radius);
    float t = clamp(ambient / max(blur, 1.0), 0.0, 1.0);
    float ambientA = (1.0 - t) * (1.0 - t) * (1.0 - t);

    float a = clamp(contactA * 0.35 + ambientA * 0.65, 0.0, 1.0) * strength;
    fragColor = color * a * qt_Opacity;
}
