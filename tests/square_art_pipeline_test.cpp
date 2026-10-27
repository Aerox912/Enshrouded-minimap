#pragma warning(push)
#pragma warning(disable: 4715)
#define main FrameFailureSetupMain
#include "frame_failure_test.cpp"
#undef main
#pragma warning(pop)
#include <fstream>
#include <iterator>
#include <vector>
#include <cmath>

namespace {
    std::vector<std::uint8_t> uploadedBytes;
    std::uint64_t stagingBytes = 0;
    std::uint32_t barrierCount = 0, copyCount = 0;
    std::uint32_t copyWidth = 0, copyHeight = 0, copyLayout = 0;
    std::uint32_t drawCount = 0, drawVertices = 0, drawInstances = 0;
    std::uint32_t pushCount = 0, pushSize = 0, pushStages = 0;
    float push[8]{};
    float viewportWidth = 0, viewportHeight = 0;
    std::uint32_t scissorWidth = 0, scissorHeight = 0;
    int alphaOnePixels = 0;
    std::vector<std::uint8_t> capturedVertexShader, capturedFragmentShader;
    int artShaderModuleCount = 0;
    std::uint32_t clearAttachmentCalls = 0;

    static ModContext artContext;
    void CheckArt(bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
        std::cout << "PASS: " << label << '\n';
    }
    std::int32_t __fastcall ArtRequirements(void*, void* object, VkMemoryRequirements* req) {
        req->size = live.count(object) && live[object] == "buffer" ? stagingBytes : 64;
        req->alignment = 1; req->memoryTypeBits = 1; return VK_SUCCESS;
    }
    std::int32_t __fastcall ArtMap(void*, void*, std::uint64_t, std::uint64_t size, std::uint32_t, void** data) {
        uploadedBytes.resize(static_cast<std::size_t>(size)); *data = uploadedBytes.data(); return VK_SUCCESS;
    }
    std::int32_t __fastcall ArtFlush(void*, std::uint32_t count, const VkMappedMemoryRange*) {
        return count == 1 ? VK_SUCCESS : -11;
    }
    std::int32_t __fastcall ArtPipeline(void*, void*, std::uint32_t count,
        const VkGraphicsPipelineCreateInfo* infos, const void*, void** pipeline) {
        CheckArt(count == 1 && infos && infos->pViewportState && infos->pViewportState->pViewports &&
            infos->pViewportState->pScissors, "square frame pipeline has explicit viewport and scissor");
        viewportWidth = infos->pViewportState->pViewports[0].width;
        viewportHeight = infos->pViewportState->pViewports[0].height;
        scissorWidth = infos->pViewportState->pScissors[0].extent.width;
        scissorHeight = infos->pViewportState->pScissors[0].extent.height;
        return Create("pipeline", pipeline);
    }
    void __fastcall ArtBarrier(void*, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t, const void*, std::uint32_t, const void*, std::uint32_t imageCount,
        const VkImageMemoryBarrier* images) {
        if (imageCount == 1 && images) {
            if (barrierCount == 0)
                CheckArt(images[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                    images[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    "first texture barrier transitions undefined image to transfer destination");
            if (barrierCount == 1)
                CheckArt(images[0].oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                    images[0].newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    "second texture barrier makes uploaded image shader-readable");
        }
        ++barrierCount;
    }
    void __fastcall ArtCopy(void*, void*, void*, std::uint32_t layout, std::uint32_t count,
        const VkBufferImageCopy* regions) {
        ++copyCount; copyLayout = layout;
        if (count == 1 && regions) { copyWidth = regions[0].imageExtent.width; copyHeight = regions[0].imageExtent.height; }
    }
    void __fastcall ArtBind(void*, std::uint32_t, void*) {}
    void __fastcall ArtBindSets(void*, std::uint32_t, void*, std::uint32_t,
        std::uint32_t, void* const*, std::uint32_t, const std::uint32_t*) {}
    void __fastcall ArtPush(void*, void*, std::uint32_t stages, std::uint32_t offset,
        std::uint32_t size, const void* values) {
        ++pushCount; pushStages = stages; pushSize = size;
        if (offset == 0 && size == sizeof(push) && values) std::memcpy(push, values, size);
    }
    void __fastcall ArtDraw(void*, std::uint32_t vertices, std::uint32_t instances, std::uint32_t, std::uint32_t) {
        ++drawCount; drawVertices = vertices; drawInstances = instances;
    }
    bool NearArt(float a, float b) { return std::fabs(a-b) < 0.02f; }
    std::vector<std::uint8_t> ReadBytes(const char* path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return {};
        auto n = file.tellg(); std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
        file.seekg(0); if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(n))) return {};
        return bytes;
    }
    std::int32_t __fastcall ArtShaderModule(void*, const VkShaderModuleCreateInfo* info, const void*, void** handle)
    {
        if (info == nullptr || info->pCode == nullptr || info->codeSize == 0)
            return -11;
        const auto* first = reinterpret_cast<const std::uint8_t*>(info->pCode);
        auto& captured = (++artShaderModuleCount == 1) ? capturedVertexShader : capturedFragmentShader;
        captured.assign(first, first + info->codeSize);
        return Create(artShaderModuleCount == 1 ? "vertex-module" : "fragment-module", handle);
    }

