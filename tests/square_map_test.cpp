#include "../src/dllmain.cpp"
#include <iostream>

namespace
{
    struct SquareTestPixel { float r = 0, g = 0, b = 0; bool touched = false; };
    constexpr int kTestWidth = 1400;
    constexpr int kTestHeight = 1200;
    SquareTestPixel squareTestPixels[kTestHeight][kTestWidth];
    std::string squareTestShape = "round";
    std::string squareTestOrientation = "north-up";
    std::string squareTestPosition = "top-right";
    std::string squareTestOffset = "0";
    std::string squareTestShowClock = "true";
    std::string squareTestShowWeather = "true";
    MinimapArrowGlow::PushConstants squareTestArrowGlowPush{};
    MinimapMarkerGlow::PushConstants squareTestMarkerGlowPush{};

    void SquareTestCheck(bool ok, const char* message)
    {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        std::cout << "PASS: " << message << '\n';
    }

    std::string SquareTestGetConfig(const char*, const char* key, std::string fallback)
    {
        if (std::strcmp(key, "map_shape") == 0) return squareTestShape;
        if (std::strcmp(key, "map_orientation") == 0) return squareTestOrientation;
        if (std::strcmp(key, "position") == 0) return squareTestPosition;
        if (std::strcmp(key, "offset") == 0) return squareTestOffset;
        if (std::strcmp(key, "show_clock") == 0) return squareTestShowClock;
        if (std::strcmp(key, "show_weather") == 0) return squareTestShowWeather;
        return fallback;
    }

    std::int64_t SquareTestFixed(int value)
    {
        return static_cast<std::int64_t>(value) * 4294967296LL;
    }

    void SquareTestRaster(void*, std::uint32_t attachmentCount, const VkClearAttachment* attachments,
        std::uint32_t rectCount, const VkClearRect* rects)
    {
        for (std::uint32_t attachment = 0; attachment < attachmentCount; ++attachment)
            for (std::uint32_t index = 0; index < rectCount; ++index)
                for (std::uint32_t dy = 0; dy < rects[index].rect.extent.height; ++dy)
                    for (std::uint32_t dx = 0; dx < rects[index].rect.extent.width; ++dx)
                    {
                        const int x = rects[index].rect.offset.x + static_cast<int>(dx);
                        const int y = rects[index].rect.offset.y + static_cast<int>(dy);
                        if (x < 0 || x >= kTestWidth || y < 0 || y >= kTestHeight) std::exit(3);
                        squareTestPixels[y][x] = {
                            attachments[attachment].clearValue.color.float32[0],
                            attachments[attachment].clearValue.color.float32[1],
                            attachments[attachment].clearValue.color.float32[2],
                            true
                        };
                    }
    }

