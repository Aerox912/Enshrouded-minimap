#define main FrameResourceChecks
#include "frame_failure_test.cpp"
#undef main

int terrainGpuDraws = 0;
int terrainUploadCopies = 0;
int terrainCpuClears = 0;
int terrainVisibleMapIconClears = 0;
std::shared_ptr<const LiveFogSnapshot> terrainFogOnRenderPass;
int terrainFlushCalls = 0;
int terrainUnmapCalls = 0;
int terrainUnmapAfterFlush = 0;
bool terrainFlushSeenForMap = false;
bool terrainFlushShouldFail = false;
std::vector<unsigned char> terrainMapped;

#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
static void ConfigureMappedMemoryCallbacks()
{
    terrainFlushCalls = terrainUnmapCalls = terrainUnmapAfterFlush = 0;
    terrainFlushSeenForMap = false;
    g_renderer.fns.mapMemory = [](void*, void*, std::uint64_t, std::uint64_t, std::uint32_t, void** p) -> std::int32_t {
        terrainFlushSeenForMap = false;
        *p = terrainMapped.data();
        return VK_SUCCESS;
    };
    g_renderer.fns.flushMappedMemoryRanges = [](void*, std::uint32_t count, const VkMappedMemoryRange* ranges) -> std::int32_t {
        ++terrainFlushCalls;
        Check(count == 1 && ranges != nullptr && ranges[0].offset == 0 && ranges[0].size == ~std::uint64_t(0),
            "shared staging upload flushes the full mapped allocation");
        terrainFlushSeenForMap = true;
        return terrainFlushShouldFail ? -11 : VK_SUCCESS;
    };
    g_renderer.fns.unmapMemory = [](void*, void*) {
        ++terrainUnmapCalls;
        if (terrainFlushSeenForMap)
            ++terrainUnmapAfterFlush;
    };
}

static void RunSharedUploadFlushChecks()
{
    failure = "";
    failureMode = 0;
    terrainFlushShouldFail = false;
    Setup(0);
    g_renderer.fns.flushMappedMemoryRanges = nullptr;
    TexturedResources withoutFlush;
    const std::vector<std::uint8_t> pixels(2 * 2 * 4, 128);
    Check(!TryCreateTexturedResourcesLocked(g_renderer, withoutFlush, pixels, 2, 2, true) &&
        calls == 0 && live.empty() && bindings.empty(),
        "missing flush function rejects optional texture creation before allocation");
    Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && calls == 0 && !g_renderer.frameTextureReady &&
        live.empty() && bindings.empty(),
        "missing flush function leaves the procedural frame fallback available");

    failure = "";
    Setup(0);
    ConfigureMappedMemoryCallbacks();
    Check(TryCreateTexturedFrameResourcesLocked(g_renderer),
        "shared texture resource helper creates the textured minimap frame");
    Check(terrainFlushCalls == 1 && terrainUnmapCalls == 1 && terrainUnmapAfterFlush == 1,
        "shared frame upload flushes the full staging allocation before unmapping");
    DestroyTexturedResources(g_renderer, g_renderer);
    Check(live.empty() && bindings.empty(), "shared frame resource teardown releases successful resources");

    failure = "";
    Setup(0);
    ConfigureMappedMemoryCallbacks();
    terrainFlushShouldFail = true;
    Check(!TryCreateTexturedFrameResourcesLocked(g_renderer) && terrainFlushCalls == 1 &&
        !g_renderer.frameTextureReady && live.empty() && bindings.empty(),
        "flush failure rejects the textured frame and releases partial ownership");
    terrainFlushShouldFail = false;
}

static std::shared_ptr<LiveFogSnapshot> MakeFogSnapshot(std::uint8_t value)
{
    auto snapshot = std::make_shared<LiveFogSnapshot>();
    snapshot->worldWidth = snapshot->worldHeight = 256.0f;
    snapshot->columns = snapshot->rows = 1;
    snapshot->tiles.assign(1024, value);
    snapshot->tick = GetTickCount();
    snapshot->session = g_worldSessionGeneration.load();
    return snapshot;
}