    void __fastcall ArtClear(void*, std::uint32_t, const VkClearAttachment*,
        std::uint32_t rectCount, const VkClearRect* rects)
    {
        if (rectCount > 0 && rects != nullptr)
            ++clearAttachmentCalls;
    }

    std::string ReadText(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    struct AlphaBounds
    {
        int minX = 1254, minY = 1254, maxX = -1, maxY = -1;
        std::uint64_t count = 0;
    };

    bool SquareClipAcceptsTexelCenter(int x, int y)
    {
        const float u = (static_cast<float>(x) + 0.5f) / 1254.0f;
        const float v = (static_cast<float>(y) + 0.5f) / 1254.0f;
        return u >= 77.0f / 1254.0f && u < 1178.0f / 1254.0f &&
            v >= 66.0f / 1254.0f && v < 1164.0f / 1254.0f;
    }

    AlphaBounds FindUploadedAlphaBounds(const std::vector<std::uint8_t>& bytes, bool applySquareClip)
    {
        AlphaBounds result{};
        if (bytes.size() != 1254u * 1254u * 4u)
            return result;
        for (int y = 0; y < 1254; ++y)
        {
            for (int x = 0; x < 1254; ++x)
            {
                const std::size_t alphaOffset = (static_cast<std::size_t>(y) * 1254u + static_cast<std::size_t>(x)) * 4u + 3u;
                if (bytes[alphaOffset] == 0 || (applySquareClip && !SquareClipAcceptsTexelCenter(x, y)))
                    continue;
                ++result.count;
                result.minX = MinValue(result.minX, x);
                result.minY = MinValue(result.minY, y);
                result.maxX = MaxValue(result.maxX, x);
                result.maxY = MaxValue(result.maxY, y);
            }
        }
        return result;
    }

    std::string FixtureAssetPath(const char* root, const char* name)
    {
        return std::string(root) + "\\minimap_mod\\assets\\" + name;
    }

    void SetupArtRenderer(const char* fixtureRoot, std::uint32_t colorSpace = 0)
    {
        artContext.shroudtopia.mod_folder = fixtureRoot;
        artContext.Log = [](const char*) {};
        g_modContext = &artContext;
        capturedVertexShader.clear();
        capturedFragmentShader.clear();
        uploadedBytes.clear();
        artShaderModuleCount = 0;
        clearAttachmentCalls = 0;
        pushCount = drawCount = 0;
        barrierCount = copyCount = 0;
        failure.clear();
        failureMode = 0;
        Setup(colorSpace);
        g_renderer.width = 1400;
        g_renderer.height = 1200;
        g_renderer.renderPass = 0x200;
        g_renderer.fns.getBufferMemoryRequirements = reinterpret_cast<decltype(g_renderer.fns.getBufferMemoryRequirements)>(&ArtRequirements);
        g_renderer.fns.getImageMemoryRequirements = reinterpret_cast<decltype(g_renderer.fns.getImageMemoryRequirements)>(&ArtRequirements);
        g_renderer.fns.mapMemory = ArtMap;
        g_renderer.fns.flushMappedMemoryRanges = ArtFlush;
        g_renderer.fns.createShaderModule = ArtShaderModule;
        g_renderer.fns.createGraphicsPipelines = ArtPipeline;
        g_renderer.fns.cmdPipelineBarrier = ArtBarrier;
        g_renderer.fns.cmdCopyBufferToImage = ArtCopy;
        g_renderer.fns.cmdBindPipeline = ArtBind;
        g_renderer.fns.cmdBindDescriptorSets = ArtBindSets;
        g_renderer.fns.cmdPushConstants = ArtPush;
        g_renderer.fns.cmdDraw = ArtDraw;
        g_renderer.fns.cmdClearAttachments = ArtClear;
    }
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "Expected missing-fragment root, complete-fragment root, raw RGBA path, and square shader source path.\n";
        return 2;
    }