    void __fastcall SquareTestBindPipeline(void*, std::uint32_t, void*) {}
    void __fastcall SquareTestBindDescriptorSets(void*, std::uint32_t, void*, std::uint32_t,
        std::uint32_t, void* const*, std::uint32_t, const std::uint32_t*) {}
    void __fastcall SquareTestPushConstants(void*, void*, std::uint32_t, std::uint32_t,
        std::uint32_t size, const void* values)
    {
        if (size == sizeof(squareTestArrowGlowPush))
            std::memcpy(&squareTestArrowGlowPush, values, size);
        if (size == sizeof(squareTestMarkerGlowPush))
            std::memcpy(&squareTestMarkerGlowPush, values, size);
    }
    void __fastcall SquareTestDraw(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {}

    void SquareTestClearPixels()
    {
        std::memset(squareTestPixels, 0, sizeof(squareTestPixels));
    }

    ModContext SquareTestConfig()
    {
        ModContext config{};
        config.config.GetString = SquareTestGetConfig;
        return config;
    }

    void CheckConfigAndOrientation()
    {
        squareTestShape = "square";
        squareTestOrientation = "rotating";
        ModContext config = SquareTestConfig();
        RefreshMinimapConfig(&config, true);

        VulkanMinimapRenderer renderer{};
        CaptureMinimapShapeSnapshot(renderer);
        const MinimapOrientationAngles configured = ResolveMinimapOrientationAngles(
            g_minimapOrientation.load(), 0.75f, true);
        SquareTestCheck(IsSquareMinimap(renderer), "map_shape=square is parsed into the renderer snapshot");
        SquareTestCheck(g_minimapOrientation.load() == MinimapOrientation::Rotating,
            "square mode preserves the configured round orientation setting");
        SquareTestCheck(EffectiveMinimapMapHeading(renderer, configured) == 0.0f &&
            std::fabs(configured.playerArrowHeading) < 0.0001f,
            "square mode overrides only effective map heading and retains configured arrow heading");

        const std::uint64_t squareSnapshot = renderer.mapShapeSnapshot;
        squareTestShape = "round";
        RefreshMinimapConfig(&config, true);
        SquareTestCheck(IsSquareMinimap(renderer) && renderer.mapShapeSnapshot == squareSnapshot,
            "a config transition cannot mutate an in-flight renderer shape snapshot");
        CaptureMinimapShapeSnapshot(renderer);
        SquareTestCheck(!IsSquareMinimap(renderer) && ShapeEpochFromSnapshot(renderer.mapShapeSnapshot) ==
            ShapeEpochFromSnapshot(squareSnapshot) + 1,
            "the next renderer snapshot observes a shape transition with a new epoch");

        const int x = 0, y = 0;
        int px = 0, py = 0;
        bool clipped = false;
        ProjectWorldToMinimap(400.0f, -20.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            x, y, 120, px, py, clipped, MinimapShape::Square);
        SquareTestCheck(clipped && px == 102 && py == 5,
            "far off-axis square projection preserves bearing at the Chebyshev rim");
        ProjectWorldToMinimap(300.0f, -300.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            x, y, 120, px, py, clipped, MinimapShape::Square);
        SquareTestCheck(clipped && px == 102 && py == 102,
            "diagonal square projection reaches the corner without moving its bearing");
        ProjectWorldToMinimap(400.0f, -20.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            x, y, 120, px, py, clipped, MinimapShape::Round);
        SquareTestCheck(clipped && px == 101 && py == 5,
            "round projection retains its prior radial clamp result");
    }

    void CheckFrameAndPanelBounds()
    {
        VulkanMinimapRenderer renderer{};
        renderer.width = kTestWidth;
        renderer.height = kTestHeight;
        renderer.fns.cmdClearAttachments = SquareTestRaster;
        renderer.mapShapeSnapshot = (g_minimapShapeConfig.load() & ~1ull) | 1ull;
        renderer.mapShapeSnapshotValid = true;
        WorldClockSnapshot clock{};
        clock.valid = true;
        clock.minuteOfDay = 615;
        WorldWeatherSnapshot weather{};
        weather.valid = true;
        weather.kind = 2;

        const int expectedWidths[] = { 130, 259, 518 };
        const float scales[] = { 0.5f, 1.0f, 2.0f };
        for (int sample = 0; sample < 3; ++sample)
        {
            const float scale = scales[sample];
            const MinimapPixelBounds frame = SquareFrameBoundsAtScale(600, 500, scale);
            SquareTestCheck(frame.Width() == expectedWidths[sample] && frame.Height() == expectedWidths[sample],
                sample == 0 ? "square frame geometry is equal-sided at 50 percent" :
                sample == 1 ? "square frame geometry is equal-sided at 100 percent" :
                    "square frame geometry is equal-sided at 200 percent");

            SquareTestClearPixels();
            {
                MinimapScaleScope scope(renderer, scale, 600, 500);
                DrawWorldClockPanel(renderer, nullptr, 600, 500, clock, weather, true);
            }
            int minX = kTestWidth, maxX = -1;
            for (int y = 0; y < kTestHeight; ++y)
                for (int x = 0; x < kTestWidth; ++x)
                    if (squareTestPixels[y][x].touched)
                    {
                        minX = MinValue(minX, x);
                        maxX = MaxValue(maxX, x);
                    }
            std::cout << "MEASURE panel scale=" << scale << " bounds=" << minX << ".." << maxX
                << " expected=" << frame.left << ".." << frame.right - 1 << '\n';
            SquareTestCheck(maxX - minX + 1 == expectedWidths[sample] && minX == frame.left && maxX + 1 == frame.right,
                sample == 0 ? "full clock/weather bar bounds match the square frame at 50 percent" :
                sample == 1 ? "full clock/weather bar bounds match the square frame at 100 percent" :
                    "full clock/weather bar bounds match the square frame at 200 percent");

            SquareTestClearPixels();
            const MinimapPixelBounds oddCenterFrame = SquareFrameBoundsAtScale(601, 501, scale);
            {
                MinimapScaleScope scope(renderer, scale, 601, 501);
                DrawWorldClockPanel(renderer, nullptr, 601, 501, clock, weather, true);
            }
            minX = kTestWidth; maxX = -1;
            for (int y = 0; y < kTestHeight; ++y)
                for (int x = 0; x < kTestWidth; ++x)
                    if (squareTestPixels[y][x].touched)
                    {
                        minX = MinValue(minX, x);
                        maxX = MaxValue(maxX, x);
                    }
            SquareTestCheck(maxX - minX + 1 == expectedWidths[sample] &&
                minX == oddCenterFrame.left && maxX + 1 == oddCenterFrame.right &&
                oddCenterFrame.Width() == oddCenterFrame.Height(),
                sample == 0 ? "odd-center half-pixel bar/frame bounds agree at 50 percent" :
                sample == 1 ? "odd-center half-pixel bar/frame bounds agree at 100 percent" :
                    "odd-center half-pixel bar/frame bounds agree at 200 percent");
        }

        const float scale = 1.0f;
        MinimapPixelBounds allContent{};
        for (int content = 0; content < 2; ++content)
        {
            SquareTestClearPixels();
            {
                MinimapScaleScope scope(renderer, scale, 600, 500);
                DrawWorldClockPanel(renderer, nullptr, 600, 500,
                    content == 0 ? clock : WorldClockSnapshot{},
                    content == 1 ? weather : WorldWeatherSnapshot{}, true);
            }
            int minX = kTestWidth, maxX = -1;
            for (int y = 0; y < kTestHeight; ++y)
                for (int x = 0; x < kTestWidth; ++x)
                    if (squareTestPixels[y][x].touched)
                    {
                        minX = MinValue(minX, x);
                        maxX = MaxValue(maxX, x);
                    }
            if (content == 0) allContent = { minX, 0, maxX + 1, 0 };
            SquareTestCheck(maxX - minX + 1 == MINIMAP_SQUARE_FRAME_OUTER_WIDTH,
                content == 0 ? "time-only content retains the square-mode full bar footprint" :
                    "weather-only content retains the square-mode full bar footprint");
            if (content == 1)
                SquareTestCheck(minX == allContent.left && maxX + 1 == allContent.right,
                    "partial clock and weather availability do not resize the square bar");
        }

        SquareTestClearPixels();
        {
            MinimapScaleScope scope(renderer, 1.0f, 600, 500);
            DrawWorldClockPanel(renderer, nullptr, 600, 500, WorldClockSnapshot{}, WorldWeatherSnapshot{}, false);
        }
        bool anyPixel = false;
        for (int y = 0; y < kTestHeight && !anyPixel; ++y)
            for (int x = 0; x < kTestWidth; ++x)
                anyPixel |= squareTestPixels[y][x].touched;
        SquareTestCheck(!anyPixel, "the panel stays hidden when time and weather content are unavailable");
    }

    void CheckPlacementAndScaling()
    {
        const int radius = MINIMAP_SQUARE_MAP_HALF_EXTENT;
        const int frameHalf = 130;
        const int topExtra = -MINIMAP_SQUARE_FRAME_TOP_OFFSET - radius;
        const int bottomExtra = MINIMAP_SQUARE_FRAME_BOTTOM_EXCLUSIVE_OFFSET - radius;
        const int footer = bottomExtra + CLOCK_FRAME_GAP + 2 * CLOCK_PANEL_HALF_HEIGHT;
        g_minimapOffset = 0;
        int leftX = 0, rightX = 0;
        int yPositions[3] = {};
        for (int placement = static_cast<int>(MinimapPlacement::TopRight);
            placement <= static_cast<int>(MinimapPlacement::BottomLeft); ++placement)
        {
            g_minimapPlacement = placement;
            const int x = ComputeMinimapCenterX(1200, radius, 58, frameHalf);
            const int y = ComputeMinimapCenterY(900, radius, 52, footer, topExtra);
            if (placement == static_cast<int>(MinimapPlacement::TopRight)) rightX = x;
            if (placement == static_cast<int>(MinimapPlacement::TopLeft)) leftX = x;
            yPositions[placement % 3] = y;
        }
        SquareTestCheck(leftX < rightX && yPositions[0] < yPositions[1] && yPositions[1] < yPositions[2],
            "all six placements remain distinct in square mode");
        g_minimapPlacement = static_cast<int>(MinimapPlacement::MiddleRight);
        g_minimapOffset = 100;
        const int shiftedUp = ComputeMinimapCenterY(900, radius, 52, footer, topExtra);
        g_minimapOffset = -100;
        const int shiftedDown = ComputeMinimapCenterY(900, radius, 52, footer, topExtra);
        SquareTestCheck(shiftedUp < yPositions[1] && shiftedDown > yPositions[1],
            "signed offsets retain their upward-positive scaling in square mode");
        g_minimapOffset = 1080;
        const int topClamp = ComputeMinimapCenterY(900, radius, 52, footer, topExtra);
        g_minimapOffset = -1080;
        const int bottomClamp = ComputeMinimapCenterY(900, radius, 52, footer, topExtra);
        SquareTestCheck(topClamp >= radius + topExtra + 8 && bottomClamp <= 900 - radius - 8 - footer,
            "large signed offsets stay within the square frame and bar safety bounds");
        g_minimapOffset = 0;
        g_minimapPlacement = static_cast<int>(MinimapPlacement::TopRight);
    }

    void CheckMarkerAndGlowClipping()
    {
        VulkanMinimapRenderer renderer{};
        renderer.width = 400;
        renderer.height = 400;
        renderer.widgetCenterX = renderer.widgetCenterY = 100;
        renderer.widgetScale = 1.0f;
        renderer.arrowGlowFrameEligible = true;
        renderer.arrowGlowResources.frameTextureReady = true;
        renderer.arrowGlowResources.framePipeline = 1;
        renderer.arrowGlowResources.framePipelineLayout = 1;
        renderer.arrowGlowResources.frameDescriptorSet = 1;
        renderer.markerGlowResources.ready = true;
        renderer.markerGlowResources.pipeline = 1;
        renderer.markerGlowResources.pipelineLayout = 1;
        renderer.fns.cmdBindPipeline = SquareTestBindPipeline;
        renderer.fns.cmdBindDescriptorSets = SquareTestBindDescriptorSets;
        renderer.fns.cmdPushConstants = SquareTestPushConstants;
        renderer.fns.cmdDraw = SquareTestDraw;
        renderer.fns.cmdClearAttachments = SquareTestRaster;
        g_playerArrowGlowEnabled = true;
        g_pingGlowEnabled = true;

        renderer.mapShapeSnapshot = (g_minimapShapeConfig.load() & ~1ull) | 1ull;
        renderer.mapShapeSnapshotValid = true;
        SquareTestCheck(TryDrawPlayerArrowGlow(renderer, nullptr, 115, 115, 24, 0.2f,
            100, 100, 20, true) && squareTestArrowGlowPush.clipRadiusPixels < 0.0f,
            "production player-arrow glow encodes square clip policy without changing its push layout");
        SquareTestCheck(TryDrawMarkerGlow(renderer, nullptr, 115, 115,
            MinimapMarkerGlow::Kind::FilledDiamond, 100, 100, 20,
            MinimapArrowGlow::Style::SameColour) && squareTestMarkerGlowPush.clip.z < 0.0f,
            "production marker glow encodes square clip policy for the accepted glow shader");

        SquareTestClearPixels();
        CmdClearSmallCircle(renderer, nullptr, 100, 100, 20, 115, 115, 3, 1, 0, 0);
        SquareTestCheck(squareTestPixels[115][115].touched,
            "square marker body accepts a diagonal pixel inside the square edge");
        renderer.mapShapeSnapshot = g_minimapShapeConfig.load() & ~1ull;
        SquareTestClearPixels();
        CmdClearSmallCircle(renderer, nullptr, 100, 100, 20, 115, 115, 3, 1, 0, 0);
        SquareTestCheck(!squareTestPixels[115][115].touched,
            "round marker body keeps its prior radial clip boundary");
    }

    void CheckSquareResourceRetryEpoch()
    {
        {
            std::lock_guard<std::mutex> lock(g_squareMinimapFrameMutex);
            g_squareMinimapFrame = {};
            g_squareMinimapFrame.attempted = true;
            g_squareMinimapFrame.loaded = true;
            g_squareMinimapFrame.width = g_squareMinimapFrame.height = 1254;
            g_squareMinimapFrame.rgba.resize(4);
        }
        VulkanMinimapRenderer renderer{};
        PublishMinimapShape(MinimapShape::Square);
        CaptureMinimapShapeSnapshot(renderer);
        const std::uint64_t firstEpoch = ShapeEpochFromSnapshot(renderer.mapShapeSnapshot);
        SquareTestCheck(!PrepareSquareFrameResourcesLocked(renderer) && renderer.squareFrameAttempted &&
            renderer.squareFrameUnavailable && renderer.squareFrameObservedShapeEpoch == firstEpoch,
            "missing Vulkan functions select square fallback and mark one frame setup attempt");
        SquareTestCheck(!PrepareSquareFrameResourcesLocked(renderer) && renderer.squareFrameAttempted,
            "square frame setup does not retry every rendered frame");
        PublishMinimapShape(MinimapShape::Round);
        CaptureMinimapShapeSnapshot(renderer);
        PublishMinimapShape(MinimapShape::Square);
        CaptureMinimapShapeSnapshot(renderer);
        const std::uint64_t nextEpoch = ShapeEpochFromSnapshot(renderer.mapShapeSnapshot);
        SquareTestCheck(nextEpoch > firstEpoch && !PrepareSquareFrameResourcesLocked(renderer) &&
            renderer.squareFrameObservedShapeEpoch == nextEpoch && renderer.squareFrameAttempted,
            "an explicit shape transition is the bounded retry trigger for failed square resources");
        PublishMinimapShape(MinimapShape::Round);
        CaptureMinimapShapeSnapshot(renderer);
    }

    void CheckSquareTerrainAndFallback()
    {
        squareTestShape = "square";
        squareTestOrientation = "rotating";
        ModContext config = SquareTestConfig();
        RefreshMinimapConfig(&config, true);
        int squareTextureSize = 0, roundTextureSize = 0;
        SquareTestCheck(TryResolveSquareFrameRgbaDimensions(1254LL * 1254LL * 4LL, squareTextureSize) &&
            squareTextureSize == 1254 &&
            !TryResolveFrameRgbaDimensions(1254LL * 1254LL * 4LL, roundTextureSize),
            "square frame dimensions allow 2048 while circle frame limits remain unchanged");

        g_realMap = {};
        g_realMap.attempted = true;
        g_realMap.loaded = true;
        g_realMap.width = g_realMap.height = 8;
        g_realMap.path = "inert-square-corner-fixture";
        g_realMap.rgba.resize(8u * 8u * 4u);
        for (std::size_t pixel = 0; pixel < 64; ++pixel)
        {
            g_realMap.rgba[pixel * 4 + 0] = 255;
            g_realMap.rgba[pixel * 4 + 1] = 0;
            g_realMap.rgba[pixel * 4 + 2] = 255;
            g_realMap.rgba[pixel * 4 + 3] = 255;
        }
        g_defaultMap = {};
        g_defaultMapStyle = false;
        g_minimapMapLight = 0;
        g_worldMapLayoutSupported = true;
        g_liveMarkerLayoutSupported = true;
        g_worldClock = {};
        g_worldWeather = {};
        g_squareMinimapFrame = {};
        SquareTestClearPixels();

        CapturedPlayerPosition player{};
        player.x = SquareTestFixed(5120);
        player.z = SquareTestFixed(5120);
        player.headingRadians = 0.75f;
        player.lastUpdateTick = GetTickCount();
        player.hasHeading = true;
        player.valid = true;
        player.channel = 30;
        PublishPlayerPosition(player);

        VulkanMinimapRenderer renderer{};
        renderer.width = 512;
        renderer.height = 320;
        renderer.fns.cmdClearAttachments = SquareTestRaster;
        DrawMinimapWidget(renderer, nullptr, MinimapFogFrame{});

        bool terrainInDiagonalCorner = false;
        for (int y = renderer.widgetCenterY + 76; y <= renderer.widgetCenterY + 88; ++y)
            for (int x = renderer.widgetCenterX + 76; x <= renderer.widgetCenterX + 88; ++x)
                if (squareTestPixels[y][x].r > 0.3f && squareTestPixels[y][x].g < 0.2f && squareTestPixels[y][x].b > 0.3f)
                    terrainInDiagonalCorner = true;
        SquareTestCheck(terrainInDiagonalCorner,
            "square configuration renders actual CPU terrain in a diagonal corner outside the round radius");
        const int outerTop = renderer.widgetCenterY + MINIMAP_SQUARE_FRAME_TOP_OFFSET + 1;
        SquareTestCheck(squareTestPixels[outerTop][renderer.widgetCenterX].r > 0.5f,
            "missing square GPU frame uses square vector fallback without reverting to a round frame");
        SquareTestCheck(g_minimapOrientation.load() == MinimapOrientation::Rotating,
            "terrain fallback leaves configured round orientation intact");

        g_realMap = {};
        g_defaultMap = {};
        g_worldMapLayoutSupported = false;
        g_liveMarkerLayoutSupported = false;
        g_minimapMapLight = 55;
        RefreshMinimapConfig(nullptr, true);
    }
}

int main()
{
    CheckConfigAndOrientation();
    CheckFrameAndPanelBounds();
    CheckPlacementAndScaling();
    CheckMarkerAndGlowClipping();
    CheckSquareResourceRetryEpoch();
    CheckSquareTerrainAndFallback();
    return 0;
}
