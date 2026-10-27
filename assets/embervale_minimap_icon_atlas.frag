#version 450

layout(binding = 0) uniform sampler2D iconAtlas;

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

layout(location = 0) out vec4 color;

int roundAwayFromZero(float value)
{
    precise float integral = trunc(value);
    precise float fraction = abs(value - integral);
    if (fraction >= 0.5)
    {
        precise float direction = value < 0.0 ? -1.0 : 1.0;
        precise float rounded = integral + direction;
        return int(rounded);
    }
    return int(integral);
}

int legacyBoundary(int logical, float center)
{
    // These precise intermediates emit SPIR-V NoContraction decorations and
    // keep the CPU's integer delta, float multiply, then float add sequence.
    precise float offset = float(logical - int(center));
    precise float scaled = offset * p.transform.z;
    precise float transformed = center + scaled;
    return roundAwayFromZero(transformed);
}

int logicalCellOffset(int pixel, int first, int count, float center)
{
    if (count <= 0 || pixel < legacyBoundary(first, center) ||
        pixel >= legacyBoundary(first + count, center))
        return -1;

    int low = 0;
    int high = count;
    while (low + 1 < high)
    {
        const int middle = (low + high) / 2;
        if (legacyBoundary(first + middle, center) <= pixel)
            low = middle;
        else
            high = middle;
    }
    return low;
}

float srgbToLinear(float value)
{
    value = clamp(value, 0.0, 1.0);
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float linearToPq(float value)
{
    const float normalized = pow(max(0.0, value) * 203.0 / 10000.0, 2610.0 / 16384.0);
    return pow((3424.0 / 4096.0 + (2413.0 / 128.0) * normalized) /
        (1.0 + (2392.0 / 128.0) * normalized), 2523.0 / 32.0);
}

vec3 encodeOverlay(vec3 value)
{
    if (p.style.w < 0.5)
        return value;

    const vec3 linear = vec3(srgbToLinear(value.r), srgbToLinear(value.g), srgbToLinear(value.b));
    if (p.style.w < 1.5)
        return linear * (203.0 / 80.0);

    precise float red = 0.627404 * linear.r + 0.329283 * linear.g + 0.043313 * linear.b;
    precise float green = 0.069097 * linear.r + 0.919540 * linear.g + 0.011362 * linear.b;
    precise float blue = 0.016391 * linear.r + 0.088013 * linear.g + 0.895595 * linear.b;
    return vec3(linearToPq(red), linearToPq(green), linearToPq(blue));
}

void main()
{
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const int targetWidth = int(p.logicalRect.z);
    const int targetHeight = int(p.logicalRect.w);
    if (targetWidth <= 0 || targetHeight <= 0)
        discard;

    const int logicalOffsetX = logicalCellOffset(pixel.x, int(p.logicalRect.x), targetWidth, p.transform.x);
    const int logicalOffsetY = logicalCellOffset(pixel.y, int(p.logicalRect.y), targetHeight, p.transform.y);
    if (logicalOffsetX < 0 || logicalOffsetY < 0)
        discard;
    const int logicalX = int(p.logicalRect.x) + logicalOffsetX;
    const int logicalY = int(p.logicalRect.y) + logicalOffsetY;
    // Legacy CPU raster rows use -1 as the unset runStart sentinel, so a visible
    // horizontal run beginning at negative global X is omitted. Retain that X
    // behavior for pixel parity; valid negative Y cells remain drawable.
    if (logicalX < 0)
        discard;

    const int dx = logicalX - int(p.atlas.z);
    const int dy = logicalY - int(p.atlas.w);
    const int radius = int(p.transform.w);
    if (p.state.z > 0.5)
    {
        if (max(abs(dx), abs(dy)) > radius)
            discard;
    }
    else if (dx * dx + dy * dy > radius * radius)
    {
        discard;
    }

    const int localX = logicalX - int(p.logicalRect.x);
    const int localY = logicalY - int(p.logicalRect.y);
    const int sourceWidth = int(p.sourceRect.z);
    const int sourceHeight = int(p.sourceRect.w);
    const int sourceX = localX * sourceWidth / targetWidth;
    const int sourceY = localY * sourceHeight / targetHeight;
    const ivec2 atlasPixel = ivec2(int(p.sourceRect.x) + sourceX,
        int(p.sourceRect.y) + sourceY);
    const vec4 source = texelFetch(iconAtlas, atlasPixel, 0);
    if (source.a <= 24.0 / 255.0)
        discard;

    vec3 sourceColor = source.rgb;
    if (p.state.w > 0.5)
    {
        const ivec2 glyphPixel = atlasPixel + ivec2(0, int(p.atlas.x));
        if (texelFetch(iconAtlas, glyphPixel, 0).a <= 0.5)
            discard;
        sourceColor = p.style.rgb;
    }

    color = vec4(encodeOverlay(sourceColor), 1.0);
}
