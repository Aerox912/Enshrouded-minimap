// Focused behavior checks for the independent procedural marker-glow pipeline.
#pragma warning(push)
#pragma warning(disable: 4715)
#define main MarkerGlowFrameResourceChecks
#include "frame_failure_test.cpp"
#undef main
#pragma warning(pop)
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    std::set<std::string> requestedKeys;
    std::unordered_map<std::string, std::string> configuredValues;
    std::vector<std::string> configLogs;
    std::vector<std::string> markerCommands;
    std::vector<std::vector<std::uint8_t>> markerPushBytes;
    std::vector<std::uint32_t> markerPushSizes;
    int markerFailures = 0;
    int markerGpuDraws = 0;
    int markerBodyClears = 0;
    int markerDescriptorBinds = 0;
    int markerImageAllocations = 0;
    int markerPipelineCreates = 0;
    static ModContext markerContext;

    void CheckMarker(bool ok, const char* message)
    {
        if (!ok)
        {
            ++markerFailures;
            std::cerr << "FAIL: " << message << '\n';
            return;
        }
        std::cout << "PASS: " << message << '\n';
    }

    std::string GetMarkerConfig(const char* modKey, const char* key, std::string fallback)
    {
        if (std::string(modKey) == "minimap_mod")
            requestedKeys.insert(key);
        const auto found = configuredValues.find(key);
        return found == configuredValues.end() ? fallback : found->second;
    }

    bool MarkerLogContains(const std::string& text)
    {
        for (const auto& line : configLogs)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    }

    void RefreshMarkerConfig(const std::string& path, bool forceLog = true)
    {
        markerContext.shroudtopia.config_file = path;
        g_modContext = &markerContext;
        configLogs.clear();
        RefreshMinimapConfig(&markerContext, forceLog);
    }

    void WriteMarkerConfig(const std::string& path, const char* json)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << json;
        file.close();
        CheckMarker(file.good(), "marker configuration fixture is writable");
    }

    std::int32_t CreateMarkerPipelineLayout(void* device,
        const VkPipelineLayoutCreateInfo* info, const void* allocator, void** output)
    {
#if __has_include("../src/marker_glow_policy.h")
        CheckMarker(info != nullptr && info->setLayoutCount == 0 && info->pushConstantRangeCount == 1 &&
            info->pPushConstantRanges != nullptr &&
            info->pPushConstantRanges[0].size == sizeof(MinimapMarkerGlow::PushConstants) &&
            info->pPushConstantRanges[0].size <= 128,
            "marker pipeline uses one descriptor-free push range within Vulkan's 128-byte guarantee");
#else
        (void)info;
#endif
        return Create("pipeline-layout", output);
    }

    std::int32_t CreateMarkerPipeline(void* device, void* cache, std::uint32_t count,
        const VkGraphicsPipelineCreateInfo* infos, const void* allocator, void** output)
    {
        ++markerPipelineCreates;
        CheckMarker(count == 1 && infos != nullptr && infos->pColorBlendState != nullptr &&
            infos->pColorBlendState->attachmentCount == 1 &&
            infos->pColorBlendState->pAttachments != nullptr &&
            infos->pColorBlendState->pAttachments[0].blendEnable == 1,
            "marker pipeline creates one alpha-blended procedural draw");
        return Create("pipeline", output);
    }

    void ConfigureMarkerCommands()
    {
        auto& f = g_renderer.fns;
        f.createPipelineLayout = CreateMarkerPipelineLayout;
        f.createGraphicsPipelines = CreateMarkerPipeline;
        f.cmdBindPipeline = [](void*, std::uint32_t, void*) {
            markerCommands.emplace_back("bind-pipeline");
        };
        f.cmdBindDescriptorSets = [](void*, std::uint32_t, void*, std::uint32_t, std::uint32_t,
            void* const*, std::uint32_t, const std::uint32_t*) {
            ++markerDescriptorBinds;
        };
        f.cmdPushConstants = [](void*, void*, std::uint32_t stages, std::uint32_t offset,
            std::uint32_t size, const void* data) {
            CheckMarker(stages == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) &&
                offset == 0 && data != nullptr && size <= 128,
                "marker draw sends matching vertex and fragment push constants");
            if (data != nullptr && size <= 128)
            {
                const auto* first = static_cast<const std::uint8_t*>(data);
                markerPushBytes.emplace_back(first, first + size);
                markerPushSizes.push_back(size);
            }
            markerCommands.emplace_back("push");
        };
        f.cmdDraw = [](void*, std::uint32_t vertices, std::uint32_t instances,
            std::uint32_t, std::uint32_t) {
            CheckMarker(vertices == 6 && instances == 1,
                "marker glow uses a six-vertex procedural quad");
            ++markerGpuDraws;
            markerCommands.emplace_back("draw");
        };
        f.cmdClearAttachments = [](void*, std::uint32_t, const VkClearAttachment*,
            std::uint32_t rectCount, const VkClearRect*) {
            if (rectCount != 0)
            {
                ++markerBodyClears;
                markerCommands.emplace_back("body");
            }
        };
        f.createImage = [](void*, const VkImageCreateInfo*, const void*, void** output) -> std::int32_t {
            ++markerImageAllocations;
            *output = nullptr;
            return -11;
        };
    }

    void ResetMarkerObservation()
    {
        markerCommands.clear();
        markerPushBytes.clear();
        markerPushSizes.clear();
        markerGpuDraws = markerBodyClears = markerDescriptorBinds = markerImageAllocations = markerPipelineCreates = 0;
    }

    std::size_t FirstCommand(const char* name)
    {
        for (std::size_t index = 0; index < markerCommands.size(); ++index)
            if (markerCommands[index] == name)
                return index;
        return markerCommands.size();
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Expected a mods fixture directory and task support directory.\n";
        return 2;
    }

    markerContext.shroudtopia.mod_folder = argv[1];
    markerContext.config.GetString = GetMarkerConfig;
    markerContext.Log = [](const char* message) { configLogs.emplace_back(message); };
    g_modContext = &markerContext;

    const std::string missingConfig = std::string(argv[2]) + "\\marker-glow-missing-config.json";
    configuredValues.clear();
    requestedKeys.clear();
    RefreshMarkerConfig(missingConfig);
    CheckMarker(requestedKeys.count("player_arrow_glow") == 1 &&
        MarkerLogContains("player_arrow_glow=on"),
        "missing configuration enables player-arrow glow by default");
    CheckMarker(requestedKeys.count("ping_glow") == 1 &&
        requestedKeys.count("ping_glow_style") == 1 &&
        MarkerLogContains("ping_glow=on") && MarkerLogContains("ping_glow_style=same-colour"),
        "missing configuration enables same-colour ping glow by default");
    CheckMarker(requestedKeys.count("waypoint_glow") == 1 &&
        requestedKeys.count("waypoint_glow_style") == 1 &&
        MarkerLogContains("waypoint_glow=on") && MarkerLogContains("waypoint_glow_style=same-colour"),
        "missing configuration enables same-colour waypoint glow by default");
    CheckMarker(requestedKeys.count("death_marker_glow") == 1 &&
        MarkerLogContains("death_marker_glow=on"),
        "missing configuration enables fixed-style death glow by default");
    CheckMarker(MarkerLogContains("offset_raw=0") &&
        MarkerLogContains("offset_source=shroudtopia_config_api"),
        "offset diagnostic identifies the API fallback and reports the normal raw value");

    const auto configApi = markerContext.config.GetString;
    markerContext.config.GetString = nullptr;
    RefreshMarkerConfig(missingConfig);
    CheckMarker(MarkerLogContains("offset_raw=0") && MarkerLogContains("offset_source=default"),
        "offset diagnostic identifies the compiled default when no config API source exists");
    markerContext.config.GetString = configApi;
    RefreshMarkerConfig(missingConfig);

    const std::string disabledConfig = std::string(argv[2]) + "\\marker-glow-disabled.json";
    WriteMarkerConfig(disabledConfig,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":false,"
        "\"waypoint_glow\":false,\"death_marker_glow\":false,\"offset\":42}}}");
    configuredValues["player_arrow_glow"] = "true";
    configuredValues["ping_glow"] = "true";
    configuredValues["waypoint_glow"] = "true";
    configuredValues["death_marker_glow"] = "true";
    RefreshMarkerConfig(disabledConfig);
    CheckMarker(MarkerLogContains("player_arrow_glow=off") && MarkerLogContains("ping_glow=off") &&
        MarkerLogContains("waypoint_glow=off") && MarkerLogContains("death_marker_glow=off"),
        "explicit file false overrides config API true for every independent glow switch");
    CheckMarker(MarkerLogContains("offset_raw=42") &&
        MarkerLogContains("offset_source=" + disabledConfig),
        "offset diagnostic identifies the winning config file value");

    configuredValues["offset"] = "42\n[ignored-extra-text]";
    RefreshMarkerConfig(missingConfig);
    CheckMarker(MarkerLogContains("offset_raw=<invalid>") &&
        !MarkerLogContains("[ignored-extra-text]"),
        "offset diagnostic keeps unsafe raw values bounded and control-free");

    configuredValues["ping_glow_style"] = "invalid-style";
    configuredValues["waypoint_glow_style"] = "CONTRASTING";
    RefreshMarkerConfig(missingConfig);
    CheckMarker(MarkerLogContains("ping_glow_style=same-colour") &&
        MarkerLogContains("waypoint_glow_style=contrasting"),
        "marker style parsing defaults invalid values and accepts contrasting case-insensitively");

    const std::string drawConfig = std::string(argv[2]) + "\\marker-glow-draw.json";
    WriteMarkerConfig(drawConfig,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":true,"
        "\"ping_glow_style\":\"contrasting\",\"waypoint_glow\":true,"
        "\"waypoint_glow_style\":\"contrasting\",\"death_marker_glow\":true}}}");
    RefreshMarkerConfig(drawConfig);

    ResetMarkerObservation();
    Setup(0);
    ConfigureMarkerCommands();
    g_renderer.arrowGlowFrameEligible = true;
    g_playerArrow.attempted = false;
    g_playerArrow.loaded = false;
    void* commandBuffer = reinterpret_cast<void*>(0x100);
