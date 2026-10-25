// Focused behavior checks for the optional GPU player-arrow halo.
#pragma warning(push)
#pragma warning(disable: 4715)
#define main ArrowGlowFrameResourceChecks
#include "frame_failure_test.cpp"
#undef main
#pragma warning(pop)
#include <fstream>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>

namespace
{
    std::set<std::string> requestedKeys;
    std::unordered_map<std::string, std::string> configuredValues;
    std::vector<std::string> configLogs;
    int glowGpuDraws = 0;
    int legacyClearCalls = 0;
    int glowUploadCopies = 0;
    int glowFlushCalls = 0;
    int invalidPushCalls = 0;
    int busyFenceChecks = 0;
    int busyFenceResets = 0;
    int busySubmissions = 0;
    std::vector<std::uint8_t> mappedArrowGlow;
    std::vector<MinimapArrowGlow::PushConstants> glowPushes;
    std::vector<std::string> commandOrder;
    static ModContext context;

    std::string GetConfigString(const char* modKey, const char* key, std::string fallback)
    {
        if (std::string(modKey) == "minimap_mod")
            requestedKeys.insert(key);
        const auto found = configuredValues.find(key);
        return found == configuredValues.end() ? fallback : found->second;
    }

    bool ConfigLogContains(const std::string& text)
    {
        for (const auto& line : configLogs)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    }

    std::int32_t BusyFenceStatus(void*, void*)
    {
        ++busyFenceChecks;
        return VK_NOT_READY;
    }

    std::int32_t BusyFenceReset(void*, std::uint32_t, void* const*)
    {
        ++busyFenceResets;
        return VK_SUCCESS;
    }

    std::int32_t BusyQueueSubmit(void*, std::uint32_t, const VkSubmitInfo*, void*)
    {
        ++busySubmissions;
        return VK_SUCCESS;
    }

    void RefreshTestConfig(const std::string& path)
    {
        context.shroudtopia.config_file = path;
        g_modContext = &context;
        g_shroudtopiaConfigPath.clear();
        configLogs.clear();
        RefreshMinimapConfig(&context, true);
    }

    void RunConfigChecks(const std::string& supportRoot)
    {
        const std::string modFolder = context.shroudtopia.mod_folder;
        context = {};
        context.shroudtopia.mod_folder = modFolder;
        context.config.GetString = GetConfigString;
        context.Log = [](const char* message) { configLogs.emplace_back(message); };
        configuredValues.clear();
        requestedKeys.clear();
        const std::string missingConfig = (std::filesystem::path(supportRoot) / "arrow-glow-no-config.json").string();
        RefreshTestConfig(missingConfig);
        Check(requestedKeys.count("player_arrow_glow") == 1 &&
            requestedKeys.count("player_arrow_glow_style") == 1,
            "config API fallback reads both player-arrow halo settings");
        Check(ConfigLogContains("player_arrow_glow=on") &&
            ConfigLogContains("player_arrow_glow_style=same-colour"),
            "unset config enables the halo with same-colour as the style default");

        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "contrasting";
        RefreshTestConfig(missingConfig);
        Check(ConfigLogContains("player_arrow_glow=on") &&
            ConfigLogContains("player_arrow_glow_style=contrasting"),
            "API config enables halo and selects contrasting style");

        configuredValues["player_arrow_glow_style"] = "invalid-style";
        RefreshTestConfig(missingConfig);
        Check(ConfigLogContains("player_arrow_glow=on") &&
            ConfigLogContains("player_arrow_glow_style=same-colour"),
            "invalid style falls back to same-colour without changing the enable switch");
        configuredValues["player_arrow_glow_style"] = "   ";
        RefreshTestConfig(missingConfig);
        Check(ConfigLogContains("player_arrow_glow_style=same-colour"),
            "empty style falls back to same-colour");

        const std::string filePath = (std::filesystem::path(supportRoot) / "arrow-glow-config-precedence.json").string();
        {
            std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
            file << "{\"mods\":{\"minimap_mod\":{\"player_arrow_glow\":false,\"player_arrow_glow_style\":\"same-colour\"}}}";
        }
        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "contrasting";
        RefreshTestConfig(filePath);
        Check(ConfigLogContains("player_arrow_glow=off") &&
            ConfigLogContains("player_arrow_glow_style=same-colour"),
            "file settings retain precedence over the config API fallback");
    }

