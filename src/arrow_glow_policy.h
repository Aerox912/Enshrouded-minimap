#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace MinimapArrowGlow
{
    enum class Style : std::uint32_t
    {
        SameColour = 0,
        Contrasting = 1
    };

    struct Color
    {
        float red;
        float green;
        float blue;
    };

    // Four std430-compatible vec4 groups shared with the vertex and fragment shaders.
    struct PushConstants
    {
        float centerX;
        float centerY;
        float viewportWidth;
        float viewportHeight;
        float sizePixels;
        float headingRadians;
        float haloRadiusPixels;
        float opacity;
        float clipCenterX;
        float clipCenterY;
        float clipRadiusPixels;
        float clipPadding;
        float red;
        float green;
        float blue;
        float colorPadding;
    };

    constexpr float HaloRadiusPixels = 2.5f;
    constexpr float HaloOpacity = 0.72f;

    static_assert(sizeof(PushConstants) == 64, "Arrow glow push constants must match four GLSL vec4 values.");
    static_assert(sizeof(PushConstants) <= 128, "Vulkan guarantees at least 128 push-constant bytes.");

    inline Style ResolveStyle(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return Style::SameColour;
        const auto last = value.find_last_not_of(" \t\r\n");
        std::string normalized;
        normalized.reserve(last - first + 1);
        for (std::size_t i = first; i <= last; ++i)
        {
            const char ch = value[i];
            normalized.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
        return normalized == "contrasting" ? Style::Contrasting : Style::SameColour;
    }

    inline const char* StyleName(Style style)
    {
        return style == Style::Contrasting ? "contrasting" : "same-colour";
    }

    inline Color SelectColor(Style style, bool remote)
    {
        if (style == Style::Contrasting)
            return remote ? Color{ 223.0f / 255.0f, 79.0f / 255.0f, 1.0f } :
                Color{ 1.0f, 153.0f / 255.0f, 50.0f / 255.0f };
        return remote ? Color{ 72.0f / 255.0f, 1.0f, 104.0f / 255.0f } :
            Color{ 67.0f / 255.0f, 231.0f / 255.0f, 1.0f };
    }
}