    const std::vector<std::uint8_t> expectedRaw = ReadBytes(argv[3]);
    const std::vector<std::uint8_t> expectedSquareFragment = ReadBytes(
        FixtureAssetPath(argv[2], "embervale_minimap_square_frame.frag.spv").c_str());
    const std::vector<std::uint8_t> expectedRoundFragment = ReadBytes(
        FixtureAssetPath(argv[2], "embervale_minimap_frame.frag.spv").c_str());
    const std::string squareShaderSource = ReadText(argv[4]);
    stagingBytes = expectedRaw.size();

    CheckArt(stagingBytes == 1254ull * 1254ull * 4ull, "approved S1 raw asset is 1254x1254 RGBA8");
    CheckArt(!expectedSquareFragment.empty() && !expectedRoundFragment.empty(),
        "fixture contains exact square and unchanged round frame fragment binaries");
    CheckArt(squareShaderSource.find("vec2(77.0, 66.0) / 1254.0") != std::string::npos &&
        squareShaderSource.find("vec2(1178.0, 1164.0) / 1254.0") != std::string::npos &&
        squareShaderSource.find("color = texture(frameTexture, uv);") != std::string::npos,
        "square fragment clips the calibrated UV bounds and retains direct implicit texture sampling");

    int squareMax = 0, squareBeyond = 0, roundMax = 0, roundBeyond = 0;
    CheckArt(TryResolveSquareFrameRgbaDimensions(2048ll * 2048ll * 4ll, squareMax) &&
        squareMax == 2048 && !TryResolveSquareFrameRgbaDimensions(2049ll * 2049ll * 4ll, squareBeyond),
        "square frame resolver accepts 2048 and rejects 2049");
    CheckArt(TryResolveFrameRgbaDimensions(1024ll * 1024ll * 4ll, roundMax) && roundMax == 1024 &&
        !TryResolveFrameRgbaDimensions(1025ll * 1025ll * 4ll, roundBeyond) &&
        TryResolveSquareFrameRgbaDimensions(1254ll * 1254ll * 4ll, squareMax) && squareMax == 1254 &&
        !TryResolveFrameRgbaDimensions(1254ll * 1254ll * 4ll, roundMax),
        "square-only frame dimensions exceed the unchanged circular 1024 limit");

    {
        std::lock_guard<std::mutex> lock(g_squareMinimapFrameMutex);
        g_squareMinimapFrame = {};
    }
    SetupArtRenderer(argv[2]);
    PublishMinimapShape(MinimapShape::Square);
    CaptureMinimapShapeSnapshot(g_renderer);
    CheckArt(PrepareSquareFrameResourcesLocked(g_renderer) &&
        g_renderer.squareFrameResources.frameTextureReady && g_renderer.squareFrameResources.frameTextureUploadPending,
        "actual square frame preparation publishes the square pipeline and texture");
    const bool selectedSquareFragment = capturedFragmentShader == expectedSquareFragment;
    const bool selectedRoundFragment = capturedFragmentShader == expectedRoundFragment;
    std::cout << "MEASURE square pipeline selected square SPIR-V=" << selectedSquareFragment << " round SPIR-V=" << selectedRoundFragment << std::endl;
    CheckArt(selectedSquareFragment || selectedRoundFragment,
        "actual square frame preparation loads a recognized frame fragment binary");

    CheckArt(g_squareMinimapFrame.rgba == expectedRaw,
        "square loader retains the exact immutable S1 bytes");
    bool uploadMatches = uploadedBytes.size() == expectedRaw.size();
    alphaOnePixels = 0;
    if (uploadMatches)
    {
        for (std::size_t i = 0; i < expectedRaw.size(); ++i)
        {
            std::uint8_t expected = expectedRaw[i];
            if ((i % 4) == 3 && expected <= 1)
            {
                if (expected == 1) ++alphaOnePixels;
                expected = 0;
            }
            if (uploadedBytes[i] != expected)
            {
                uploadMatches = false;
                break;
            }
        }
    }
    CheckArt(uploadMatches && alphaOnePixels > 0,
        "actual production upload copy zeros alpha 0/1 only and preserves other RGBA bytes");

