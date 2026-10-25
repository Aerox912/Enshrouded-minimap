#version 450

layout(push_constant) uniform IconSprite
{
    vec4 logicalRect;
    vec4 sourceRect;
    vec4 physicalRect;
    vec4 transform;
    vec4 state;
    vec4 style;
    vec4 atlas;
} p;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
    const vec2 corner = corners[gl_VertexIndex];
    const vec2 pixel = mix(p.physicalRect.xy, p.physicalRect.zw, corner);
    const vec2 ndc = vec2(pixel.x / p.state.x * 2.0 - 1.0,
        pixel.y / p.state.y * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