static void RunFogFrameSnapshotChecks()
{
    const DWORD now = GetTickCount();
    auto explored = MakeFogSnapshot(255);
    auto hidden = MakeFogSnapshot(0);
    MinimapFogFrame fresh{ true, explored };
    Check(CurrentMinimapFogSnapshot(fresh) == explored.get(),
        "current session fog snapshot remains usable");
    auto staleSnapshot = std::make_shared<LiveFogSnapshot>(*explored);
    staleSnapshot->tick = now - 2001;
    Check(CurrentMinimapFogSnapshot({ true, staleSnapshot }) == nullptr,
        "stale fog snapshot remains rejected");
    auto otherSession = std::make_shared<LiveFogSnapshot>(*explored);
    otherSession->session = explored->session + 1;
    Check(CurrentMinimapFogSnapshot({ true, otherSession }) == nullptr,
        "new-session fog snapshot remains rejected");
    Check(CurrentMinimapFogSnapshot({ true, nullptr }) == nullptr,
        "missing fog snapshot remains rejected");

    g_liveFogLayoutSupported.store(true);
    g_showFogOfWar.store(false);
    const MinimapFogFrame disabled = CaptureMinimapFogFrame();
    Check(!disabled.enabled && !disabled.snapshot &&
        CurrentMinimapFogSnapshot(disabled) == nullptr,
        "hidden fog capture remains disabled");
    g_showFogOfWar.store(true);
    {
        std::lock_guard<std::mutex> lock(g_liveFogMutex);
        g_liveFog = explored;
    }

    g_worldMapLayoutSupported.store(false);
    g_showWorldMarkers.store(true);
    {
        std::lock_guard<std::mutex> lock(g_playerPositionMutex);
        g_playerPosition = {};
        g_playerPosition.x = WorldToFixed(16.0f);
        g_playerPosition.z = WorldToFixed(16.0f);
        g_playerPosition.valid = true;
        g_playerPosition.lastUpdateTick = now;
    }
    {
        std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
        g_visibleMapMarkers.clear();
        CapturedMapMarkerVisibility marker{};
        marker.kind = 55;
        marker.visibility = 2;
        marker.hasWorldPosition = true;
        marker.x = WorldToFixed(16.0f);
        marker.z = WorldToFixed(16.0f);
        marker.lastUpdateTick = now;
        g_visibleMapMarkers.push_back(marker);
    }
    {
        std::lock_guard<std::mutex> lock(g_minimapIconMutex);
        g_minimapIcons.attempted = true;
        g_minimapIcons.loaded = false;
    }

    terrainVisibleMapIconClears = 0;
    const auto fixtureMarkers = CopyVisibleMapMarkers();
    std::vector<MinimapWorldPoint> fixturePoints;
    BuildWorldPoints(fixturePoints, {}, {}, fixtureMarkers);
    FilterFoggedWorldPoints(fixturePoints, fresh);
    Check(fixtureMarkers.size() == 1 && fixturePoints.size() == 1,
        "explored marker fixture survives filtering with the prepared frame");
    const auto originalWidth = g_renderer.width;
    const auto originalHeight = g_renderer.height;
    g_renderer.width = 1024;
    g_renderer.height = 768;
    terrainFogOnRenderPass = hidden;
    g_renderer.fns.cmdBeginRenderPass = [](void*, const VkRenderPassBeginInfo*, std::uint32_t) {
        std::lock_guard<std::mutex> lock(g_liveFogMutex);
        g_liveFog = terrainFogOnRenderPass;
    };
    const int drawsBefore = terrainGpuDraws;
    const int iconClearsBefore = terrainVisibleMapIconClears;
    const bool recorded = RecordVulkanMinimapCommandLocked(0);
    g_renderer.fns.cmdBeginRenderPass = [](void*, const VkRenderPassBeginInfo*, std::uint32_t) {};
    Check(recorded && terrainGpuDraws == drawsBefore + 1 &&
        g_renderer.terrainFallbackReason == nullptr,
        "frame fog update after terrain preparation does not cause CPU fallback");
    Check(terrainVisibleMapIconClears > iconClearsBefore,
        "terrain and icons keep the explored marker from the same prepared fog snapshot");
    g_renderer.width = originalWidth;
    g_renderer.height = originalHeight;

    {
        std::lock_guard<std::mutex> lock(g_liveFogMutex);
        g_liveFog = explored;
    }
    {
        std::lock_guard<std::mutex> lock(g_playerPositionMutex);
        g_playerPosition = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
        g_visibleMapMarkers.clear();
    }
    g_liveFogLayoutSupported.store(false);
}

