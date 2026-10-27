#pragma once

#include "arrow_glow_policy.h"

namespace MinimapMarkerGlow
{
    enum class Kind : std::uint32_t
    {
        FilledDiamond = 0,
        HollowDiamond = 1,
        DeathMarker = 2
    };

    struct Float4
    {
        float x;
        float y;
        float z;
        float w;
    };

    // Five vec4 values are shared by both shader stages (80 bytes total).
    struct PushConstants
    {
        Float4 viewportCenter;
        Float4 shape;
        Float4 clip;
        Float4 haloColor;
        Float4 plateColor;
    };

    constexpr float PingBodyExtent = 13.0f;
    constexpr float WaypointBodyExtent = 15.0f;
    constexpr float HaloRadiusPixels = 3.0f;
    constexpr float HaloOpacity = 0.70f;
    constexpr float DeathBodyRadiusPixels = 16.5f;
    constexpr float DeathRimWidthPixels = 1.5f;

    static_assert(sizeof(PushConstants) == 80,
        "Marker glow push constants must match five GLSL vec4 values.");
    static_assert(sizeof(PushConstants) <= 128,
        "Vulkan guarantees at least 128 push-constant bytes.");

    inline MinimapArrowGlow::Color SelectColor(MinimapArrowGlow::Style style, bool ping)
    {
        if (style == MinimapArrowGlow::Style::Contrasting)
            return ping ? MinimapArrowGlow::Color{ 1.0f, 153.0f / 255.0f, 50.0f / 255.0f } :
                MinimapArrowGlow::Color{ 0.25f, 0.85f, 1.0f };
        return ping ? MinimapArrowGlow::Color{ 0.42f, 1.0f, 0.34f } :
            MinimapArrowGlow::Color{ 1.0f, 0.86f, 0.28f };
    }
}