#if __has_include("../src/marker_glow_policy.h")
    CheckMarker(!g_playerArrowGlowEnabled.load() && PrepareMarkerGlowFrame(g_renderer, commandBuffer),
        "marker pipeline prepares with player-arrow glow explicitly off");
    g_renderer.widgetScale = 1.5f;
    g_renderer.widgetCenterX = 100;
    g_renderer.widgetCenterY = 100;
    DrawPingDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    CheckMarker(markerGpuDraws == 1 && FirstCommand("draw") < FirstCommand("body"),
        "ping halo draws before the original filled green diamond body");
    CheckMarker(markerImageAllocations == 0 && allocations == 0 && !g_playerArrow.attempted &&
        markerDescriptorBinds == 0,
        "procedural marker preparation needs no arrow load, descriptor, image, or atlas upload");
    if (!markerPushBytes.empty())
    {
        MinimapMarkerGlow::PushConstants push{};
        std::memcpy(&push, markerPushBytes[0].data(), sizeof(push));
        CheckMarker(markerPushSizes[0] == 80 && std::fabs(push.viewportCenter.x - 115.0f) < 0.001f &&
            std::fabs(push.viewportCenter.y - 118.0f) < 0.001f &&
            std::fabs(push.shape.x - 19.5f) < 0.001f &&
            std::fabs(push.clip.z - 120.0f) < 0.001f && push.shape.w == 0.0f &&
            std::fabs(push.haloColor.x - 1.0f) < 0.001f &&
            std::fabs(push.haloColor.y - (153.0f / 255.0f)) < 0.001f,
            "contrasting ping push applies HUD scale, circular clip, and amber geometry");
    }
    else
        CheckMarker(false, "ping push constants are captured");

    DrawWaypointDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    CheckMarker(markerGpuDraws == 2 && FirstCommand("draw") < FirstCommand("body"),
        "waypoint halo draws before the unchanged hollow diamond outline");
    if (markerPushBytes.size() >= 2)
    {
        MinimapMarkerGlow::PushConstants push{};
        std::memcpy(&push, markerPushBytes[1].data(), sizeof(push));
        CheckMarker(push.shape.w == 1.0f && std::fabs(push.shape.x - 22.5f) < 0.001f &&
            std::fabs(push.haloColor.x - 0.25f) < 0.001f &&
            std::fabs(push.haloColor.y - 0.85f) < 0.001f &&
            std::fabs(push.haloColor.z - 1.0f) < 0.001f,
            "contrasting waypoint push keeps hollow geometry and cyan color");
    }
    else
        CheckMarker(false, "waypoint push constants are captured");

    const std::string sameColourConfig = std::string(argv[2]) + "\\marker-glow-same-colour.json";
    WriteMarkerConfig(sameColourConfig,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":true,"
        "\"ping_glow_style\":\"same-colour\",\"waypoint_glow\":true,"
        "\"waypoint_glow_style\":\"same-colour\",\"death_marker_glow\":true}}}");
    RefreshMarkerConfig(sameColourConfig);
    markerPushBytes.clear();
    markerPushSizes.clear();
    markerCommands.clear();
    DrawPingDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    DrawWaypointDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    if (markerPushBytes.size() >= 2)
    {
        MinimapMarkerGlow::PushConstants pingPush{}, waypointPush{};
        std::memcpy(&pingPush, markerPushBytes[0].data(), sizeof(pingPush));
        std::memcpy(&waypointPush, markerPushBytes[1].data(), sizeof(waypointPush));
        CheckMarker(pingPush.haloColor.y > pingPush.haloColor.x &&
            pingPush.haloColor.y > pingPush.haloColor.z &&
            waypointPush.haloColor.x > waypointPush.haloColor.y &&
            waypointPush.haloColor.y > waypointPush.haloColor.z,
            "same-colour defaults use green ping and gold waypoint halos");
    }
    else
        CheckMarker(false, "same-colour ping and waypoint push constants are captured");

    const std::string pingOffConfig = std::string(argv[2]) + "\\marker-glow-ping-off.json";
    WriteMarkerConfig(pingOffConfig,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":false,"
        "\"waypoint_glow\":true,\"waypoint_glow_style\":\"same-colour\","
        "\"death_marker_glow\":true}}}");
    RefreshMarkerConfig(pingOffConfig);
    markerCommands.clear();
    const int drawsBeforePingOff = markerGpuDraws;
    const int bodiesBeforePingOff = markerBodyClears;
    DrawPingDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    CheckMarker(markerGpuDraws == drawsBeforePingOff && markerBodyClears > bodiesBeforePingOff,
        "turning ping glow off preserves its original body while waypoint glow stays enabled");
    const int drawsBeforeWaypointAfterPingOff = markerGpuDraws;
    DrawWaypointDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    CheckMarker(markerGpuDraws == drawsBeforeWaypointAfterPingOff + 1,
        "waypoint glow remains independently active while ping glow is off");

    g_minimapIcons = {};
    g_minimapIcons.attempted = true;
    g_minimapIcons.loaded = true;
    MinimapIcon rasterDeath{};
    rasterDeath.key = 77;
    rasterDeath.width = rasterDeath.height = 1;
    rasterDeath.rgba = { 255, 32, 32, 255 };
    g_minimapIcons.icons.push_back(rasterDeath);
    markerPushBytes.clear();
    markerPushSizes.clear();
    markerCommands.clear();
    const std::size_t drawCountBeforeDeath = markerGpuDraws;
    const std::size_t bodyCountBeforeDeath = markerBodyClears;
    DrawDeathMarker(g_renderer, commandBuffer, 100, 100, 80, 110, 112, 77);
    CheckMarker(markerGpuDraws == drawCountBeforeDeath + 1 && markerBodyClears > bodyCountBeforeDeath &&
        FirstCommand("draw") < FirstCommand("body"),
        "D2 halo is recorded before and preserves an available raster death icon");
    if (!markerPushBytes.empty())
    {
        MinimapMarkerGlow::PushConstants push{};
        std::memcpy(&push, markerPushBytes[0].data(), sizeof(push));
        CheckMarker(push.shape.w == 2.0f && std::fabs(push.clip.w - 24.75f) < 0.001f &&
            push.plateColor.x < 0.05f && push.plateColor.y < 0.05f &&
            push.haloColor.x > 0.95f && push.haloColor.y < 0.25f,
            "D2 push supplies a dark backplate, red rim, and scaled circular clip");
    }
    else
        CheckMarker(false, "death push constants are captured");

    g_minimapIcons.icons.clear();
    markerCommands.clear();
    markerPushBytes.clear();
    markerPushSizes.clear();
    const std::size_t drawCountBeforeFallback = markerGpuDraws;
    const std::size_t bodyCountBeforeFallback = markerBodyClears;
    DrawDeathMarker(g_renderer, commandBuffer, 100, 100, 80, 110, 112, 77);
    CheckMarker(markerGpuDraws == drawCountBeforeFallback + 1 && markerBodyClears > bodyCountBeforeFallback &&
        FirstCommand("draw") < FirstCommand("body"),
        "D2 halo is recorded before and preserves the procedural skull fallback");

    g_renderer.arrowGlowFrameEligible = false;
    markerCommands.clear();
    const std::size_t drawCountBeforeIneligible = markerGpuDraws;
    DrawPingDiamond(g_renderer, commandBuffer, 110, 112, 100, 100, 80);
    CheckMarker(markerGpuDraws == drawCountBeforeIneligible,
        "world-ineligible or hidden-map frame gate suppresses marker effects");
    CheckMarker(markerDescriptorBinds == 0 && markerImageAllocations == 0,
        "marker draw remains descriptor-free and image-free");
    DestroyVulkanMinimapRendererLocked();
    CheckMarker(live.empty() && bindings.empty(), "marker pipeline teardown releases its renderer-owned resources");

    ResetMarkerObservation();
    Setup(0);
    ConfigureMarkerCommands();
    g_renderer.arrowGlowFrameEligible = true;
    failure = "pipeline";
    CheckMarker(!PrepareMarkerGlowFrame(g_renderer, commandBuffer) && live.empty(),
        "partial procedural-pipeline failure releases temporary shader and layout resources");
    const int failedPipelineCreateCount = markerPipelineCreates;
    CheckMarker(!PrepareMarkerGlowFrame(g_renderer, commandBuffer) &&
        markerPipelineCreates == failedPipelineCreateCount,
        "latched marker-pipeline failure does not repeat creation on the next eligible call");
    DrawPingDiamond(g_renderer, commandBuffer, 64, 64, 64, 64, 80);
    CheckMarker(markerGpuDraws == 0 && markerBodyClears != 0,
        "failed marker glow keeps the original ping body visible");
    failure.clear();
    const std::string retryOff = std::string(argv[2]) + "\\marker-glow-retry-off.json";
    const std::string retryOn = std::string(argv[2]) + "\\marker-glow-retry-on.json";
    WriteMarkerConfig(retryOff,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":false,"
        "\"waypoint_glow\":true,\"death_marker_glow\":true}}}");
    WriteMarkerConfig(retryOn,
        "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"ping_glow\":true,"
        "\"waypoint_glow\":true,\"death_marker_glow\":true}}}");
    RefreshMarkerConfig(retryOff);
    RefreshMarkerConfig(retryOn);
    CheckMarker(PrepareMarkerGlowFrame(g_renderer, commandBuffer),
        "explicit off/on re-enable retries a previously failed marker pipeline once");
    CheckMarker(markerPipelineCreates == failedPipelineCreateCount + 1,
        "off/on transition makes exactly one marker-pipeline retry");
    DestroyVulkanMinimapRendererLocked();
    CheckMarker(live.empty() && bindings.empty(), "retried marker pipeline tears down without retained resources");
#else
    DrawPingDiamond(g_renderer, commandBuffer, 64, 64);
    CheckMarker(markerGpuDraws != 0,
        "missing procedural ping-glow draw is detected while preserving its existing body");
#endif

    return markerFailures == 0 ? 0 : 1;
}
