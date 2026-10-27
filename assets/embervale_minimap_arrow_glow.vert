#version 450

layout(push_constant) uniform ArrowGlow
{
    vec4 viewportCenter;
    vec4 shape;
    vec4 clip;
    vec4 glowColor;
} p;

layout(location = 0) out vec2 localPixels;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(0, 0), vec2(1, 0), vec2(0, 1),
        vec2(0, 1), vec2(1, 0), vec2(1, 1));
    const vec2 corner = corners[gl_VertexIndex];
    localPixels = (corner - vec2(0.5)) * (p.shape.x + 2.0 * p.shape.z);
    const vec2 screen = p.viewportCenter.xy + localPixels;
    const vec2 ndc = screen / p.viewportCenter.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