    void ConfigureOverlayCommands()
    {
        auto& f = g_renderer.fns;
        f.resetCommandBuffer = [](void*, std::uint32_t) -> std::int32_t { return VK_SUCCESS; };
        f.beginCommandBuffer = [](void*, const VkCommandBufferBeginInfo*) -> std::int32_t { return VK_SUCCESS; };
        f.endCommandBuffer = [](void*) -> std::int32_t { return VK_SUCCESS; };
        f.cmdBeginRenderPass = [](void*, const VkRenderPassBeginInfo*, std::uint32_t) {};
        f.cmdEndRenderPass = [](void*) {};
        f.cmdClearAttachments = [](void*, std::uint32_t, const VkClearAttachment*, std::uint32_t, const VkClearRect*) {
            ++legacyClearCalls;
        };
        f.cmdPipelineBarrier = [](void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
            const void*, std::uint32_t, const void*, std::uint32_t, const VkImageMemoryBarrier*) {
            commandOrder.emplace_back("barrier");
        };
        f.cmdCopyBufferToImage = [](void*, void*, void*, std::uint32_t, std::uint32_t, const VkBufferImageCopy* copy) {
            ++glowUploadCopies;
            commandOrder.emplace_back("upload");
            Check(copy != nullptr && copy->imageExtent.width == 4 && copy->imageExtent.height == 4,
                "one immutable threshold mask uploads at the source-arrow dimensions");
        };
        f.cmdBindPipeline = [](void*, std::uint32_t, void*) {};
        f.cmdBindDescriptorSets = [](void*, std::uint32_t, void*, std::uint32_t, std::uint32_t, void* const*,
            std::uint32_t, const std::uint32_t*) {};
        f.cmdPushConstants = [](void*, void*, std::uint32_t stages, std::uint32_t offset,
            std::uint32_t size, const void* data) {
            if (stages != (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) ||
                offset != 0 || size != sizeof(MinimapArrowGlow::PushConstants) || data == nullptr)
            {
                ++invalidPushCalls;
                return;
            }
            MinimapArrowGlow::PushConstants push{};
            std::memcpy(&push, data, sizeof(push));
            glowPushes.push_back(push);
            commandOrder.emplace_back("push");
        };
        f.cmdDraw = [](void*, std::uint32_t vertexCount, std::uint32_t instanceCount,
            std::uint32_t, std::uint32_t) {
            ++glowGpuDraws;
            if (vertexCount != 6 || instanceCount != 1)
                ++invalidPushCalls;
            commandOrder.emplace_back("gpu-draw");
        };
        f.flushMappedMemoryRanges = [](void*, std::uint32_t count, const VkMappedMemoryRange*) -> std::int32_t {
            ++glowFlushCalls;
            return count == 1 ? VK_SUCCESS : -11;
        };
        f.mapMemory = [](void*, void*, std::uint64_t, std::uint64_t, std::uint32_t, void** output) -> std::int32_t {
            *output = mappedArrowGlow.data();
            return mappedArrowGlow.empty() ? -11 : VK_SUCCESS;
        };
        f.unmapMemory = [](void*, void*) {};
    }

