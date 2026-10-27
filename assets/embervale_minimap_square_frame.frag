#version 450

layout(binding = 0) uniform sampler2D frameTexture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

void main()
{
    const vec2 clipMin = vec2(77.0, 66.0) / 1254.0;
    const vec2 clipMax = vec2(1178.0, 1164.0) / 1254.0;
    if (any(lessThan(uv, clipMin)) || any(greaterThanEqual(uv, clipMax)))
        discard;

    // Keep the existing frame fragment's implicit sample and direct color write.
    color = texture(frameTexture, uv);
}