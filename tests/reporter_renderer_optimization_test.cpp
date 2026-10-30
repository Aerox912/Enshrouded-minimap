#define MINIMAP_TEST_ICON_ATLAS_MUTATION 1
#pragma warning(push)
#pragma warning(disable: 4715)
#define main FrameFailureSetupMain
#include "frame_failure_test.cpp"
#undef main
#pragma warning(pop)
#undef MINIMAP_TEST_ICON_ATLAS_MUTATION

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>

namespace
{
    bool g_trackAllocations = false;
    std::size_t g_trackedAllocations = 0;
}

void* operator new(std::size_t size)
{
    if (g_trackAllocations) ++g_trackedAllocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (g_trackAllocations) ++g_trackedAllocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    struct TestPixel
    {
        float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 0.0f;
        bool touched = false;
    };

    struct TestSpritePush
    {
        float logicalRect[4]{};
        float sourceRect[4]{};
        float physicalRect[4]{};
        float transform[4]{};
        float state[4]{};
        float style[4]{};
        float atlas[4]{};
    };

    static_assert(sizeof(TestSpritePush) == 112, "sprite push constants stay within Vulkan's 128-byte minimum");

    std::vector<TestPixel> g_pixels;
    int g_testWidth = 0, g_testHeight = 0;
    std::uint32_t g_clearCalls = 0, g_clearRects = 0, g_drawCalls = 0;
    std::uint32_t g_copyCalls = 0, g_barrierCalls = 0, g_pushCalls = 0;
    std::uint32_t g_copyWidth = 0, g_copyHeight = 0;
    std::uint32_t g_lastPushSize = 0, g_eventCount = 0;
    char g_events[8192]{};
    TestSpritePush g_lastPush{};
    std::uint64_t g_stagingBytes = 0;
    std::vector<std::uint8_t> g_uploadedBytes;
    std::vector<std::uint8_t> g_vertexShaderBytes, g_fragmentShaderBytes;
    int g_shaderModuleCount = 0;
    ModContext g_testContext{};

    void ReporterCheck(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
        std::cout << "PASS: " << message << '\n';
    }

    void ResetCounters()
    {
        g_clearCalls = g_clearRects = g_drawCalls = 0;
        g_copyCalls = g_barrierCalls = g_pushCalls = 0;
        g_copyWidth = g_copyHeight = g_lastPushSize = g_eventCount = 0;
        g_lastPush = {};
    }

    void SetViewport(int width, int height)
    {
        g_testWidth = width;
        g_testHeight = height;
        g_pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), {});
    }

    void ClearPixels()
    {
        std::fill(g_pixels.begin(), g_pixels.end(), TestPixel{});
    }

    TestPixel& PixelAt(int x, int y)
    {
        return g_pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(g_testWidth) +
            static_cast<std::size_t>(x)];
    }

    void AddEvent(char event)
    {
        if (g_eventCount < sizeof(g_events)) g_events[g_eventCount++] = event;
    }

    void __fastcall TestClearAttachments(void*, std::uint32_t attachmentCount,
        const VkClearAttachment* attachments, std::uint32_t rectCount, const VkClearRect* rects)
    {
        ++g_clearCalls;
        g_clearRects += rectCount;
        AddEvent('C');
        for (std::uint32_t attachment = 0; attachment < attachmentCount; ++attachment)
            for (std::uint32_t index = 0; index < rectCount; ++index)
                for (std::uint32_t dy = 0; dy < rects[index].rect.extent.height; ++dy)
                    for (std::uint32_t dx = 0; dx < rects[index].rect.extent.width; ++dx)
                    {
                        const int x = rects[index].rect.offset.x + static_cast<int>(dx);
                        const int y = rects[index].rect.offset.y + static_cast<int>(dy);
                        if (x < 0 || x >= g_testWidth || y < 0 || y >= g_testHeight)
                        {
                            std::cerr << "FAIL: clear rect escaped the fixture viewport: x=" << x << " y=" << y
                                << " width=" << g_testWidth << " height=" << g_testHeight << std::endl;
                            std::exit(86);
                        }
                        PixelAt(x, y) = {
                            attachments[attachment].clearValue.color.float32[0],
                            attachments[attachment].clearValue.color.float32[1],
                            attachments[attachment].clearValue.color.float32[2],
                            attachments[attachment].clearValue.color.float32[3],
                            true
                        };
                    }
    }

    void __fastcall TestMemoryRequirements(void*, void* object, VkMemoryRequirements* requirements)
    {
        requirements->size = live.count(object) != 0 && live[object] == "buffer" ? g_stagingBytes : 64;
        requirements->alignment = 1;
        requirements->memoryTypeBits = 1;
    }

    std::int32_t __fastcall TestMapMemory(void*, void*, std::uint64_t, std::uint64_t size,
        std::uint32_t, void** mapped)
    {
        g_uploadedBytes.assign(static_cast<std::size_t>(size), 0);
        *mapped = g_uploadedBytes.data();
        return VK_SUCCESS;
    }

    std::int32_t __fastcall TestFlush(void*, std::uint32_t count, const VkMappedMemoryRange*)
    {
        return count == 1 ? VK_SUCCESS : -11;
    }

    std::int32_t __fastcall TestShaderModule(void*, const VkShaderModuleCreateInfo* info,
        const void*, void** module)
    {
        if (info == nullptr || info->pCode == nullptr || info->codeSize == 0) return -11;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(info->pCode);
        auto& bytes = (++g_shaderModuleCount == 1) ? g_vertexShaderBytes : g_fragmentShaderBytes;
        bytes.assign(begin, begin + info->codeSize);
        return Create(g_shaderModuleCount == 1 ? "vertex-module" : "fragment-module", module);
    }

    std::int32_t __fastcall TestPipeline(void*, void*, std::uint32_t count,
        const VkGraphicsPipelineCreateInfo* infos, const void*, void** pipeline)
    {
        ReporterCheck(count == 1 && infos != nullptr && infos->pInputAssemblyState != nullptr &&
            infos->pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            "sprite pipeline uses a triangle list for one quad");
        ReporterCheck(infos->pColorBlendState != nullptr && infos->pColorBlendState->attachmentCount == 1 &&
            infos->pColorBlendState->pAttachments != nullptr &&
            infos->pColorBlendState->pAttachments[0].blendEnable == 0,
            "sprite pipeline preserves opaque raster-icon writes");
        return Create("pipeline", pipeline);
    }

    void __fastcall TestBarrier(void*, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t, const void*, std::uint32_t, const void*, std::uint32_t,
        const VkImageMemoryBarrier*)
    {
        ++g_barrierCalls;
    }

    void __fastcall TestCopy(void*, void*, void*, std::uint32_t, std::uint32_t count,
        const VkBufferImageCopy* regions)
    {
        ++g_copyCalls;
        if (count == 1 && regions != nullptr)
        {
            g_copyWidth = regions[0].imageExtent.width;
            g_copyHeight = regions[0].imageExtent.height;
        }
    }

    void __fastcall TestBindPipeline(void*, std::uint32_t, void*) {}
    void __fastcall TestBindDescriptorSets(void*, std::uint32_t, void*, std::uint32_t,
        std::uint32_t, void* const*, std::uint32_t, const std::uint32_t*) {}

    void __fastcall TestPushConstants(void*, void*, std::uint32_t, std::uint32_t offset,
        std::uint32_t size, const void* values)
    {
        ++g_pushCalls;
        g_lastPushSize = size;
        if (offset == 0 && size == sizeof(g_lastPush) && values != nullptr)
            std::memcpy(&g_lastPush, values, sizeof(g_lastPush));
    }

    float ShaderEncodeChannel(float value)
    {
        value = (std::max)(0.0f, (std::min)(1.0f, value));
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    std::array<float, 3> ShaderEncode(float red, float green, float blue, float mode)
    {
        if (mode < 0.5f) return { red, green, blue };
        float r = ShaderEncodeChannel(red), g = ShaderEncodeChannel(green), b = ShaderEncodeChannel(blue);
        if (mode < 1.5f) return { r * (203.0f / 80.0f), g * (203.0f / 80.0f), b * (203.0f / 80.0f) };
        const float rr = 0.627404f * r + 0.329283f * g + 0.043313f * b;
        const float gg = 0.069097f * r + 0.919540f * g + 0.011362f * b;
        const float bb = 0.016391f * r + 0.088013f * g + 0.895595f * b;
        const auto pq = [](float channel) {
            const float value = std::pow((std::max)(0.0f, channel) * 203.0f / 10000.0f, 2610.0f / 16384.0f);
            return std::pow((3424.0f / 4096.0f + (2413.0f / 128.0f) * value) /
                (1.0f + (2392.0f / 128.0f) * value), 2523.0f / 32.0f);
        };
        return { pq(rr), pq(gg), pq(bb) };
    }

    int TestBoundary(int logical, int center, float scale)
    {
        volatile float delta = static_cast<float>(logical - center);
        volatile float product = delta * scale;
        volatile float transformed = static_cast<float>(center) + product;
        return static_cast<int>(std::lround(transformed));
    }

    bool TestLogicalCell(int pixel, int first, int count, int center, float scale, int& localIndex)
    {
        localIndex = -1;
        if (count <= 0 || pixel < TestBoundary(first, center, scale) ||
            pixel >= TestBoundary(first + count, center, scale))
            return false;
        int low = 0, high = count;
        while (low + 1 < high)
        {
            const int middle = (low + high) / 2;
            if (TestBoundary(first + middle, center, scale) <= pixel) low = middle;
            else high = middle;
        }
        localIndex = low;
        return true;
    }

    void RenderSpritePush()
    {
        const int iconIndex = static_cast<int>(g_lastPush.atlas[1]);
        if (iconIndex < 0 || iconIndex >= static_cast<int>(g_minimapIcons.icons.size())) { std::cerr << "FAIL: sprite oracle received invalid icon index " << iconIndex << std::endl; std::exit(87); }
        const MinimapIcon& icon = g_minimapIcons.icons[static_cast<std::size_t>(iconIndex)];
        const int left = static_cast<int>(g_lastPush.logicalRect[0]);
        const int top = static_cast<int>(g_lastPush.logicalRect[1]);
        const int targetWidth = static_cast<int>(g_lastPush.logicalRect[2]);
        const int targetHeight = static_cast<int>(g_lastPush.logicalRect[3]);
        const int physicalLeft = static_cast<int>(g_lastPush.physicalRect[0]);
        const int physicalTop = static_cast<int>(g_lastPush.physicalRect[1]);
        const int physicalRight = static_cast<int>(g_lastPush.physicalRect[2]);
        const int physicalBottom = static_cast<int>(g_lastPush.physicalRect[3]);
        const int widgetCenterX = static_cast<int>(g_lastPush.transform[0]);
        const int widgetCenterY = static_cast<int>(g_lastPush.transform[1]);
        const float scale = g_lastPush.transform[2];
        const int radius = static_cast<int>(g_lastPush.transform[3]);
        const bool square = g_lastPush.state[2] > 0.5f;
        const bool styled = g_lastPush.state[3] > 0.5f;
        const int clipCenterX = static_cast<int>(g_lastPush.atlas[2]);
        const int clipCenterY = static_cast<int>(g_lastPush.atlas[3]);
        const int atlasX = static_cast<int>(g_lastPush.sourceRect[0]);
        const int atlasY = static_cast<int>(g_lastPush.sourceRect[1]);
        const int glyphOffset = static_cast<int>(g_lastPush.atlas[0]);
        const int sourceWidth = static_cast<int>(g_lastPush.sourceRect[2]);
        const int sourceHeight = static_cast<int>(g_lastPush.sourceRect[3]);
        const float colorMode = g_lastPush.style[3];

        const int viewportWidth = static_cast<int>(g_lastPush.state[0]);
        const int viewportHeight = static_cast<int>(g_lastPush.state[1]);
        for (int y = (std::max)(0, physicalTop); y < (std::min)(viewportHeight, physicalBottom); ++y)
        {
            int localCellY = -1;
            if (!TestLogicalCell(y, top, targetHeight, widgetCenterY, scale, localCellY)) continue;
            const int logicalY = top + localCellY;
            const int dy = logicalY - clipCenterY;
            for (int x = (std::max)(0, physicalLeft); x < (std::min)(viewportWidth, physicalRight); ++x)
            {
                int localCellX = -1;
                if (!TestLogicalCell(x, left, targetWidth, widgetCenterX, scale, localCellX)) continue;
                const int logicalX = left + localCellX;
                // Match the production shader's legacy CPU runStart parity rule.
                if (logicalX < 0) continue;
                const int dx = logicalX - clipCenterX;
                if ((square && (std::max)(std::abs(dx), std::abs(dy)) > radius) ||
                    (!square && dx * dx + dy * dy > radius * radius))
                    continue;

                const int localX = logicalX - left;
                const int localY = logicalY - top;
                const int sourceX = (localX * sourceWidth) / targetWidth;
                const int sourceY = (localY * sourceHeight) / targetHeight;
                const std::size_t sourceIndex = static_cast<std::size_t>(sourceY) * icon.width +
                    static_cast<std::size_t>(sourceX);
                const std::size_t sourceOffset = sourceIndex * 4u;
                if (icon.rgba[sourceOffset + 3] <= 24 || (styled && !icon.glyph[sourceIndex])) continue;

                const float red = styled ? g_lastPush.style[0] : icon.rgba[sourceOffset + 0] / 255.0f;
                const float green = styled ? g_lastPush.style[1] : icon.rgba[sourceOffset + 1] / 255.0f;
                const float blue = styled ? g_lastPush.style[2] : icon.rgba[sourceOffset + 2] / 255.0f;
                const auto encoded = ShaderEncode(red, green, blue, colorMode);
                PixelAt(x, y) = { encoded[0], encoded[1], encoded[2], 1.0f, true };
            }
        }
        (void)atlasX;
        (void)atlasY;
        (void)glyphOffset;
    }

    void __fastcall TestDraw(void*, std::uint32_t vertices, std::uint32_t instances,
        std::uint32_t, std::uint32_t)
    {
        ++g_drawCalls;
        AddEvent('D');
        if (vertices == 6 && instances == 1 && g_lastPushSize == sizeof(g_lastPush))
            RenderSpritePush();
    }

    void ConfigureFakeRenderer()
    {
        failure.clear();
        failureMode = 0;
        Setup(0);
        g_renderer.width = 128;
        g_renderer.height = 96;
        g_renderer.renderPass = 0x200;
        g_renderer.widgetScale = 1.0f;
        g_renderer.widgetCenterX = 64;
        g_renderer.widgetCenterY = 48;
        g_renderer.fns.cmdClearAttachments = TestClearAttachments;
        g_renderer.fns.getBufferMemoryRequirements = TestMemoryRequirements;
        g_renderer.fns.getImageMemoryRequirements = TestMemoryRequirements;
        g_renderer.fns.mapMemory = TestMapMemory;
        g_renderer.fns.flushMappedMemoryRanges = TestFlush;
        g_renderer.fns.createShaderModule = TestShaderModule;
        g_renderer.fns.createGraphicsPipelines = TestPipeline;
        g_renderer.fns.cmdPipelineBarrier = TestBarrier;
        g_renderer.fns.cmdCopyBufferToImage = TestCopy;
        g_renderer.fns.cmdBindPipeline = TestBindPipeline;
        g_renderer.fns.cmdBindDescriptorSets = TestBindDescriptorSets;
        g_renderer.fns.cmdPushConstants = TestPushConstants;
        g_renderer.fns.cmdDraw = TestDraw;
        g_testContext.shroudtopia.mod_folder.clear();
        g_testContext.Log = [](const char*) {};
        g_modContext = &g_testContext;
        g_vertexShaderBytes.clear();
        g_fragmentShaderBytes.clear();
        g_uploadedBytes.clear();
        g_shaderModuleCount = 0;
        g_stagingBytes = 128ull * 256ull * 4ull;
        SetViewport(128, 96);
        ResetCounters();
    }

    MinimapIcon MakeIcon(std::uint32_t key, std::uint32_t width, std::uint32_t height, int variant)
    {
        MinimapIcon icon{};
        icon.key = key;
        icon.width = width;
        icon.height = height;
        icon.rgba.resize(static_cast<std::size_t>(width) * height * 4u);
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
                const bool light = ((x * 3u + y * 5u + static_cast<std::uint32_t>(variant)) % 7u) < 3u;
                icon.rgba[offset + 0] = light ? static_cast<std::uint8_t>(210u - (x % 40u)) : 24u;
                icon.rgba[offset + 1] = light ? static_cast<std::uint8_t>(92u + (y % 60u)) : 46u;
                icon.rgba[offset + 2] = light ? 18u : static_cast<std::uint8_t>(80u + (x % 70u));
                icon.rgba[offset + 3] = ((x + y) % 13u == 0u) ? 24u :
                    ((x + y) % 13u == 1u ? 25u : 255u);
            }
        PrepareMapIconGlyph(icon);
        return icon;
    }

    void SetFixtureIcons()
    {
        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) {
            atlas = {};
            atlas.attempted = true;
            atlas.loaded = true;
            atlas.path = "reporter-renderer-offline-fixture";
            atlas.icons.push_back(MakeIcon(0xF001u, 8, 64, 0));
            atlas.icons.push_back(MakeIcon(0xF002u, 64, 8, 1));
            atlas.icons.push_back(MakeIcon(0xF003u, 63, 31, 2));
            atlas.icons.push_back(MakeIcon(0xF004u, 17, 16, 3));
        });
    }

    void CheckUploadedAtlasBytes()
    {
        std::vector<std::uint8_t> expected(128u * 256u * 4u, 0);
        for (std::size_t slot = 0; slot < g_minimapIcons.icons.size(); ++slot)
        {
            const MinimapIcon& icon = g_minimapIcons.icons[slot];
            const std::uint32_t atlasX = static_cast<std::uint32_t>(slot % 2) * 64;
            const std::uint32_t atlasY = static_cast<std::uint32_t>(slot / 2) * 64;
            for (std::uint32_t y = 0; y < icon.height; ++y)
            {
                const std::size_t sourceOffset = static_cast<std::size_t>(y) * icon.width * 4;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(atlasY + y) * 128 + atlasX) * 4;
                std::memcpy(expected.data() + destinationOffset, icon.rgba.data() + sourceOffset,
                    static_cast<std::size_t>(icon.width) * 4);
                for (std::uint32_t x = 0; x < icon.width; ++x)
                {
                    const std::size_t pixel = static_cast<std::size_t>(y) * icon.width + x;
                    const std::size_t glyphPixel =
                        (static_cast<std::size_t>(128 + atlasY + y) * 128 + atlasX + x) * 4;
                    expected[glyphPixel + 3] = pixel < icon.glyph.size() && icon.glyph[pixel] ? 255 : 0;
                }
            }
        }
        ReporterCheck(g_uploadedBytes == expected,
            "uploaded atlas bytes preserve variable-size RGBA entries and the prepared glyph mask");
    }

    std::string ReadText(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    std::vector<std::uint8_t> ReadBytes(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return {};
        const auto size = file.tellg();
        if (size <= 0) return {};
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return {};
        return bytes;
    }

    void CheckPixelsEqual(const std::vector<TestPixel>& expected, const char* message)
    {
        if (expected.size() != g_pixels.size()) ReporterCheck(false, message);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            const TestPixel& a = expected[index];
            const TestPixel& b = g_pixels[index];
            if (a.touched != b.touched || (a.touched &&
                (std::fabs(a.red - b.red) > 0.00001f || std::fabs(a.green - b.green) > 0.00001f ||
                 std::fabs(a.blue - b.blue) > 0.00001f || std::fabs(a.alpha - b.alpha) > 0.00001f)))
            {
                std::cerr << "PIXEL mismatch index=" << index << " expected=" << a.touched << ':' << a.red << ',' << a.green << ',' << a.blue << ',' << a.alpha
                    << " actual=" << b.touched << ':' << b.red << ',' << b.green << ',' << b.blue << ',' << b.alpha << '\n';
                ReporterCheck(false, message);
            }
        }
        ReporterCheck(true, message);
    }

    void LegacyRasterIcon(VulkanMinimapRenderer& renderer, void* commandBuffer,
        int cx, int cy, int radius, int x, int y, const MinimapIcon& icon, bool clipped)
    {
        const int targetWidth = clipped ? MaxValue(18, static_cast<int>(icon.width * 3u / 4u)) : static_cast<int>(icon.width);
        const int targetHeight = clipped ? MaxValue(18, static_cast<int>(icon.height * 3u / 4u)) : static_cast<int>(icon.height);
        const int left = x - targetWidth / 2;
        const int top = y - targetHeight / 2;
        const int radiusSq = radius * radius;
        const bool square = IsSquareMinimap(renderer);
        const bool styled = g_worldMapIconStyle.load();
        if (styled)
        {
            const int diamond = MaxValue(6, MaxValue(targetWidth, targetHeight) / 2 + 1);
            DrawMapIconDiamond(renderer, commandBuffer, cx, cy, radius, x, y, diamond + 2, 0.99f, 0.95f, 0.78f);
            DrawMapIconDiamond(renderer, commandBuffer, cx, cy, radius, x, y, diamond, 0.97f, 0.75f, 0.16f);
        }
        for (int dstY = 0; dstY < targetHeight; ++dstY)
        {
            const int screenY = top + dstY;
            const int dy = screenY - cy;
            const std::uint32_t srcY = MinValue<std::uint32_t>(icon.height - 1u,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(dstY) * icon.height) / static_cast<std::uint32_t>(targetHeight)));
            int runStart = -1;
            float runRed = 0.0f, runGreen = 0.0f, runBlue = 0.0f;
            const auto flushRun = [&](int endX) {
                if (runStart < 0 || endX <= runStart) return;
                CmdClearRect(renderer, commandBuffer, runRed, runGreen, runBlue, 1.0f, runStart, screenY, endX - runStart, 1);
                runStart = -1;
            };
            for (int dstX = 0; dstX < targetWidth; ++dstX)
            {
                const int screenX = left + dstX;
                const int dx = screenX - cx;
                if (square ? MaxValue(std::abs(dx), std::abs(dy)) > radius : dx * dx + dy * dy > radiusSq)
                {
                    flushRun(screenX);
                    continue;
                }
                const std::uint32_t srcX = MinValue<std::uint32_t>(icon.width - 1u,
                    static_cast<std::uint32_t>((static_cast<std::uint64_t>(dstX) * icon.width) / static_cast<std::uint32_t>(targetWidth)));
                const std::size_t offset = (static_cast<std::size_t>(srcY) * icon.width + srcX) * 4u;
                const std::size_t glyphIndex = offset / 4u;
                const std::uint8_t alpha = icon.rgba[offset + 3];
                if (alpha <= 24 || (styled && (glyphIndex >= icon.glyph.size() || !icon.glyph[glyphIndex])))
                {
                    flushRun(screenX);
                    continue;
                }
                const float red = styled ? 0.33f : icon.rgba[offset + 0] / 255.0f;
                const float green = styled ? 0.20f : icon.rgba[offset + 1] / 255.0f;
                const float blue = styled ? 0.05f : icon.rgba[offset + 2] / 255.0f;
                if (runStart >= 0 && std::fabs(runRed - red) < 0.002f &&
                    std::fabs(runGreen - green) < 0.002f && std::fabs(runBlue - blue) < 0.002f)
                    continue;
                flushRun(screenX);
                runStart = screenX;
                runRed = red; runGreen = green; runBlue = blue;
            }
            flushRun(left + targetWidth);
        }
    }

    struct SpriteCase
    {
        std::uint32_t key;
        int clipCenterX, clipCenterY, x, y, radius;
        bool clipped, square, styled;
        float scale;
        int colorSpace;
    };

    int IconIndexFor(std::uint32_t key)
    {
        for (std::size_t index = 0; index < g_minimapIcons.icons.size(); ++index)
            if (g_minimapIcons.icons[index].key == key) return static_cast<int>(index);
        return -1;
    }

    void DrawIconAndOverlays(bool legacy, const SpriteCase& testCase)
    {
        const int iconIndex = IconIndexFor(testCase.key);
        if (iconIndex < 0) { std::cerr << "FAIL: test case icon key missing from frozen fixture: " << testCase.key << std::endl; std::exit(88); }
        if (legacy)
            LegacyRasterIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
                testCase.radius, testCase.x, testCase.y, g_minimapIcons.icons[static_cast<std::size_t>(iconIndex)], testCase.clipped);
        else
            ReporterCheck(TryDrawMinimapRasterIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
                testCase.radius, testCase.x, testCase.y, testCase.key, testCase.clipped),
                "GPU or CPU fallback recognizes each accepted raster icon key");

        if (testCase.key == 0xF004u)
        {
            CmdClearPoiIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
                testCase.radius, testCase.x, testCase.y, 42, false);
            CmdClearPoiIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
                testCase.radius, testCase.x, testCase.y, 11, false);
            DrawCustomPinOutline(g_renderer, nullptr, testCase.x, testCase.y);
        }
    }

    void CheckSpriteRenderer(const std::string& fixtureRoot, const std::string& assetRoot)
    {
        ConfigureFakeRenderer();
        SetFixtureIcons();
        g_testContext.shroudtopia.mod_folder = fixtureRoot;
        g_modContext = &g_testContext;
        const SpriteCase warmup{ 0xF001u, 0, 0, 0, 3, 10, false, false, false, 0.5f, 0 };
        g_renderer.widgetScale = warmup.scale;
        g_renderer.widgetCenterX = warmup.clipCenterX;
        g_renderer.widgetCenterY = warmup.clipCenterY;
        g_renderer.mapShapeSnapshot = 0;
        g_renderer.mapShapeSnapshotValid = true;
        ClearPixels();
        LegacyRasterIcon(g_renderer, nullptr, warmup.clipCenterX, warmup.clipCenterY,
            warmup.radius, warmup.x, warmup.y, g_minimapIcons.icons[0], warmup.clipped);
        const std::vector<TestPixel> warmupExpected = g_pixels;
        ClearPixels();
        ReporterCheck(TryDrawMinimapRasterIcon(g_renderer, nullptr, warmup.clipCenterX, warmup.clipCenterY,
            warmup.radius, warmup.x, warmup.y, warmup.key, warmup.clipped),
            "pre-upload raster icon keeps the accepted CPU fallback available");
        CheckPixelsEqual(warmupExpected, "pre-upload CPU fallback matches the frozen legacy raster oracle");
        RecordFrameTextureUploadIfNeeded(g_renderer, reinterpret_cast<void*>(0x100));
        ReporterCheck(g_copyCalls == 1 && g_copyWidth == 128 && g_copyHeight == 256 && g_barrierCalls == 2,
            "static icon atlas is uploaded once before its first GPU sprite draw");
        CheckUploadedAtlasBytes();
        const std::uint32_t callsAfterUpload = calls;
        RecordFrameTextureUploadIfNeeded(g_renderer, reinterpret_cast<void*>(0x100));
        ReporterCheck(g_copyCalls == 1 && g_barrierCalls == 2 && calls == callsAfterUpload,
            "ready icon atlas does not repeat resource creation or upload per frame");

        const std::string iconVertex = fixtureRoot + "\\minimap_mod\\assets\\embervale_minimap_icon_atlas.vert.spv";
        const std::string iconFragment = fixtureRoot + "\\minimap_mod\\assets\\embervale_minimap_icon_atlas.frag.spv";
        const auto expectedVertex = ReadBytes(iconVertex);
        const auto expectedFragment = ReadBytes(iconFragment);
        if (!expectedVertex.empty() && !expectedFragment.empty())
            ReporterCheck(g_vertexShaderBytes == expectedVertex && g_fragmentShaderBytes == expectedFragment,
                "sprite pipeline selects the packaged icon-atlas shader binaries");

        const std::string fragmentSource = assetRoot + "\\embervale_minimap_icon_atlas.frag";
        const std::string source = ReadText(fragmentSource);
        if (!source.empty())
            ReporterCheck(source.find("roundAwayFromZero") != std::string::npos &&
                source.find("texelFetch") != std::string::npos &&
                source.find("NoContraction") != std::string::npos,
                "fragment source uses integer fetches and explicit tie-away, no-contraction interval mapping");

        const SpriteCase cases[] = {
            { 0xF001u, -1, 0, 1, 4, 9, false, false, false, 0.75f, 0 },
            { 0xF002u, 33, 27, 36, 26, 11, true, true, false, 0.5f, 1000104002 },
            { 0xF003u, 61, 49, 66, 50, 18, false, false, false, 1.25f, 1000104008 },
            { 0xF004u, 15, 17, 14, 16, 12, true, true, true, 2.0f, 0 },
            { 0xF004u, 67, 47, 71, 48, 15, false, false, true, 1.5f, 1000104002 }
        };

        for (std::size_t index = 0; index < std::size(cases); ++index)
        {
            const SpriteCase& testCase = cases[index];
            g_renderer.widgetScale = testCase.scale;
            g_renderer.widgetCenterX = testCase.clipCenterX;
            g_renderer.widgetCenterY = testCase.clipCenterY;
            g_renderer.colorSpace = static_cast<std::uint32_t>(testCase.colorSpace);
            g_renderer.mapShapeSnapshot = testCase.square ? 1u : 0u;
            g_renderer.mapShapeSnapshotValid = true;
            g_worldMapIconStyle.store(testCase.styled);
            ClearPixels();
            DrawIconAndOverlays(true, testCase);
            const std::vector<TestPixel> expected = g_pixels;
            ClearPixels();
            ResetCounters();
            DrawIconAndOverlays(false, testCase);
            CheckPixelsEqual(expected, "GPU sprite and following primitive/custom pin output match the frozen CPU oracle");
            const int iconIndex = IconIndexFor(testCase.key);
            const MinimapIcon& icon = g_minimapIcons.icons[static_cast<std::size_t>(iconIndex)];
            ReporterCheck(g_lastPush.sourceRect[0] == static_cast<float>((iconIndex % 2) * 64) &&
                g_lastPush.sourceRect[1] == static_cast<float>((iconIndex / 2) * 64) &&
                g_lastPush.sourceRect[2] == static_cast<float>(icon.width) &&
                g_lastPush.sourceRect[3] == static_cast<float>(icon.height) && g_lastPush.atlas[0] == 128.0f,
                "sprite push selects the expected color tile and matching glyph plane");
            ReporterCheck(g_drawCalls == 1 && g_lastPushSize == sizeof(g_lastPush) &&
                g_pushCalls == 1,
                "one raster icon records one ordered opaque atlas draw");
        }

        const SpriteCase unsupportedScale{ 0xF003u, 63, 47, 70, 49, 16, false, false, false, 0.25f, 0 };
        g_renderer.widgetScale = unsupportedScale.scale;
        g_renderer.widgetCenterX = unsupportedScale.clipCenterX;
        g_renderer.widgetCenterY = unsupportedScale.clipCenterY;
        ClearPixels();
        LegacyRasterIcon(g_renderer, nullptr, unsupportedScale.clipCenterX, unsupportedScale.clipCenterY,
            unsupportedScale.radius, unsupportedScale.x, unsupportedScale.y,
            g_minimapIcons.icons[2], unsupportedScale.clipped);
        const std::vector<TestPixel> unsupportedExpected = g_pixels;
        ClearPixels();
        ResetCounters();
        DrawIconAndOverlays(false, unsupportedScale);
        CheckPixelsEqual(unsupportedExpected, "unsupported fractional scale keeps exact CPU raster fallback output");
        ReporterCheck(g_drawCalls == 0 && g_clearCalls > 0,
            "unsupported fractional scale bypasses the GPU sprite draw");

        g_worldMapIconStyle.store(false);
        DestroyVulkanMinimapRendererLocked();
        ReporterCheck(live.empty() && bindings.empty(), "per-generation icon-atlas resources teardown after device idle");
        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) { atlas = {}; });
    }

    void CheckSpriteNegativeCellMapping(const std::string& fixtureRoot)
    {
        struct BoundaryCase
        {
            float scale;
            int clipCenterX, clipCenterY, iconX, iconY, radius;
            int sampleX, sampleY, expectedLogicalX, expectedLogicalY;
        };
        const BoundaryCase cases[] = {
            { 0.5f, 5, 5, 0, 5, 6, 5, 2, 5, -1 },
            { 0.75f, 2, 2, 0, 2, 3, 2, 0, 2, -1 },
            { 1.25f, 1, 1, 1, 0, 2, 1, 0, 1, 0 },
            { 1.5f, 1, 1, 1, 0, 2, 1, 0, 1, 0 },
            { 2.0f, 1, 1, 1, 0, 2, 1, 0, 1, 0 },
            { 0.75f, 3, 10, 0, 0, 12, 3, 1, 3, -2 }
        };

        ConfigureFakeRenderer();
        SetFixtureIcons();

        MinimapIcon icon = MakeIcon(0xF005u, 17, 31, 4);
        for (std::size_t pixel = 0; pixel < icon.rgba.size() / 4; ++pixel)
        {
            icon.rgba[pixel * 4 + 0] = 16;
            icon.rgba[pixel * 4 + 1] = 100;
            icon.rgba[pixel * 4 + 2] = 112;
            icon.rgba[pixel * 4 + 3] = 255;
        }
        const std::size_t boundaryTexel = (9u * icon.width + 13u) * 4u;
        icon.rgba[boundaryTexel + 3] = 25;
        MutateMinimapIconAtlasForTest([&](MinimapIconAtlas& atlas) {
            atlas.icons.clear();
            atlas.icons.push_back(icon);
        });

        g_testContext.shroudtopia.mod_folder = fixtureRoot;
        g_modContext = &g_testContext;
        g_renderer.width = 160;
        g_renderer.height = 120;
        g_renderer.widgetScale = 0.5f;
        g_renderer.widgetCenterX = 5;
        g_renderer.widgetCenterY = 5;
        g_renderer.mapShapeSnapshot = 0;
        g_renderer.mapShapeSnapshotValid = true;
        g_renderer.colorSpace = 0;
        g_worldMapIconStyle.store(false);
        SetViewport(160, 120);

        const BoundaryCase& firstCase = cases[0];
        int negativeXLocal = -1;
        ReporterCheck(TestLogicalCell(2, -8, static_cast<int>(icon.width), 5, 0.5f, negativeXLocal) &&
            -8 + negativeXLocal == -1,
            "negative global X is valid in interval lookup before the legacy parity guard");
        LegacyRasterIcon(g_renderer, nullptr, firstCase.clipCenterX, firstCase.clipCenterY,
            firstCase.radius, firstCase.iconX, firstCase.iconY, icon, false);
        const std::vector<TestPixel> firstExpected = g_pixels;
        const TestPixel& negativeBoundaryPixel = firstExpected[2u * 160u + 5u];
        ReporterCheck(negativeBoundaryPixel.touched &&
            std::fabs(negativeBoundaryPixel.red - 16.0f / 255.0f) < 0.00001f &&
            std::fabs(negativeBoundaryPixel.green - 100.0f / 255.0f) < 0.00001f &&
            std::fabs(negativeBoundaryPixel.blue - 112.0f / 255.0f) < 0.00001f,
            "frozen legacy oracle paints the valid logical -1 boundary pixel");

        ClearPixels();
        ReporterCheck(TryDrawMinimapRasterIcon(g_renderer, nullptr, firstCase.clipCenterX,
            firstCase.clipCenterY, firstCase.radius, firstCase.iconX, firstCase.iconY, 0xF005u, false),
            "negative-boundary icon remains available through pre-upload CPU fallback");
        CheckPixelsEqual(firstExpected, "pre-upload CPU fallback matches negative-boundary legacy pixels");
        RecordFrameTextureUploadIfNeeded(g_renderer, reinterpret_cast<void*>(0x100));

        for (const BoundaryCase& testCase : cases)
        {
            g_renderer.widgetScale = testCase.scale;
            g_renderer.widgetCenterX = testCase.clipCenterX;
            g_renderer.widgetCenterY = testCase.clipCenterY;

            const int logicalLeft = testCase.iconX - static_cast<int>(icon.width) / 2;
            const int logicalTop = testCase.iconY - static_cast<int>(icon.height) / 2;
            int localIndex = -1;
            const bool xValid = TestLogicalCell(testCase.sampleX, logicalLeft, static_cast<int>(icon.width),
                testCase.clipCenterX, testCase.scale, localIndex);
            const int mappedX = logicalLeft + localIndex;
            const bool yValid = TestLogicalCell(testCase.sampleY, logicalTop, static_cast<int>(icon.height),
                testCase.clipCenterY, testCase.scale, localIndex);
            const int mappedY = logicalTop + localIndex;
            ReporterCheck(xValid && yValid && mappedX == testCase.expectedLogicalX &&
                mappedY == testCase.expectedLogicalY,
                "local interval lookup validates before reconstructing negative or near-zero logical cells");

            const int firstBoundary = TestBoundary(logicalTop, testCase.clipCenterY, testCase.scale);
            const int endBoundary = TestBoundary(logicalTop + static_cast<int>(icon.height),
                testCase.clipCenterY, testCase.scale);
            const bool beforeValid = TestLogicalCell(firstBoundary - 1, logicalTop,
                static_cast<int>(icon.height), testCase.clipCenterY, testCase.scale, localIndex);
            const bool endValid = TestLogicalCell(endBoundary, logicalTop,
                static_cast<int>(icon.height), testCase.clipCenterY, testCase.scale, localIndex);
            ReporterCheck(!beforeValid && !endValid,
                "logical cells immediately outside both transformed interval boundaries stay invalid");

            const int zeroBoundary = TestBoundary(0, testCase.clipCenterY, testCase.scale);
            const int zeroNextBoundary = TestBoundary(1, testCase.clipCenterY, testCase.scale);
            const int resolvedZeroCell = zeroBoundary == zeroNextBoundary ? 1 : 0;
            const bool zeroValid = TestLogicalCell(zeroBoundary, logicalTop, static_cast<int>(icon.height),
                testCase.clipCenterY, testCase.scale, localIndex);
            ReporterCheck(zeroValid && logicalTop + localIndex == resolvedZeroCell,
                "near-zero logical boundary resolves correctly when neighboring intervals collapse");

            ClearPixels();
            LegacyRasterIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
                testCase.radius, testCase.iconX, testCase.iconY, icon, false);
            const std::vector<TestPixel> expected = g_pixels;
            ReporterCheck(expected[static_cast<std::size_t>(testCase.sampleY) * 160u + testCase.sampleX].touched,
                "frozen legacy oracle paints the selected negative or near-zero sample pixel");
            if (&testCase == &cases[0])
                ReporterCheck(!expected[5u * 160u + 2u].touched,
                    "frozen legacy output suppresses an onscreen negative-X run start");

            ClearPixels();
            ResetCounters();
            ReporterCheck(TryDrawMinimapRasterIcon(g_renderer, nullptr, testCase.clipCenterX,
                testCase.clipCenterY, testCase.radius, testCase.iconX, testCase.iconY, 0xF005u, false),
                "negative-boundary icon selects the ready GPU atlas path");
            ReporterCheck(g_drawCalls == 1, "mapping regression exercises one GPU sprite draw");
            if (&testCase == &cases[0])
                ReporterCheck(!g_pixels[5u * 160u + 2u].touched,
                    "GPU sprite preserves negative-X run-start suppression for parity");
            CheckPixelsEqual(expected, "GPU sprite mapping matches the frozen legacy pixel oracle");
        }
    }

    void CheckSpriteUnavailableFallback(const std::string& missingFixtureRoot)
    {
        ConfigureFakeRenderer();
        SetFixtureIcons();
        g_testContext.shroudtopia.mod_folder = missingFixtureRoot;
        g_modContext = &g_testContext;
        g_renderer.mapShapeSnapshot = 0;
        g_renderer.mapShapeSnapshotValid = true;
        const SpriteCase testCase{ 0xF001u, 64, 48, 68, 50, 14, false, false, false, 1.0f, 0 };
        ClearPixels();
        LegacyRasterIcon(g_renderer, nullptr, testCase.clipCenterX, testCase.clipCenterY,
            testCase.radius, testCase.x, testCase.y, g_minimapIcons.icons[0], testCase.clipped);
        const std::vector<TestPixel> expected = g_pixels;

        ClearPixels();
        const bool fallbackDrawResult = TryDrawMinimapRasterIcon(g_renderer, nullptr, testCase.clipCenterX,
            testCase.clipCenterY, testCase.radius, testCase.x, testCase.y, testCase.key, testCase.clipped);
        ReporterCheck(fallbackDrawResult,
            "missing optional shader keeps the raster icon available through CPU fallback");
        CheckPixelsEqual(expected, "missing optional shader preserves legacy CPU raster pixels");
        ReporterCheck(g_renderer.iconAtlasUnavailable && !g_renderer.iconAtlasResources.frameTextureReady &&
            live.empty() && bindings.empty(),
            "failed icon-atlas setup releases its partial resources and stays local");
        const int setupCalls = calls;

        ClearPixels();
        ReporterCheck(TryDrawMinimapRasterIcon(g_renderer, nullptr, testCase.clipCenterX,
            testCase.clipCenterY, testCase.radius, testCase.x, testCase.y, testCase.key, testCase.clipped),
            "CPU fallback remains available after the atlas failure is cached");
        CheckPixelsEqual(expected, "repeated optional-resource failure keeps legacy CPU pixels");
        ReporterCheck(calls == setupCalls,
            "unavailable icon-atlas resources are not recreated on every raster icon");

        DestroyVulkanMinimapRendererLocked();
        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) { atlas = {}; });
    }
    void LegacyWeatherLabel(VulkanMinimapRenderer& renderer, void* commandBuffer, int x, int cy, WeatherKind kind)
    {
        static constexpr char letters[] = "ABCDEILNORSWZ";
        static constexpr std::uint8_t glyphs[][7] = {
            {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
            {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
            {31,16,16,30,16,16,31}, {14,4,4,4,4,4,14},
            {16,16,16,16,16,16,31}, {17,25,25,21,19,19,17},
            {14,17,17,17,17,17,14}, {30,17,17,30,20,18,17},
            {15,16,16,14,1,1,30}, {17,17,17,21,21,21,10},
            {31,1,2,4,8,16,31}
        };
        std::vector<VkClearRect> ink;
        const char* name = WeatherName(kind);
        for (int i = 0; name[i]; ++i)
        {
            const char* letter = std::strchr(letters, name[i]);
            if (!letter) continue;
            for (int y = 0; y < 7; ++y)
                for (int bit = 0; bit < 5; ++bit)
                    if (glyphs[letter - letters][y] & (1 << (4 - bit)))
                        AppendClippedClearRect(renderer, ink, x + i * 12 + bit * 2, cy - 7 + y * 2, 2, 2);
        }
        CmdClearRects(renderer, commandBuffer, 0.96f, 0.89f, 0.69f, 1, ink);
    }

    void LegacyWeatherSymbol(VulkanMinimapRenderer& renderer, void* commandBuffer,
        int cx, int cy, WeatherKind kind, bool daytime, bool daylightKnown = true)
    {
        const bool clear = kind == WeatherKind::Clear;
        std::vector<VkClearRect> celestial, cloud, precipitation;
        for (int y = -12; y <= 12; ++y)
            for (int x = -14; x <= 14; ++x)
            {
                const int sx = clear ? x : x + 5;
                const int sy = clear ? y : y + 4;
                const int d = sx * sx + sy * sy;
                const bool sun = d <= 16 || (d >= 36 && d <= 72 &&
                    (std::abs(sx) <= 1 || std::abs(sy) <= 1 || std::abs(std::abs(sx) - std::abs(sy)) <= 1));
                const bool moon = d <= 49 && (sx - 3) * (sx - 3) + (sy + 2) * (sy + 2) > 36;
                if (daylightKnown && (daytime ? sun : moon))
                    AppendClippedClearRect(renderer, celestial, cx + x, cy + y, 1, 1);
                if (clear) continue;
                const bool body = (y >= -1 && y <= 3 && x >= -7 && x <= 12) ||
                    (x + 2) * (x + 2) + (y + 2) * (y + 2) <= 16 ||
                    (x - 5) * (x - 5) + (y + 2) * (y + 2) <= 25;
                if (body) AppendClippedClearRect(renderer, cloud, cx + x, cy + y, 1, 1);
                bool detail = false;
                if (kind == WeatherKind::Rain)
                    detail = y >= 6 && y <= 11 && x >= -6 && x <= 11 && ((x + y / 2) % 6 == 0 || (x + y / 2) % 6 == 1);
                else if (kind == WeatherKind::Snow)
                {
                    for (int flakeX : { -4, 7 })
                    {
                        const int dx = x - flakeX, dy = y - 8;
                        detail |= std::abs(dx) <= 3 && std::abs(dy) <= 3 &&
                            (dx == 0 || dy == 0 || std::abs(dx) == std::abs(dy));
                    }
                }
                else if (kind == WeatherKind::Blizzard)
                {
                    const int dx = x - 8, dy = y - 8;
                    detail = (x >= -10 && x <= 1 && (y == 7 || y == 10)) ||
                        (std::abs(dx) <= 3 && std::abs(dy) <= 3 &&
                 (dx == 0 || dy == 0 || std::abs(dx) == std::abs(dy)));
                }
                if (detail) AppendClippedClearRect(renderer, precipitation, cx + x, cy + y, 1, 1);
            }
        CmdClearRects(renderer, commandBuffer, daytime ? 1.0f : 0.74f,
            daytime ? 0.78f : 0.88f, daytime ? 0.30f : 0.98f, 1, celestial);
        CmdClearRects(renderer, commandBuffer, 0.75f, 0.82f, 0.87f, 1, cloud);
        CmdClearRects(renderer, commandBuffer, 0.60f, 0.85f, 1.0f, 1, precipitation);
    }

    void LegacyClockPanel(VulkanMinimapRenderer& renderer, void* commandBuffer,
        int cx, int cy, const WorldClockSnapshot& clock, const WorldWeatherSnapshot& weather, bool showTime)
    {
        const bool hasTime = showTime && clock.valid && clock.minuteOfDay >= 0 && clock.minuteOfDay < 1440;
        const bool hasWeather = weather.valid && weather.kind <= 3;
        if (!hasTime && !hasWeather) return;
        const int extraWidth = IsSquareMinimap(renderer) ? 50 : hasTime && hasWeather ? 50 : hasWeather ? 12 : 0;
        const int timeX = hasWeather ? cx - 72 : cx + 7;
        const auto plate = [&](int halfWidth, int halfHeight, float r, float g, float b) {
            halfWidth += extraWidth;
            std::vector<VkClearRect> rows;
            rows.reserve(halfHeight * 2 + 1);
            for (int y = -halfHeight; y <= halfHeight; ++y)
            {
                const int cut = MaxValue(0, std::abs(y) - halfHeight + 7);
                AppendClippedClearRect(renderer, rows, cx - halfWidth + cut, cy + y,
                    2 * (halfWidth - cut) + 1, 1);
            }
            CmdClearRects(renderer, commandBuffer, r, g, b, 1.0f, rows);
        };
        plate(74, CLOCK_PANEL_HALF_HEIGHT, 0.025f, 0.022f, 0.018f);
        plate(72, 16, 0.65f, 0.48f, 0.25f);
        plate(71, 15, 0.23f, 0.17f, 0.09f);
        plate(69, 13, 0.76f, 0.59f, 0.32f);
        plate(68, 12, 0.032f, 0.042f, 0.043f);
        CmdClearRimJewel(renderer, commandBuffer, cx - 72 - extraWidth, cy);
        CmdClearRimJewel(renderer, commandBuffer, cx + 72 + extraWidth, cy);
        if (hasTime)
        {
            static constexpr std::uint8_t digits[10][7] = {
                {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
                {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
                {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
                {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
                {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
            };
            const int hours = clock.minuteOfDay / 60, minutes = clock.minuteOfDay % 60;
            const int values[5] = { hours / 10, hours % 10, -1, minutes / 10, minutes % 10 };
            std::vector<VkClearRect> ink;
            ink.reserve(150);
            for (int i = 0; i < 5; ++i)
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 5; ++x)
                    {
                        const bool lit = values[i] < 0 ? (x == 2 && (y == 2 || y == 5)) :
                            (digits[values[i]][y] & (1 << (4 - x))) != 0;
                        if (lit) AppendClippedClearRect(renderer, ink, timeX - 29 + i * 12 + x * 2,
                            cy - 7 + y * 2, 2, 2);
                    }
            CmdClearRects(renderer, commandBuffer, 0.96f, 0.89f, 0.69f, 1.0f, ink);
            if (!hasWeather) LegacyWeatherSymbol(renderer, commandBuffer, cx - 44, cy, WeatherKind::Clear, clock.daytime);
        }
        if (hasWeather)
        {
            const auto kind = static_cast<WeatherKind>(weather.kind);
            if (hasTime)
            {
                std::vector<VkClearRect> divider;
                AppendClippedClearRect(renderer, divider, cx - 32, cy - 7, 1, 14);
                CmdClearRects(renderer, commandBuffer, 0.38f, 0.30f, 0.17f, 1, divider);
            }
            const int weatherX = hasTime ? cx - 10 : cx - 58;
            LegacyWeatherSymbol(renderer, commandBuffer, weatherX, cy, kind, clock.daytime, clock.valid);
            LegacyWeatherLabel(renderer, commandBuffer, weatherX + 26, cy, kind);
        }
    }

    void CheckPanelCache()
    {
        VulkanMinimapRenderer renderer{};
        renderer.width = g_testWidth = 160;
        renderer.height = g_testHeight = 120;
        renderer.widgetScale = 1.0f;
        renderer.widgetCenterX = 80;
        renderer.widgetCenterY = 60;
        renderer.mapShapeSnapshot = 0;
        renderer.mapShapeSnapshotValid = true;
        renderer.fns.cmdClearAttachments = TestClearAttachments;
        WorldClockSnapshot clock{};
        clock.valid = true;
        clock.minuteOfDay = 615;
        clock.daytime = true;
        WorldWeatherSnapshot weather{};
        weather.valid = true;
        weather.kind = static_cast<std::uint32_t>(WeatherKind::Rain);

        ClearPixels();
        ResetCounters();
        LegacyClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        const std::vector<TestPixel> expected = g_pixels;
        const std::uint32_t legacyRectCount = g_clearRects;
        const std::uint32_t legacyClearCalls = g_clearCalls;

        ClearPixels();
        ResetCounters();
        DrawWorldClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        CheckPixelsEqual(expected, "cached clock/weather geometry preserves the frozen legacy pixel output");
        const std::uint32_t compactRectCount = g_clearRects;
        ReporterCheck(compactRectCount < legacyRectCount,
            "same-colour clock/weather rectangles are compacted while layers remain ordered");
        ReporterCheck(g_clearCalls == legacyClearCalls,
            "panel cache keeps the legacy clear-call layer boundaries");
        std::cout << "METRIC: panel legacy_rects=" << legacyRectCount << " compact_rects=" << compactRectCount
            << " legacy_calls=" << legacyClearCalls << " compact_calls=" << g_clearCalls << '\n';

        ClearPixels();
        g_trackedAllocations = 0;
        g_trackAllocations = true;
        DrawWorldClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        g_trackAllocations = false;
        ReporterCheck(g_trackedAllocations == 0,
            "identical clock/weather redraw reuses cached geometry without heap allocation");
        CheckPixelsEqual(expected, "cached repeat retains the same ordered clock/weather output");

        const float scales[] = { 0.5f, 1.25f, 2.0f };
        const int centers[][2] = { { 0, 0 }, { 81, 61 }, { 80, 61 } };
        for (std::size_t index = 0; index < std::size(scales); ++index)
        {
            renderer.mapShapeSnapshot = index == 1 ? 1u : 0u;
            renderer.widgetScale = scales[index];
            renderer.widgetCenterX = centers[index][0];
            renderer.widgetCenterY = centers[index][1];
            const int cx = centers[index][0] - 1;
            const int cy = centers[index][1] + 2;
            ClearPixels();
            LegacyClockPanel(renderer, nullptr, cx, cy, clock, weather, true);
            const std::vector<TestPixel> stateExpected = g_pixels;
            ClearPixels();
            DrawWorldClockPanel(renderer, nullptr, cx, cy, clock, weather, true);
            CheckPixelsEqual(stateExpected, "shape, fractional scale, viewport edge and placement changes rebuild exact panel geometry");
        }

        renderer.widgetScale = 1.0f;
        renderer.widgetCenterX = 80;
        renderer.widgetCenterY = 60;
        renderer.mapShapeSnapshot = 0;
        clock.minuteOfDay = 616;
        weather.kind = static_cast<std::uint32_t>(WeatherKind::Snow);
        ClearPixels();
        LegacyClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        const std::vector<TestPixel> changedExpected = g_pixels;
        ClearPixels();
        DrawWorldClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        CheckPixelsEqual(changedExpected, "minute and weather-kind changes invalidate clock/weather geometry");

        clock.daytime = false;
        ClearPixels();
        LegacyClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        const std::vector<TestPixel> nightExpected = g_pixels;
        ClearPixels();
        DrawWorldClockPanel(renderer, nullptr, 80, 60, clock, weather, true);
        CheckPixelsEqual(nightExpected, "daylight changes invalidate clock/weather geometry");

        clock.valid = false;
        weather.valid = true;
        ClearPixels();
        LegacyClockPanel(renderer, nullptr, 80, 60, clock, weather, false);
        const std::vector<TestPixel> weatherOnlyExpected = g_pixels;
        ClearPixels();
        DrawWorldClockPanel(renderer, nullptr, 80, 60, clock, weather, false);
        CheckPixelsEqual(weatherOnlyExpected, "daylight validity and weather-only state preserve the legacy panel");
    }
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--panel") == 0)
    {
        SetViewport(160, 120);
        CheckPanelCache();
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--fallback") == 0)
    {
        CheckSpriteUnavailableFallback(argv[2]);
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--sprite-mapping") == 0)
    {
        CheckSpriteNegativeCellMapping(argv[2]);
        return 0;
    }
    if (argc != 4 || std::strcmp(argv[1], "--sprite") != 0)
    {
        std::cerr << "Expected --panel, --fallback <fixture root>, --sprite-mapping <shader fixture root>, or --sprite <shader fixture root> <shader source assets directory>.\n";
        return 2;
    }
    CheckSpriteRenderer(argv[2], argv[3]);
    return 0;
}