    const AlphaBounds uncut = FindUploadedAlphaBounds(uploadedBytes, false);
    const AlphaBounds clipped = FindUploadedAlphaBounds(uploadedBytes, true);
    CheckArt(uncut.count == 250647 && uncut.minX == 71 && uncut.minY == 63 &&
        uncut.maxX == 1182 && uncut.maxY == 1171,
        "effective uploaded alpha support reproduces the independent all-four-edge finding");
    CheckArt(clipped.minX == 77 && clipped.minY == 66 && clipped.maxX == 1177 && clipped.maxY == 1163,
        "square UV fragment retains effective alpha support only within the calibrated solid rectangle");

    RecordTextureUpload(g_renderer, g_renderer.squareFrameResources, reinterpret_cast<void*>(0x100));
    CheckArt(barrierCount == 2 && copyCount == 1 && copyWidth == 1254 && copyHeight == 1254 &&
        copyLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        !g_renderer.squareFrameResources.frameTextureUploadPending,
        "actual square upload records both layout barriers and a full-size texture copy");

    const float scales[] = { 1.0f, 0.5f, 2.0f };
    const int expectedWidths[] = { 259, 130, 518 };
    const int centers[4][2] = { {700,600}, {701,601}, {700,601}, {701,600} };
    const char* centerNames[] = { "even-even", "odd-odd", "even-odd", "odd-even" };
    for (int scaleIndex = 0; scaleIndex < 3; ++scaleIndex)
    {
        for (int centerIndex = 0; centerIndex < 4; ++centerIndex)
        {
            const float scale = scales[scaleIndex];
            const int cx = centers[centerIndex][0], cy = centers[centerIndex][1];
            const MinimapPixelBounds outer = SquareFrameBoundsAtScale(cx, cy, scale);
            g_renderer.widgetScale = scale;
            g_renderer.widgetCenterX = cx;
            g_renderer.widgetCenterY = cy;
            pushCount = drawCount = 0;
            CheckArt(TryDrawSquareTexturedFrame(g_renderer, reinterpret_cast<void*>(0x100), cx, cy),
                "actual production helper draws the cached square frame");
            CheckArt(pushCount == 1 && pushSize == 32 && drawCount == 1 &&
                drawVertices == 6 && drawInstances == 1,
                "actual production frame draw uses the existing eight-float quad push");

            const float quadLeft = (push[0] + 1.0f) * g_renderer.width * 0.5f;
            const float quadTop = (push[1] + 1.0f) * g_renderer.height * 0.5f;
            const float quadRight = (push[2] + 1.0f) * g_renderer.width * 0.5f;
            const float quadBottom = (push[3] + 1.0f) * g_renderer.height * 0.5f;
            const float texelW = (quadRight - quadLeft) / 1254.0f;
            const float texelH = (quadBottom - quadTop) / 1254.0f;
            const float rawLeft = quadLeft + uncut.minX * texelW;
            const float rawTop = quadTop + uncut.minY * texelH;
            const float rawRight = quadLeft + (uncut.maxX + 1) * texelW;
            const float rawBottom = quadTop + (uncut.maxY + 1) * texelH;
            const float clipLeft = quadLeft + 77.0f * texelW;
            const float clipTop = quadTop + 66.0f * texelH;
            const float clipRight = quadLeft + 1178.0f * texelW;
            const float clipBottom = quadTop + 1164.0f * texelH;
            const bool fragmentClips = selectedSquareFragment;
            const float effectiveLeft = fragmentClips ? clipLeft : rawLeft;
            const float effectiveTop = fragmentClips ? clipTop : rawTop;
            const float effectiveRight = fragmentClips ? clipRight : rawRight;
            const float effectiveBottom = fragmentClips ? clipBottom : rawBottom;
            const bool rawCrossesAllSides = rawLeft < outer.left && rawTop < outer.top &&
                rawRight > outer.right && rawBottom > outer.bottom;
            const bool clippedMatches = NearArt(clipLeft, static_cast<float>(outer.left)) &&
                NearArt(clipTop, static_cast<float>(outer.top)) &&
                NearArt(clipRight, static_cast<float>(outer.right)) &&
                NearArt(clipBottom, static_cast<float>(outer.bottom)) &&
                outer.Width() == expectedWidths[scaleIndex] && outer.Height() == expectedWidths[scaleIndex];
            const bool effectiveMatches = NearArt(effectiveLeft, static_cast<float>(outer.left)) &&
                NearArt(effectiveTop, static_cast<float>(outer.top)) &&
                NearArt(effectiveRight, static_cast<float>(outer.right)) &&
                NearArt(effectiveBottom, static_cast<float>(outer.bottom));
            std::cout << "MEASURE scale=" << scale << " center=" << centerNames[centerIndex] <<
                " selected=" << (fragmentClips ? "square-clip" : "round-direct") <<
                " uncut=[" << rawLeft << "," << rawTop << ".." << rawRight << "," << rawBottom <<
                "] effective=[" << effectiveLeft << "," << effectiveTop << ".." << effectiveRight << "," << effectiveBottom <<
                "] logical=[" << outer.left << "," << outer.top << ".." << outer.right << "," << outer.bottom << "]" << std::endl;
            CheckArt(rawCrossesAllSides,
                "uncut uploaded support still demonstrates the overhang at each scale and center parity");
            CheckArt(clippedMatches && effectiveMatches,
                "actual selected square frame fragment constrains support to logical bounds at each scale and center parity");
        }
    }

