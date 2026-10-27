#version 450

layout(binding = 0) uniform sampler2D arrowMask;
layout(push_constant) uniform ArrowGlow
{
    vec4 viewportCenter;
    vec4 shape;
    vec4 clip;
    vec4 glowColor;
} p;

layout(location = 0) in vec2 localPixels;
layout(location = 0) out vec4 color;

float sampleMask(vec2 uv)
{
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 0.0;
    return texture(arrowMask, uv).a;
}

void main()
{
    const vec2 directions[8] = vec2[8](
        vec2(1.0, 0.0), vec2(0.70710678, 0.70710678),
        vec2(0.0, 1.0), vec2(-0.70710678, 0.70710678),
        vec2(-1.0, 0.0), vec2(-0.70710678, -0.70710678),
        vec2(0.0, -1.0), vec2(0.70710678, -0.70710678));

    const vec2 clipOffset = gl_FragCoord.xy - p.clip.xy;
    const float clipExtent = abs(p.clip.z);
    if (p.clip.z < 0.0 ? max(abs(clipOffset.x), abs(clipOffset.y)) > clipExtent :
        dot(clipOffset, clipOffset) > clipExtent * clipExtent)
        discard;

    const float sizePixels = p.shape.x;
    const float haloRadius = p.shape.z;
    const float headingCos = cos(p.shape.y);
    const float headingSin = sin(p.shape.y);
    const vec2 sourceOffset = vec2(
        headingCos * localPixels.x + headingSin * localPixels.y,
        -headingSin * localPixels.x + headingCos * localPixels.y);
    const vec2 uv = sourceOffset / sizePixels + vec2(0.5);
    if (sampleMask(uv) > 0.10)
        discard;

    float halo = 0.0;
    for (int direction = 0; direction < 8; ++direction)
    {
        for (int ring = 1; ring <= 6; ++ring)
        {
            const float distancePixels = float(ring) * 0.5;
            const float falloff = max(0.0, 1.0 - distancePixels / (haloRadius + 0.5));
            const float coverage = sampleMask(uv + directions[direction] * (distancePixels / sizePixels));
            halo = max(halo, coverage * falloff);
        }
    }

    if (halo <= 0.0)
        discard;
    color = vec4(p.glowColor.rgb, halo * p.shape.w);
}
