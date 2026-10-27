#version 450

layout(push_constant) uniform MarkerGlow
{
    vec4 viewportCenter;
    vec4 shape;
    vec4 clip;
    vec4 haloColor;
    vec4 plateColor;
} p;

layout(location = 0) in vec2 localPixels;
layout(location = 0) out vec4 color;

void main()
{
    const vec2 clipOffset = gl_FragCoord.xy - p.clip.xy;
    const float clipExtent = abs(p.clip.z);
    if (p.clip.z < 0.0 ? max(abs(clipOffset.x), abs(clipOffset.y)) > clipExtent :
        dot(clipOffset, clipOffset) > clipExtent * clipExtent)
        discard;

    const float kind = p.shape.w;
    const float haloRadius = p.shape.y;
    if (kind < 1.5)
    {
        // Manhattan distance describes the existing filled and hollow diamonds.
        const float outsideDistance = (abs(localPixels.x) + abs(localPixels.y) - p.shape.x) * 0.70710678;
        if (outsideDistance < 0.0 || outsideDistance >= haloRadius)
            discard;
        const float halo = 1.0 - smoothstep(0.0, haloRadius, outsideDistance);
        color = vec4(p.haloColor.rgb, halo * p.shape.z);
        return;
    }

    // D2: a dark plate, red rim, and restrained red exterior halo behind the icon.
    const float radialDistance = length(localPixels) - p.clip.w;
    const float rimWidth = p.clip.w * (1.5 / 16.5);
    if (radialDistance < -rimWidth)
    {
        color = vec4(p.plateColor.rgb, p.plateColor.a);
        return;
    }
    if (abs(radialDistance) <= rimWidth)
    {
        color = vec4(p.haloColor.rgb, 0.92);
        return;
    }
    if (radialDistance <= haloRadius)
    {
        const float halo = 1.0 - smoothstep(0.0, haloRadius, radialDistance);
        color = vec4(p.haloColor.rgb, halo * p.shape.z * 0.72);
        return;
    }
    discard;
}