    CheckArt(selectedSquareFragment && !selectedRoundFragment,
        "actual square frame preparation selects its dedicated square-clipping fragment");

    DestroyVulkanMinimapRendererLocked();
    CheckArt(live.empty() && bindings.empty(), "square art resources teardown cleanly after the captured draw");

    {
        std::lock_guard<std::mutex> lock(g_squareMinimapFrameMutex);
        g_squareMinimapFrame = {};
    }
    PublishMinimapShape(MinimapShape::Round);
    SetupArtRenderer(argv[1]);
    PublishMinimapShape(MinimapShape::Square);
    CaptureMinimapShapeSnapshot(g_renderer);
    const bool missingPrepared = PrepareSquareFrameResourcesLocked(g_renderer);
    CheckArt(!missingPrepared && g_renderer.squareFrameUnavailable &&
        !g_renderer.squareFrameResources.frameTextureReady && live.empty() && bindings.empty(),
        "missing square fragment fails actual preparation and retains vector fallback");
    const int callsAfterMissing = calls;
    CheckArt(!PrepareSquareFrameResourcesLocked(g_renderer) && calls == callsAfterMissing,
        "missing square fragment stays latched without per-frame retries");
    const std::uint32_t clearCallsBeforeFallback = clearAttachmentCalls;
    if (!TryDrawSquareTexturedFrame(g_renderer, reinterpret_cast<void*>(0x100), 700, 600))
        CmdClearSquareFrameOverlay(g_renderer, reinterpret_cast<void*>(0x100), 700, 600);
    CheckArt(clearAttachmentCalls > clearCallsBeforeFallback,
        "missing square fragment still draws the production vector frame fallback");

    PublishMinimapShape(MinimapShape::Round);
    CaptureMinimapShapeSnapshot(g_renderer);
    artContext.shroudtopia.mod_folder = argv[2];
    PublishMinimapShape(MinimapShape::Square);
    CaptureMinimapShapeSnapshot(g_renderer);
    CheckArt(PrepareSquareFrameResourcesLocked(g_renderer) &&
        capturedFragmentShader == expectedSquareFragment,
        "explicit shape epoch recovers through actual square fragment preparation");
    DestroyVulkanMinimapRendererLocked();
    CheckArt(live.empty() && bindings.empty(), "recovered square resources teardown cleanly");

    SetupArtRenderer(argv[2]);
    PublishMinimapShape(MinimapShape::Round);
    CaptureMinimapShapeSnapshot(g_renderer);
    CheckArt(TryCreateTexturedFrameResourcesLocked(g_renderer) &&
        capturedFragmentShader == expectedRoundFragment &&
        capturedFragmentShader != expectedSquareFragment,
        "round frame resources continue selecting the unchanged round fragment");
    DestroyVulkanMinimapRendererLocked();
    CheckArt(live.empty() && bindings.empty(), "round frame resource control tears down cleanly");
    return 0;
}