static bool PrepareTerrainForTest(std::uint32_t imageIndex = 0)
{
    g_renderer.terrainPrepared = PrepareTerrainTexture(g_renderer, imageIndex, reinterpret_cast<void*>(1), { false, nullptr });
    return g_renderer.terrainPrepared;
}
#endif

static std::array<float, 32> shapePush{};
static int shapePushCalls = 0;
static int shapeDrawCalls = 0;

static int RunTerrainShapeContractChecks()
{
    VulkanMinimapRenderer renderer{};
    renderer.width = 1920;
    renderer.height = 1080;
    renderer.terrainSlots.resize(1);
    auto& slot = renderer.terrainSlots[0];
    slot.frameTextureReady = true;
    slot.mapWidth = slot.mapHeight = 1280;
    slot.frameTextureWidth = 1280;
    slot.frameTextureHeight = 2560;
    renderer.fns.cmdBindPipeline = [](void*, std::uint32_t, void*) {};
    renderer.fns.cmdBindDescriptorSets = [](void*, std::uint32_t, void*, std::uint32_t, std::uint32_t, void* const*, std::uint32_t, const std::uint32_t*) {};
    renderer.fns.cmdPushConstants = [](void*, void*, std::uint32_t stages, std::uint32_t offset, std::uint32_t size, const void* bytes) {
        Check(offset == 0 && size == sizeof(shapePush) &&
            stages == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            "terrain push ABI is 128 bytes at offset zero for both shader stages");
        std::memcpy(shapePush.data(), bytes, sizeof(shapePush));
        ++shapePushCalls;
    };
    renderer.fns.cmdDraw = [](void*, std::uint32_t count, std::uint32_t instances, std::uint32_t, std::uint32_t) {
        Check(count == 6 && instances == 1, "shape uses existing six-vertex terrain draw");
        ++shapeDrawCalls;
    };
    for (float scale : { 0.5f, 1.f, 2.f })
    {
        renderer.widgetScale = scale;
        renderer.widgetCenterX = 400;
        renderer.widgetCenterY = 300;
        for (MinimapShape shape : { MinimapShape::Round, MinimapShape::Square, MinimapShape::Round })
        {
            PublishMinimapShape(shape);
            CaptureMinimapShapeSnapshot(renderer);
            // A concurrent config change must not alter this recording's snapshot.
            PublishMinimapShape(shape == MinimapShape::Square ? MinimapShape::Round : MinimapShape::Square);
            const int before = shapeDrawCalls;
            Check(TryDrawTerrainTexture(renderer, reinterpret_cast<void*>(1), 400, 300, 100,
                5120, 5120, 3, 0, { false, nullptr }), "ready terrain accepts shape draw");
            Check(shapeDrawCalls == before + 1 && shapePushCalls == shapeDrawCalls,
                "one push and one draw per shape recording");
            Check(shapePush[27] == (shape == MinimapShape::Square ? 1.f : 0.f),
                "geometry.w carries captured shape for shader clipping and vignette");
            Check(shapePush[24] == 400 && shapePush[25] == 300 && shapePush[26] == scale &&
                shapePush[28] == 300 && shapePush[29] == 200 && shapePush[30] == 0 && shapePush[31] == 0,
                "shape flag preserves geometry scale and origin coordinates without using origin.w");
        }
    }
    const int before = shapeDrawCalls;
    slot.frameTextureUploadPending = true;
    Check(!TryDrawTerrainTexture(renderer, reinterpret_cast<void*>(1), 400, 300, 100,
        5120, 5120, 3, 0, { false, nullptr }) && shapeDrawCalls == before,
        "pending upload refuses shape draw without weakening readiness");
    slot.frameTextureUploadPending = false;
    auto fog = MakeFogSnapshot(255);
    Check(!TryDrawTerrainTexture(renderer, reinterpret_cast<void*>(1), 400, 300, 100,
        5120, 5120, 3, 0, { true, fog }) && shapeDrawCalls == before,
        "unsynchronized fog refuses shape draw");
    PublishMinimapShape(MinimapShape::Round);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--terrain-shape-contract")
        return RunTerrainShapeContractChecks();
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    const bool fogSnapshotOnly = argc == 3 && std::string(argv[2]) == "--fog-snapshot";
    if (argc != 2 && !fogSnapshotOnly)
    {
        std::cerr << "Expected the mod loader root and optional --fog-snapshot.\n";
        return 2;
    }

    failure = "";
    failureMode = 0;
    Setup(0);
    terrainMapped.resize(64 * 1024 * 1024);
    static ModContext testContext;
    testContext.shroudtopia.mod_folder = argv[1];
    testContext.Log = [](const char*) {};
    g_modContext = &testContext;

#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
    ConfigureMappedMemoryCallbacks();
    if (!fogSnapshotOnly)
    {
        RunSharedUploadFlushChecks();
        failure = "";
        Setup(0);
        ConfigureMappedMemoryCallbacks();
    }
#else
    g_renderer.fns.mapMemory = [](void*, void*, std::uint64_t, std::uint64_t, std::uint32_t, void** p) -> std::int32_t {
        *p = terrainMapped.data();
        return VK_SUCCESS;
    };
#endif

    g_renderer.width = 100;
    g_renderer.height = 100;
    g_renderer.commandBuffers = { 1, 2 };
    g_renderer.ready = true;
    g_renderer.fns.cmdClearAttachments = [](void*, std::uint32_t attachmentCount, const VkClearAttachment* attachments, std::uint32_t rectCount, const VkClearRect*) {
        ++terrainCpuClears;
        if (attachmentCount && attachments && rectCount &&
            std::fabs(attachments[0].clearValue.color.float32[0] - 0.84f) < 0.001f &&
            std::fabs(attachments[0].clearValue.color.float32[1] - 0.78f) < 0.001f &&
            std::fabs(attachments[0].clearValue.color.float32[2] - 0.58f) < 0.001f)
            ++terrainVisibleMapIconClears;
    };
    g_renderer.fns.cmdPipelineBarrier = [](void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const void*, std::uint32_t, const void*, std::uint32_t, const VkImageMemoryBarrier*) {};
    g_renderer.fns.cmdCopyBufferToImage = [](void*, void*, void*, std::uint32_t, std::uint32_t, const VkBufferImageCopy*) { ++terrainUploadCopies; };
#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
    ConfigureMappedMemoryCallbacks();
#endif
    g_renderer.fns.cmdBindPipeline = [](void*, std::uint32_t, void*) {};
    g_renderer.fns.cmdBindDescriptorSets = [](void*, std::uint32_t, void*, std::uint32_t, std::uint32_t, void* const*, std::uint32_t, const std::uint32_t*) {};
    g_renderer.fns.cmdPushConstants = [](void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, const void*) {};
    g_renderer.fns.cmdDraw = [](void*, std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) {
        Check(vertexCount == 6, "terrain uses the owned six-vertex quad");
        ++terrainGpuDraws;
    };

#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
    g_renderer.framebuffers = { 1, 2 };
    g_renderer.fns.resetCommandBuffer = [](void*, std::uint32_t) -> std::int32_t { return VK_SUCCESS; };
    g_renderer.fns.beginCommandBuffer = [](void*, const VkCommandBufferBeginInfo*) -> std::int32_t { return VK_SUCCESS; };
    g_renderer.fns.endCommandBuffer = [](void*) -> std::int32_t { return VK_SUCCESS; };
    g_renderer.fns.cmdBeginRenderPass = [](void*, const VkRenderPassBeginInfo*, std::uint32_t) {};
    g_renderer.fns.cmdEndRenderPass = [](void*) {};
    g_toggleNotice.clear();
    if (!fogSnapshotOnly)
    {
        g_minimapVisible.store(false);
        Check(RecordVulkanMinimapCommandLocked(0) && !g_renderer.terrainPrepared && g_renderer.terrainSlots.empty() &&
            terrainUploadCopies == 0 && terrainFlushCalls == 0 && terrainGpuDraws == 0,
            "hidden minimap with no notice skips terrain recording and upload");
    }
    g_minimapVisible.store(true);
#endif

    g_realMap.attempted = true;
    g_realMap.loaded = true;
    g_realMap.width = g_realMap.height = 512;
    g_realMap.rgba.assign(512 * 512 * 4, 128);
    g_defaultMap = {};
    g_staticPois.attempted = true;

#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
    RunFogFrameSnapshotChecks();
    if (fogSnapshotOnly) return 0;
    Check(PrepareTerrainForTest(0), "first swapchain image prepares terrain resources");
    const int firstSlotCalls = calls;
    const int firstSlotUploads = terrainUploadCopies;
    const int firstSlotFlushes = terrainFlushCalls;
    Check(firstSlotFlushes == 1 && terrainUnmapCalls == 1 && terrainUnmapAfterFlush == 1,
        "terrain staging upload is flushed before unmapping");
#endif

    for (int percent : { 50, 75, 100, 125, 150, 175, 200 })
    {
        const int drawsBefore = terrainGpuDraws;
        {
            MinimapScaleScope scale(g_renderer, percent / 100.0f, 50, 50);
#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
            Check(PrepareTerrainForTest(), "terrain resources prepare before terrain rendering");
#endif
            DrawRealMap(g_renderer, reinterpret_cast<void*>(1), 50, 50, 20, 5120, 5120, 3, 0, { false, nullptr });
        }
        Check(terrainGpuDraws == drawsBefore + 1,
            "map terrain dispatches through the GPU quad at every supported widget scale");
    }
#ifdef MINIMAP_GPU_TERRAIN_INTEGRATED
    Check(calls == firstSlotCalls && terrainUploadCopies == firstSlotUploads && terrainFlushCalls == firstSlotFlushes,
        "one swapchain image reuses its cached terrain resources across widget scales");

    const int beforeSecondSlotCalls = calls;
    const int beforeSecondSlotUploads = terrainUploadCopies;
    const int beforeSecondSlotFlushes = terrainFlushCalls;
    Check(PrepareTerrainForTest(1), "second swapchain image prepares its own terrain resources");
    Check(g_renderer.terrainSlots.size() == 2 &&
        g_renderer.terrainSlots[0].frameTextureImage != 0 &&
        g_renderer.terrainSlots[1].frameTextureImage != 0 &&
        g_renderer.terrainSlots[0].frameTextureImage != g_renderer.terrainSlots[1].frameTextureImage &&
        calls > beforeSecondSlotCalls && terrainUploadCopies == beforeSecondSlotUploads + 1 &&
        terrainFlushCalls == beforeSecondSlotFlushes + 1,
        "each swapchain image owns an independently uploaded terrain texture");

    const int afterSecondSlotCalls = calls;
    const int afterSecondSlotUploads = terrainUploadCopies;
    Check(PrepareTerrainForTest(0) && calls == afterSecondSlotCalls && terrainUploadCopies == afterSecondSlotUploads,
        "revisiting the first swapchain image reuses its cached terrain texture");

    for (auto& slot : g_renderer.terrainSlots)
        DestroyTexturedResources(g_renderer, slot);
    Check(live.empty() && bindings.empty(), "terrain slot teardown releases every owned GPU resource");
#endif
    return 0;
}