    void PrepareVisibleWidgetFixture()
    {
        g_renderer.ready = true;
        g_renderer.renderPass = 0x210;
        g_renderer.commandBuffers = { 0x100 };
        g_renderer.framebuffers = { 0x200 };
        g_renderer.terrainDisabled = true;
        g_minimapVisible.store(true);
        g_playerArrow = {};
        g_playerArrow.attempted = g_playerArrow.loaded = true;
        g_playerArrow.width = g_playerArrow.height = 4;
        g_playerArrow.rgba.assign(4 * 4 * 4, 0);
        g_playerArrow.rgba[3] = 48;
        g_playerArrow.rgba[7] = 49;
        g_playerArrow.rgba[4 * (2 * 4 + 1) + 3] = 255;
        g_playerArrow.rgba[4 * (2 * 4 + 2) + 3] = 255;
        g_realMap.attempted = g_defaultMap.attempted = true;
        g_realMap.loaded = g_defaultMap.loaded = false;
        g_minimapFrame = {};
        g_minimapFrame.attempted = true;
        g_minimapFrame.loaded = false;
        g_playerPosition = {};
        g_playerPosition.valid = true;
        g_playerPosition.hasHeading = true;
        g_playerPosition.headingRadians = 0.25f;
        g_playerPosition.x = WorldToFixed(5120.0f);
        g_playerPosition.z = WorldToFixed(5120.0f);
        g_playerPosition.lastUpdateTick = GetTickCount();
        mappedArrowGlow.assign(128 * 128 * 4, 0);
        glowGpuDraws = legacyClearCalls = glowUploadCopies = glowFlushCalls = invalidPushCalls = 0;
        glowPushes.clear();
        commandOrder.clear();
        ConfigureOverlayCommands();
    }

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) < 0.0001f;
    }

    void CheckPushColor(const MinimapArrowGlow::PushConstants& push,
        const MinimapArrowGlow::Color& expected, const char* message)
    {
        Check(NearlyEqual(push.red, expected.red) && NearlyEqual(push.green, expected.green) &&
            NearlyEqual(push.blue, expected.blue), message);
    }

    void RunRequestedHaloDraw()
    {
        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "contrasting";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        std::vector<std::uint8_t> fixtureShader;
        const bool fixtureShaderRead = TryReadFrameShader("embervale_minimap_arrow_glow.vert.spv", fixtureShader);
        if (!fixtureShaderRead)
            std::cerr << "shader fixture root=" << g_modContext->shroudtopia.mod_folder << '\n';
        Check(fixtureShaderRead && !fixtureShader.empty(), "test fixture resolves the halo vertex shader bytes");

        Setup(0);
        PrepareVisibleWidgetFixture();

        Check(RecordVulkanMinimapCommandLocked(0), "visible minimap command records with halo enabled");
        Check(legacyClearCalls > 0, "unchanged local arrow and widget continue through legacy clears");
        Check(glowGpuDraws == 1 && glowPushes.size() == 1,
            "enabled player-arrow setting records one local GPU halo before the legacy body");
        Check(invalidPushCalls == 0, "halo draw uses the six-vertex quad and a 64-byte all-stage push block");
        Check(glowUploadCopies == 1 && glowFlushCalls == 1 && !g_renderer.arrowGlowResources.frameTextureUploadPending,
            "mask upload flushes mapped memory and records once before the render pass");
        Check(mappedArrowGlow[3] == 0 && mappedArrowGlow[7] == 255 &&
            mappedArrowGlow[4 * (2 * 4 + 1) + 3] == 255,
            "GPU mask includes exactly source alpha values above the legacy 48 threshold");
        Check(commandOrder.size() > 3 && commandOrder[0] == "barrier" && commandOrder[1] == "upload" &&
            commandOrder[2] == "barrier" && commandOrder[3] == "push",
            "mask upload is completed in command order before the first halo draw");
        const int createsAfterFirstFrame = calls;
        Check(RecordVulkanMinimapCommandLocked(0), "second eligible minimap command records");
        Check(calls == createsAfterFirstFrame && glowUploadCopies == 1 && glowFlushCalls == 1,
            "ready halo pipeline and immutable mask are reused without per-frame creation or upload");

        glowPushes.clear();
        g_renderer.widgetScale = 1.5f;
        g_renderer.widgetCenterX = 400;
        g_renderer.widgetCenterY = 300;
        g_playerArrowGlowStyle.store(static_cast<int>(MinimapArrowGlow::Style::SameColour));
        Check(TryDrawPlayerArrowGlow(g_renderer, reinterpret_cast<void*>(0x100),
            440, 360, 24, 1.25f, 400, 300, 80, false),
            "prepared local halo uses the same-colour style");
        Check(TryDrawPlayerArrowGlow(g_renderer, reinterpret_cast<void*>(0x100),
            440, 360, 24, -0.5f, 400, 300, 80, true),
            "prepared remote halo uses the same-colour style");
        Check(glowPushes.size() == 2 && NearlyEqual(glowPushes[0].centerX, 460.0f) &&
            NearlyEqual(glowPushes[0].centerY, 390.0f) && NearlyEqual(glowPushes[0].sizePixels, 36.0f) &&
            NearlyEqual(glowPushes[0].headingRadians, 1.25f) &&
            NearlyEqual(glowPushes[0].clipRadiusPixels, 120.0f),
            "quad carries scaled center, silhouette size, heading, and circular clip bounds");
        CheckPushColor(glowPushes[0], MinimapArrowGlow::SelectColor(MinimapArrowGlow::Style::SameColour, false),
            "same-colour local glow uses the brighter cyan palette");
        CheckPushColor(glowPushes[1], MinimapArrowGlow::SelectColor(MinimapArrowGlow::Style::SameColour, true),
            "same-colour remote glow uses the brighter green palette");

        glowPushes.clear();
        g_playerArrowGlowStyle.store(static_cast<int>(MinimapArrowGlow::Style::Contrasting));
        Check(TryDrawPlayerArrowGlow(g_renderer, reinterpret_cast<void*>(0x100),
            440, 360, 24, 0.75f, 400, 300, 80, false), "prepared local halo uses contrasting style");
        Check(TryDrawPlayerArrowGlow(g_renderer, reinterpret_cast<void*>(0x100),
            440, 360, 24, 0.25f, 400, 300, 80, true), "prepared remote halo uses contrasting style");
        Check(glowPushes.size() == 2, "both contrasting halo commands record");
        CheckPushColor(glowPushes[0], MinimapArrowGlow::SelectColor(MinimapArrowGlow::Style::Contrasting, false),
            "contrasting local glow uses amber-orange");
        CheckPushColor(glowPushes[1], MinimapArrowGlow::SelectColor(MinimapArrowGlow::Style::Contrasting, true),
            "contrasting remote glow uses magenta-violet");

        DestroyVulkanMinimapRendererLocked();
        Check(live.empty() && bindings.empty() && !g_renderer.arrowGlowResources.frameTextureReady,
            "renderer retirement releases the halo mask and pipeline resources");
    }

    void RunDisabledAndWorldIneligibleChecks()
    {
        configuredValues["player_arrow_glow"] = "false";
        configuredValues["player_arrow_glow_style"] = "same-colour";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        Setup(0);
        PrepareVisibleWidgetFixture();
        Check(RecordVulkanMinimapCommandLocked(0, true), "disabled glow still records the visible minimap");
        Check(calls == 0 && glowGpuDraws == 0 && legacyClearCalls > 0,
            "disabled glow creates no halo resources or GPU draws and preserves the legacy arrow body");
        DestroyVulkanMinimapRendererLocked();

        configuredValues["player_arrow_glow"] = "true";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        Setup(0);
        PrepareVisibleWidgetFixture();
        Check(RecordVulkanMinimapCommandLocked(0, false), "notice-only frame retains existing visible-widget recording");
        Check(calls == 0 && glowGpuDraws == 0 && legacyClearCalls > 0 &&
            !g_renderer.arrowGlowResources.frameTextureReady,
            "world-ineligible notice-only frame skips halo initialization and drawing");
        DestroyVulkanMinimapRendererLocked();

        Setup(0);
        PrepareVisibleWidgetFixture();
        g_minimapVisible.store(false);
        Check(RecordVulkanMinimapCommandLocked(0, true), "hidden minimap command still records its notice pass");
        Check(calls == 0 && glowGpuDraws == 0 && !g_renderer.arrowGlowResources.frameTextureReady,
            "user-hidden minimap performs no halo creation or draw work");
        DestroyVulkanMinimapRendererLocked();
        g_minimapVisible.store(true);
    }

    void RunBusySlotSkipsHaloCheck()
    {
        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "same-colour";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        Setup(0);
        PrepareVisibleWidgetFixture();
        g_renderer.renderCompleteSemaphores = { 0x300 };
        g_renderer.submissionFences = { 0x400 };
        busyFenceChecks = busyFenceResets = busySubmissions = 0;
        g_renderer.fns.getFenceStatus = BusyFenceStatus;
        g_renderer.fns.resetFences = BusyFenceReset;
        g_renderer.fns.queueSubmit = BusyQueueSubmit;
        VkPresentInfoKHR info{};
        VkPresentInfoKHR adjusted{};
        const void* adjustedWait = nullptr;
        Check(!SubmitVulkanMinimapFrameLocked(reinterpret_cast<void*>(0x500), info, 0,
            adjusted, &adjustedWait, true), "busy image slot skips the optional overlay submission");
        Check(busyFenceChecks == 1 && busyFenceResets == 0 && busySubmissions == 0 && calls == 0 &&
            !g_renderer.arrowGlowResources.frameTextureReady && glowGpuDraws == 0,
            "busy halo frame reuses the existing per-image fence without waits, setup, upload, or draws");
        DestroyVulkanMinimapRendererLocked();
    }

    void RunFailedCreationLatchCheck()
    {
        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "same-colour";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        Setup(0);
        PrepareVisibleWidgetFixture();
        failure = "sampler";
        Check(RecordVulkanMinimapCommandLocked(0), "failed optional setup still records the legacy minimap");
        Check(g_renderer.arrowGlowUnavailable && live.empty() && bindings.empty() &&
            legacyClearCalls > 0 && glowGpuDraws == 0,
            "partial halo setup releases resources, latches failure, and preserves the old arrow body");
        const int createsAfterFailure = calls;
        const int logsAfterFailure = static_cast<int>(std::count_if(configLogs.begin(), configLogs.end(),
            [](const std::string& line) { return line.find("GPU player-arrow halo unavailable") != std::string::npos; }));
        failure.clear();
        Check(RecordVulkanMinimapCommandLocked(0), "later frame after setup failure retains legacy recording");
        const int logsAfterSecondFrame = static_cast<int>(std::count_if(configLogs.begin(), configLogs.end(),
            [](const std::string& line) { return line.find("GPU player-arrow halo unavailable") != std::string::npos; }));
        Check(calls == createsAfterFailure && logsAfterSecondFrame == logsAfterFailure && legacyClearCalls > 0,
            "latched partial failure does not retry GPU creation or log again each visible frame");

        configuredValues["player_arrow_glow"] = "false";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        const int callsWhileDisabled = calls;
        const int drawsWhileDisabled = glowGpuDraws;
        const int legacyCallsBeforeDisabledFrame = legacyClearCalls;
        Check(RecordVulkanMinimapCommandLocked(0), "disabled halo after failure retains legacy minimap recording");
        Check(calls == callsWhileDisabled && drawsWhileDisabled == glowGpuDraws &&
            g_renderer.arrowGlowUnavailable && legacyClearCalls > legacyCallsBeforeDisabledFrame,
            "disabling a failed halo performs no GPU work and keeps the legacy arrow body");

        configuredValues["player_arrow_glow"] = "true";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        failure = "sampler";
        const int callsBeforeFailedReenable = calls;
        const int legacyCallsBeforeFailedReenable = legacyClearCalls;
        Check(RecordVulkanMinimapCommandLocked(0), "re-enabled halo with persistent setup failure keeps legacy recording");
        const int callsAfterFailedReenable = calls;
        const int logsAfterFailedReenable = static_cast<int>(std::count_if(configLogs.begin(), configLogs.end(),
            [](const std::string& line) { return line.find("GPU player-arrow halo unavailable") != std::string::npos; }));
        Check(callsAfterFailedReenable > callsBeforeFailedReenable && g_renderer.arrowGlowUnavailable &&
            live.empty() && bindings.empty() && legacyClearCalls > legacyCallsBeforeFailedReenable,
            "one explicit re-enable permits one setup retry then re-latches and preserves the legacy body");
        Check(logsAfterFailedReenable == 1, "a failed re-enable retry logs once after resetting the latch");
        failure.clear();
        Check(RecordVulkanMinimapCommandLocked(0), "frame after failed re-enable retry still records legacy minimap");
        const int logsAfterRepeatedFailure = static_cast<int>(std::count_if(configLogs.begin(), configLogs.end(),
            [](const std::string& line) { return line.find("GPU player-arrow halo unavailable") != std::string::npos; }));
        Check(calls == callsAfterFailedReenable && logsAfterRepeatedFailure == logsAfterFailedReenable &&
            g_renderer.arrowGlowUnavailable && legacyClearCalls > 0,
            "re-latched setup failure does not retry or log continuously on normal frames");

        configuredValues["player_arrow_glow"] = "false";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        const int callsBeforeSecondOff = calls;
        Check(RecordVulkanMinimapCommandLocked(0), "second disabled frame still records legacy minimap");
        Check(calls == callsBeforeSecondOff && g_renderer.arrowGlowUnavailable && legacyClearCalls > 0,
            "off transition does not clear the failure latch or retry setup");

        configuredValues["player_arrow_glow"] = "true";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        g_minimapVisible.store(false);
        const int callsBeforeHiddenReenable = calls;
        const int drawsBeforeHiddenReenable = glowGpuDraws;
        Check(RecordVulkanMinimapCommandLocked(0), "hidden re-enabled halo retains its notice pass");
        Check(calls == callsBeforeHiddenReenable && drawsBeforeHiddenReenable == glowGpuDraws &&
            g_renderer.arrowGlowUnavailable && !g_renderer.arrowGlowResources.frameTextureReady,
            "hidden frame defers re-enable recovery without GPU setup or drawing");

        g_minimapVisible.store(true);
        const int callsBeforeRecoveredFrame = calls;
        const int drawsBeforeRecoveredFrame = glowGpuDraws;
        const int legacyCallsBeforeRecoveredFrame = legacyClearCalls;
        Check(RecordVulkanMinimapCommandLocked(0), "visible frame after repaired setup recovers the halo");
        Check(calls > callsBeforeRecoveredFrame && !g_renderer.arrowGlowUnavailable &&
            g_renderer.arrowGlowResources.frameTextureReady && glowGpuDraws > drawsBeforeRecoveredFrame &&
            legacyClearCalls > legacyCallsBeforeRecoveredFrame,
            "visible re-enable retries successfully after hidden deferral and keeps the old arrow body");
        const int callsAfterRecovery = calls;
        Check(RecordVulkanMinimapCommandLocked(0), "subsequent recovered frame records normally");
        Check(calls == callsAfterRecovery && !g_renderer.arrowGlowUnavailable &&
            g_renderer.arrowGlowResources.frameTextureReady,
            "recovered halo resources are reused without another setup attempt");
        DestroyVulkanMinimapRendererLocked();
    }

    void RunMissingShaderLatchCheck(const std::string& missingShaderRoot, const std::string& shaderFixtureRoot)
    {
        configuredValues["player_arrow_glow"] = "true";
        configuredValues["player_arrow_glow_style"] = "same-colour";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");

        Setup(0);
        PrepareVisibleWidgetFixture();
        g_modContext->shroudtopia.mod_folder = missingShaderRoot;
        failure = "";
        const auto logCount = [&]() {
            return std::count_if(configLogs.begin(), configLogs.end(), [](const std::string& line) {
                return line.find("GPU player-arrow halo unavailable") != std::string::npos;
            });
        };
        Check(RecordVulkanMinimapCommandLocked(0), "missing-shader frame keeps recording the legacy minimap");
        Check(g_renderer.arrowGlowUnavailable && !g_renderer.arrowGlowResources.frameTextureReady &&
            legacyClearCalls > 0 && glowGpuDraws == 0,
            "missing shader disables only the GPU halo and leaves legacy arrows visible");
        Check(live.empty() && bindings.empty(), "missing shader unwinds the partially created GPU resources");
        const int createsAfterFailure = calls;
        const int logsAfterFailure = static_cast<int>(logCount());
        Check(logsAfterFailure == 1, "renderer logs the halo setup failure only once");

        const std::string assetsPath = missingShaderRoot + "/minimap_mod/assets";
        std::filesystem::create_directories(assetsPath);
        for (const char* name : { "embervale_minimap_arrow_glow.vert.spv", "embervale_minimap_arrow_glow.frag.spv" })
            std::filesystem::copy_file(shaderFixtureRoot + "/minimap_mod/assets/" + name,
                assetsPath + "/" + name, std::filesystem::copy_options::overwrite_existing);
        Check(RecordVulkanMinimapCommandLocked(0), "later frame after setup failure still records the legacy minimap");
        Check(calls == createsAfterFailure && static_cast<int>(logCount()) == logsAfterFailure &&
            g_renderer.arrowGlowUnavailable && legacyClearCalls > 0,
            "failure latch ignores shaders made available later without retrying setup or logging and keeps body fallback");

        configuredValues["player_arrow_glow"] = "false";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        const int callsWhileDisabled = calls;
        Check(RecordVulkanMinimapCommandLocked(0), "disabled missing-shader frame still records legacy minimap");
        Check(calls == callsWhileDisabled && g_renderer.arrowGlowUnavailable && legacyClearCalls > 0,
            "disabling after shader failure does not create resources and preserves legacy arrows");

        configuredValues["player_arrow_glow"] = "true";
        RefreshTestConfig("E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/no-config.json");
        const int callsBeforeShaderRecovery = calls;
        const int drawsBeforeShaderRecovery = glowGpuDraws;
        Check(RecordVulkanMinimapCommandLocked(0), "re-enabled halo retries after its shader files become available");
        Check(calls > callsBeforeShaderRecovery && !g_renderer.arrowGlowUnavailable &&
            g_renderer.arrowGlowResources.frameTextureReady && glowGpuDraws > drawsBeforeShaderRecovery &&
            legacyClearCalls > 0,
            "off-on transition recovers from a latched missing-shader failure while retaining the old body");
        DestroyVulkanMinimapRendererLocked();
        failure.clear();
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--config-only")
    {
        RunConfigChecks(argv[2]);
        return 0;
    }
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Expected the arrow-glow shader fixture root and optional task support directory.\n";
        return 2;
    }
    context.shroudtopia.mod_folder = argv[1];
    g_modContext = &context;
    const std::string supportRoot = argc == 3 ? argv[2] :
        "E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support";
    RunConfigChecks(supportRoot);
    const std::string shaderFixtureRoot = context.shroudtopia.mod_folder;
    RunRequestedHaloDraw();
    RunDisabledAndWorldIneligibleChecks();
    RunBusySlotSkipsHaloCheck();
    RunFailedCreationLatchCheck();
    const std::string missingShaderRoot = "E:/Scratch/minimap-hollow-proof-arrow-glow-20261025/task-1-support/missing-shaders-" +
        std::to_string(GetCurrentProcessId());
    RunMissingShaderLatchCheck(missingShaderRoot, shaderFixtureRoot);
    context.shroudtopia.mod_folder = shaderFixtureRoot;
    return 0;
}
