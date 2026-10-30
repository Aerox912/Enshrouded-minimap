// Production readers and drawing commands, without hooking or modifying a game.
#ifndef ESM_NATIVE_FILTER_PROBE
#define ESM_NATIVE_FILTER_PROBE 0
#endif
unsigned long long g_testMinimapConfigFileReadCount = 0;
unsigned long long g_testMinimapConfigBlockLookupCount = 0;
unsigned long long g_testMapMarkerCacheLockCount = 0;
unsigned long long g_testMapMarkerIdentitySearchCount = 0;
unsigned long long g_testMapMarkerResolverCallCount = 0;
unsigned long long g_testMapMarkerIconResolutionCount = 0;
void (*g_testBeforeMapMarkerResolver)() = nullptr;
#define MINIMAP_TEST_ICON_ATLAS_MUTATION 1
#define MINIMAP_TEST_PERF_FILTERS 1
#define MINIMAP_TEST_NATIVE_FILTER_PROBE 1
#include "../src/dllmain.cpp"
#undef MINIMAP_TEST_ICON_ATLAS_MUTATION
#undef MINIMAP_TEST_NATIVE_FILTER_PROBE
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
    int g_perfFilterTestFailures = 0;

    void CheckPerfFilter(bool condition, const char* message)
    {
        std::cout << (condition ? "PASS: " : "FAIL: ") << message << '\n';
        if (!condition)
            ++g_perfFilterTestFailures;
    }

    void ResetConfigSnapshotTestCounters()
    {
        ::g_testMinimapConfigFileReadCount = 0;
        ::g_testMinimapConfigBlockLookupCount = 0;
    }

    bool WriteConfigSnapshotFixture(const std::string& path, const std::string& contents)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << contents;
        return output.good();
    }

    bool ReplaceConfigSnapshotFixture(const std::string& path, const std::string& contents)
    {
        const std::string replacementPath = path + ".replacement";
        if (!WriteConfigSnapshotFixture(replacementPath, contents))
            return false;
        return MoveFileExA(replacementPath.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }

    int CheckMinimapConfigRefreshSnapshot(const char* configPathArgument)
    {
        const std::string configPath = configPathArgument != nullptr ? configPathArgument : "";
        if (configPath.empty())
        {
            std::cerr << "FAIL: config fixture path was not supplied\n";
            return 1;
        }

        const std::string savedGlobalConfigPath = g_shroudtopiaConfigPath;
        const auto savedModContext = g_modContext;
        g_shroudtopiaConfigPath.clear();
        ModContext config{};
        g_modContext = &config;
        config.shroudtopia.config_file = configPath;
        std::vector<std::string> apiKeys;
        std::vector<std::string> logLines;
        config.config.GetString = [&apiKeys](const char*, const char* key, std::string fallback)
        {
            apiKeys.emplace_back(key != nullptr ? key : "");
            if (key == nullptr)
                return fallback;
            if (std::strcmp(key, "position") == 0)
                return std::string("top-left");
            if (std::strcmp(key, "toggle_key") == 0)
                return std::string("F10");
            if (std::strcmp(key, "render_camera_fallback") == 0)
                return std::string("false");
            if (std::strcmp(key, "map_sample_step") == 0)
                return std::string("1");
            if (std::strcmp(key, "max_icons") == 0)
                return std::string("40");
            if (std::strcmp(key, "show_world_markers") == 0)
                return std::string("false");
            return fallback;
        };
        config.Log = [&logLines](const char* message)
        {
            if (message != nullptr)
                logLines.emplace_back(message);
        };

        g_perfFilterTestFailures = 0;
        const bool initialFixtureWritten = WriteConfigSnapshotFixture(configPath,
            "{\"minimap_mod\":{\"position\":\"bottom-left\",\"toggle_key\":\"F9\","
            "\"map_sample_step\":\"3\",\"max_icons\":\"41\",\"debug_logging\":\"false\"}}");
        CheckPerfFilter(initialFixtureWritten, "write the config snapshot fixture");
        if (initialFixtureWritten)
        {
            ResetConfigSnapshotTestCounters();
            apiKeys.clear();
            logLines.clear();
            RefreshMinimapConfig(&config, true);
            std::cout << "CONFIG_REFRESH_READS=" << ::g_testMinimapConfigFileReadCount
                << " CONFIG_BLOCK_LOOKUPS=" << ::g_testMinimapConfigBlockLookupCount << '\n';
            CheckPerfFilter(::g_testMinimapConfigFileReadCount == 1,
                "one refresh reads the config file once");
            CheckPerfFilter(::g_testMinimapConfigBlockLookupCount == 1,
                "one refresh locates the minimap_mod object once");
            CheckPerfFilter(MinimapPlacementName(static_cast<MinimapPlacement>(g_minimapPlacement.load())) == std::string("bottom-left") &&
                g_minimapToggleKey.load() == VK_F9 && g_minimapMapSampleStep.load() == 3 &&
                g_minimapMaxDrawnPoints.load() == 41,
                "file values win over conflicting API values");
            CheckPerfFilter(!g_renderCameraFallbackEnabled.load() && !g_showWorldMarkers.load() &&
                std::count(apiKeys.begin(), apiKeys.end(), "position") == 0 &&
                std::count(apiKeys.begin(), apiKeys.end(), "render_camera_fallback") == 1,
                "an absent file key falls back independently to the API");
            const std::string expectedPositionSource = " | position_source=" + configPath;
            const std::string expectedToggleSource = " | toggle_source=" + configPath;
            CheckPerfFilter(!logLines.empty() && logLines.back().find(expectedPositionSource) != std::string::npos &&
                logLines.back().find(expectedToggleSource) != std::string::npos &&
                logLines.back().find(" | render_camera_fallback_source=shroudtopia_config_api") != std::string::npos,
                "file and API source labels remain unchanged");

            CheckPerfFilter(ReplaceConfigSnapshotFixture(configPath,
                "{\"minimap_mod\":{\"position\":\"top-left\"}}"),
                "replace config fixture before the next poll");
            ResetConfigSnapshotTestCounters();
            config.config.GetString = nullptr;
            RefreshMinimapConfig(&config, true);
            CheckPerfFilter(MinimapPlacementName(static_cast<MinimapPlacement>(g_minimapPlacement.load())) == std::string("top-left") &&
                ::g_testMinimapConfigFileReadCount == 1 && ::g_testMinimapConfigBlockLookupCount == 1,
                "the next refresh reads a replacement file without a cross-refresh cache");

            CheckPerfFilter(ReplaceConfigSnapshotFixture(configPath,
                "{\"minimap_mod\":{}}"), "write an empty minimap_mod block");
            ResetConfigSnapshotTestCounters();
            RefreshMinimapConfig(&config, true);
            CheckPerfFilter(g_minimapPlacement.load() == static_cast<int>(MinimapPlacement::TopRight) &&
                g_minimapMapSampleStep.load() == MINIMAP_DEFAULT_MAP_SAMPLE_STEP &&
                g_minimapMaxDrawnPoints.load() == MINIMAP_DEFAULT_MAX_DRAWN_POINTS &&
                ::g_testMinimapConfigFileReadCount == 1 && ::g_testMinimapConfigBlockLookupCount == 1,
                "a present block with absent keys retains defaults");

            CheckPerfFilter(ReplaceConfigSnapshotFixture(configPath,
                "{\"other_mod\":{\"position\":\"bottom-left\"}}"),
                "write a config without a minimap_mod block");
            config.config.GetString = [](const char*, const char* key, std::string fallback)
            {
                return key != nullptr && std::strcmp(key, "position") == 0 ? std::string("bottom-right") : fallback;
            };
            ResetConfigSnapshotTestCounters();
            RefreshMinimapConfig(&config, true);
            CheckPerfFilter(MinimapPlacementName(static_cast<MinimapPlacement>(g_minimapPlacement.load())) == std::string("bottom-right") &&
                ::g_testMinimapConfigFileReadCount == 1 && ::g_testMinimapConfigBlockLookupCount == 1,
                "an absent minimap_mod block uses the API fallback");

            config.shroudtopia.config_file = configPath + ".missing";
            config.config.GetString = [](const char*, const char* key, std::string fallback)
            {
                return key != nullptr && std::strcmp(key, "position") == 0 ? std::string("top-right") : fallback;
            };
            ResetConfigSnapshotTestCounters();
            RefreshMinimapConfig(&config, true);
            CheckPerfFilter(g_minimapPlacement.load() == static_cast<int>(MinimapPlacement::TopRight) &&
                ::g_testMinimapConfigFileReadCount == 1 && ::g_testMinimapConfigBlockLookupCount == 0,
                "an unreadable config file falls back without scanning an object");
        }

        DeleteFileA(configPath.c_str());
        DeleteFileA((configPath + ".replacement").c_str());
        g_shroudtopiaConfigPath = savedGlobalConfigPath;
        g_modContext = savedModContext;
        return g_perfFilterTestFailures == 0 ? 0 : 1;
    }

    void ResetMapMarkerFastPathTestCounters()
    {
        ::g_testMapMarkerIconResolutionCount = 0;
        ::g_testMapMarkerCacheLockCount = 0;
        ::g_testMapMarkerIdentitySearchCount = 0;
        ::g_testMapMarkerResolverCallCount = 0;
        ::g_testBeforeMapMarkerResolver = nullptr;
    }

    void ClearMapMarkerFastPathTestCache(DWORD pruneTick, DWORD summaryTick)
    {
        std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
        g_visibleMapMarkers.clear();
        g_lastVisibleMapMarkerPruneTick = pruneTick;
        g_lastVisibleMapMarkerSummaryTick = summaryTick;
    }

    CapturedMapMarkerVisibility MakeMapMarkerFastPathTestMarker(
        uintptr_t markerAddress, uintptr_t stateAddress, std::uint32_t kind,
        bool positioned, std::int64_t x, std::int64_t y, std::int64_t z, DWORD tick)
    {
        CapturedMapMarkerVisibility marker{};
        marker.markerAddress = markerAddress;
        marker.stateAddress = stateAddress;
        marker.kind = kind;
        marker.visibility = 1;
        marker.hasWorldPosition = positioned;
        marker.x = x;
        marker.y = y;
        marker.z = z;
        marker.lastUpdateTick = tick;
        return marker;
    }

    CapturedMapMarkerVisibility g_concurrentMapMarkerPublication{};
    bool g_concurrentMapMarkerLockAvailable = false;
    bool g_concurrentMapMarkerPublicationCompleted = false;

    void PublishConcurrentMapMarkerDuringResolution()
    {
        std::thread concurrentPublisher([]
        {
            std::unique_lock<std::mutex> probe(g_visibleMapMarkerMutex, std::try_to_lock);
            g_concurrentMapMarkerLockAvailable = probe.owns_lock();
            if (!probe.owns_lock())
                return;
            probe.unlock();

            PublishVisibleMapMarker(CapturedMapMarkerVisibility(g_concurrentMapMarkerPublication));
            g_concurrentMapMarkerPublicationCompleted = true;
        });
        concurrentPublisher.join();
    }

    int CheckUnresolvedMarkerRetries()
    {
        g_perfFilterTestFailures = 0;
        const bool savedDebug = g_debugLoggingEnabled.load();
        const bool savedVisible = g_minimapVisible.load();
        g_debugLoggingEnabled = false;
        std::array<std::uint64_t, 32> raw{};
        std::array<std::uint8_t, 0x60> state{};
        std::array<std::uint8_t, 0x180> entity{};
        std::array<std::uint8_t, 0x100> unknown{};
        std::array<std::uint8_t, 0x100> otherUnknown{};
        MapMarkerVisibilityIterationRecord record{};
        record.marker = reinterpret_cast<std::uint8_t*>(raw.data());
        record.visibilityState = state.data();
        record.entity = entity.data();
        record.unknown0 = unknown.data();
        state[0] = 1;
        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        ResetMapMarkerFastPathTestCounters();
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record) &&
            g_visibleMapMarkers.size() == 1 && !g_visibleMapMarkers[0].hasWorldPosition &&
            ::g_testMapMarkerResolverCallCount == 1, "first unresolved visible marker attempts resolution");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1,
            "unresolved visible marker resolves its kind from the current atlas");
        for (bool visible : { true, false })
        {
            g_minimapVisible = visible;
            for (int i = 0; i < 8; ++i) TryPublishMapMarkerVisibilityRecord(record);
        }
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 1,
            "identical failures are deferred independently of map display toggle");
        CheckPerfFilter(g_visibleMapMarkers[0].visibility == 1 &&
            GetTickCount() - g_visibleMapMarkers[0].lastUpdateTick < 200,
            "deferred callbacks retain fresh native visibility and cache age");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 17,
            "unresolved retries keep resolving kind instead of reusing a positioned-only result");
        Sleep(220);
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 2,
            "failed resolution retries after bounded interval without starvation");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 18,
            "deadline retry resolves kind again from the current atlas");
        raw[0] = 1234;
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 3,
            "raw identity change invalidates failed retry state immediately");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 19,
            "raw identity change remains on the existing icon resolver path");
        record.unknown0 = otherUnknown.data();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 4,
            "resolver root change invalidates failed retry state immediately");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 20,
            "resolver root change remains on the existing icon resolver path");
        state[0] = 0;
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(g_visibleMapMarkers[0].visibility == 0 &&
            ::g_testMapMarkerResolverCallCount == 4, "hidden transition is captured without resolution");
        state[0] = 1;
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 5,
            "hidden to visible retries immediately");
        // Coordinates can appear outside the raw identity words without any identity change.
        const std::int64_t xyz[3] = { WorldToFixed(4000), WorldToFixed(200), WorldToFixed(5000) };
        std::memcpy(entity.data() + 0x80, xyz, sizeof(xyz));
        Sleep(220);
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 6 && g_visibleMapMarkers[0].hasWorldPosition,
            "eventual retry discovers newly available coordinates for unchanged identity");
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 6 && g_visibleMapMarkers[0].hasWorldPosition,
            "successful coordinate capture returns to existing positioned fast path");
        ResetWorldSessionFromLog();
        CheckPerfFilter(g_visibleMapMarkers.empty(), "session reset clears retry-bearing entries");
        entity.fill(0);
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 7,
            "same addresses retry immediately in a new session");
        CapturedMapMarkerVisibility failed = g_visibleMapMarkers[0];
        failed.positionRetryPending = true;
        failed.positionFailureTick = 0xFFFFFFF0u;
        CheckPerfFilter(ShouldDeferMarkerPosition(failed, failed, 0xB7u) &&
            !ShouldDeferMarkerPosition(failed, failed, 0xB8u),
            "retry boundary is 200 ms and remains correct across DWORD wrap");
        const auto rejectsChanged = [&failed](CapturedMapMarkerVisibility changed) {
            return !ShouldDeferMarkerPosition(failed, changed, failed.positionFailureTick);
        };
        auto changed = failed; ++changed.markerAddress;
        CheckPerfFilter(rejectsChanged(changed), "marker address change rejects retry suppression");
        changed = failed; ++changed.stateAddress;
        CheckPerfFilter(rejectsChanged(changed), "state address change rejects retry suppression");
        changed = failed; ++changed.entityAddress;
        CheckPerfFilter(rejectsChanged(changed), "entity address change rejects retry suppression");
        changed = failed; ++changed.markerType;
        CheckPerfFilter(rejectsChanged(changed), "fresh type change rejects retry suppression");
        changed = failed; ++changed.kind;
        CheckPerfFilter(rejectsChanged(changed), "fresh icon kind change rejects retry suppression");
        const DWORD deadline = g_visibleMapMarkers[0].positionFailureTick;
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(g_visibleMapMarkers[0].positionFailureTick == deadline,
            "deferred captures do not extend failure deadline");
        // Exercise the publication side of a resolver racing a newer positioned capture.
        auto lateFailure = g_visibleMapMarkers[0];
        auto positioned = lateFailure;
        positioned.hasWorldPosition = true;
        positioned.positionRetryPending = false;
        positioned.x = WorldToFixed(4444);
        PublishVisibleMapMarker(std::move(positioned));
        PublishVisibleMapMarker(std::move(lateFailure));
        CheckPerfFilter(g_visibleMapMarkers[0].hasWorldPosition &&
            g_visibleMapMarkers[0].x == WorldToFixed(4444) && !g_visibleMapMarkers[0].positionRetryPending,
            "concurrent successful position survives late failure without retry state");
        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        auto newer = failed; ++newer.entityAddress; newer.positionRetryPending = false;
        PublishVisibleMapMarker(std::move(newer));
        PublishVisibleMapMarker(std::move(failed));
        CheckPerfFilter(!g_visibleMapMarkers[0].positionRetryPending,
            "late failed snapshot cannot add suppression over a changed concurrent capture");
        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        ::g_testBeforeMapMarkerResolver = []() { ResetWorldSessionFromLog(); };
        TryPublishMapMarkerVisibilityRecord(record);
        ::g_testBeforeMapMarkerResolver = nullptr;
        CheckPerfFilter(g_visibleMapMarkers.size() == 1 && !g_visibleMapMarkers[0].positionRetryPending,
            "in-flight failure finishing after world reset cannot carry retry state into new session");
        const auto attemptsAfterReset = ::g_testMapMarkerResolverCallCount;
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == attemptsAfterReset + 1,
            "same-address new-session capture resolves immediately after old scan completes");
        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        g_debugLoggingEnabled = savedDebug;
        g_minimapVisible = savedVisible;
        return g_perfFilterTestFailures == 0 ? 0 : 1;
    }

    int CheckHiddenMarkerMissFastPath()
    {
        g_perfFilterTestFailures = 0;
        const bool savedDebug = g_debugLoggingEnabled.load();
        std::array<std::uint64_t, 32> raw{};
        raw[0] = 0x1234;
        std::array<std::uint8_t, 0x60> state{};
        std::array<std::uint8_t, 0x180> entity{};
        std::array<std::uint8_t, 0x100> unknown{};
        MapMarkerVisibilityIterationRecord record{};
        record.marker = reinterpret_cast<std::uint8_t*>(raw.data());
        record.visibilityState = state.data();
        record.entity = entity.data();
        record.unknown0 = unknown.data();

        for (bool debug : {false, true})
        {
            g_debugLoggingEnabled = debug;
            const DWORD now = GetTickCount();
            ClearMapMarkerFastPathTestCache(now, now);
            auto unrelated = MakeMapMarkerFastPathTestMarker(0x500000, 0x600000,
                0x2222, true, 444, 555, 666, now);
            g_visibleMapMarkers.push_back(unrelated);
            ResetMapMarkerFastPathTestCounters();
            g_visibleMapMarkerFaults.store(3);
            for (int i = 0; i < 8; ++i)
                CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record), "hidden miss accepted");
            CheckPerfFilter(::g_testMapMarkerCacheLockCount == 8 &&
                ::g_testMapMarkerIdentitySearchCount == 8 && ::g_testMapMarkerResolverCallCount == 0,
                "repeated hidden misses use one lock/search and no position resolver, including debug mode");
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 0,
                "uncached hidden markers never resolve an icon or acquire the icon-atlas mutex");
            CheckPerfFilter(g_visibleMapMarkers.size() == 1 &&
                g_visibleMapMarkers[0].stateAddress == unrelated.stateAddress &&
                g_visibleMapMarkers[0].visibility == 1 && g_visibleMapMarkers[0].x == 444 &&
                g_visibleMapMarkers[0].lastUpdateTick == now && g_visibleMapMarkerFaults.load() == 0,
                "hidden misses do not insert or change unrelated entries and clear capture faults");
        }

        g_debugLoggingEnabled = false;
        DWORD now = GetTickCount();
        ClearMapMarkerFastPathTestCache(now - VISIBLE_MAP_MARKER_PRUNE_MS - 1, now);
        auto stale = MakeMapMarkerFastPathTestMarker(0x500000, 0x600000,
            0x2222, true, 444, 555, 666, now - VISIBLE_MAP_MARKER_STALE_MS - 100);
        auto fresh = MakeMapMarkerFastPathTestMarker(0x700000, 0x800000,
            0x3333, true, 777, 888, 999, now);
        g_visibleMapMarkers.push_back(stale);
        g_visibleMapMarkers.push_back(fresh);
        ResetMapMarkerFastPathTestCounters();
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record), "hidden miss permits due pruning");
        CheckPerfFilter(g_visibleMapMarkers.size() == 1 &&
            g_visibleMapMarkers[0].stateAddress == fresh.stateAddress &&
            GetTickCount() - g_lastVisibleMapMarkerPruneTick < VISIBLE_MAP_MARKER_PRUNE_MS,
            "hidden miss removes stale unrelated entries and advances pruning deadline");
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 1 && ::g_testMapMarkerIdentitySearchCount == 1,
            "due pruning does not require a second hidden-miss lookup");

        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record) && g_visibleMapMarkers.empty(),
            "hidden marker is absent before its visibility changes");
        state[0] = 1;
        ResetMapMarkerFastPathTestCounters();
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record) && g_visibleMapMarkers.size() == 1 &&
            g_visibleMapMarkers[0].visibility == 1 && ::g_testMapMarkerResolverCallCount == 1 &&
            ::g_testMapMarkerCacheLockCount == 2 && ::g_testMapMarkerIdentitySearchCount == 2,
            "previous hidden miss is not negatively cached and immediately enters normal visible publication");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1,
            "a newly visible marker resolves its current icon immediately");
        g_visibleMapMarkers[0].hasWorldPosition = true;
        g_visibleMapMarkers[0].x = 111;
        state[0] = 0;
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record) && g_visibleMapMarkers.size() == 1 &&
            g_visibleMapMarkers[0].visibility == 0 && g_visibleMapMarkers[0].x == 111,
            "cached marker still becomes hidden and retains position");
        ResetMapMarkerFastPathTestCounters();
        MapMarkerVisibilityIterationRecord invalid{};
        CheckPerfFilter(!TryPublishMapMarkerVisibilityRecord(invalid) &&
            ::g_testMapMarkerCacheLockCount == 0, "invalid record still rejected before cache access");
        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        g_debugLoggingEnabled = savedDebug;
        return g_perfFilterTestFailures == 0 ? 0 : 1;
    }

    int CheckVisibleMapMarkerFastPath(const char* atlasAssetDirectory = nullptr)
    {
        constexpr std::uint32_t kFixtureKnownMarkerIcon = 0x000F0001u;
        constexpr std::uint32_t kFixtureUnknownMarkerIcon = 0x000F0002u;
        g_perfFilterTestFailures = 0;
        const DWORD now = GetTickCount();
        const bool savedDebugLogging = g_debugLoggingEnabled.load();
        ModContext markerLogContext{};
        const auto savedModContext = g_modContext;
        std::vector<std::string> logLines;
        markerLogContext.Log = [&logLines](const char* message)
        {
            if (message != nullptr)
                logLines.emplace_back(message);
        };
        g_modContext = &markerLogContext;
        MinimapIconAtlas savedIconAtlas{};
        {
            std::lock_guard<std::mutex> lock(g_minimapIconMutex);
            savedIconAtlas = g_minimapIcons;
        }
        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) {
            atlas = {};
            atlas.attempted = true;
        });
        std::array<std::uint64_t, 32> markerRaw{};
        markerRaw[0] = 0x1234;
        markerRaw[1] = kFixtureKnownMarkerIcon;
        std::array<std::uint8_t, 0x60> markerState{};
        std::array<std::uint8_t, 0x180> markerEntity{};
        std::array<std::uint8_t, 0x100> markerUnknown{};
        markerState[0] = 0;
        MapMarkerVisibilityIterationRecord record{};
        record.marker = reinterpret_cast<std::uint8_t*>(markerRaw.data());
        record.visibilityState = markerState.data();
        record.entity = markerEntity.data();
        record.unknown0 = markerUnknown.data();

        ClearMapMarkerFastPathTestCache(now, now);
        g_debugLoggingEnabled = false;
        const auto expectedIcon = ResolveIconKeyFromRawWordsIfAtlasLoadedWithGeneration(
            markerRaw.data(), 5, 0x1234);
        const auto positioned = MakeMapMarkerFastPathTestMarker(
            reinterpret_cast<uintptr_t>(record.marker), reinterpret_cast<uintptr_t>(record.visibilityState),
            expectedIcon.kind, true, 111, 222, 333, now - 100);
        auto positionedWithSnapshot = positioned;
        std::memcpy(positionedWithSnapshot.raw, markerRaw.data(), sizeof(positionedWithSnapshot.raw));
        positionedWithSnapshot.markerId = 0x1234;
        positionedWithSnapshot.iconAtlasGeneration = expectedIcon.atlasGeneration;
        auto unrelated = MakeMapMarkerFastPathTestMarker(0x500000, 0x600000,
            0x2222, true, 444, 555, 666, now - 50);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(positionedWithSnapshot);
            g_visibleMapMarkers.push_back(unrelated);
        }
        ResetMapMarkerFastPathTestCounters();
        const bool positionedHitPublished = TryPublishMapMarkerVisibilityRecord(record);
        std::cout << "POSITIONED_HIT_LOCKS=" << ::g_testMapMarkerCacheLockCount
            << " IDENTITY_SEARCHES=" << ::g_testMapMarkerIdentitySearchCount
            << " RESOLVER_CALLS=" << ::g_testMapMarkerResolverCallCount
            << " ICON_RESOLVER_CALLS=" << ::g_testMapMarkerIconResolutionCount << '\n';
        CheckPerfFilter(positionedHitPublished, "positioned cached hit accepts the visibility record");
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 1 &&
            ::g_testMapMarkerIdentitySearchCount == 1 && ::g_testMapMarkerResolverCallCount == 0,
            "positioned cache hit completes under one identity search and mutex acquisition");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 0,
            "identical positioned raw/type snapshot reuses kind without resolving the icon again");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.size() == 2 &&
                g_visibleMapMarkers[0].stateAddress == reinterpret_cast<uintptr_t>(record.visibilityState) &&
                g_visibleMapMarkers[1].stateAddress == unrelated.stateAddress,
                "positioned cache hit preserves vector order and unrelated entries");
            CheckPerfFilter(g_visibleMapMarkers[0].visibility == 0 && g_visibleMapMarkers[0].kind == expectedIcon.kind &&
                g_visibleMapMarkers[0].iconAtlasGeneration == expectedIcon.atlasGeneration &&
                g_visibleMapMarkers[0].hasWorldPosition && g_visibleMapMarkers[0].x == 111 &&
                g_visibleMapMarkers[0].y == 222 && g_visibleMapMarkers[0].z == 333 &&
                static_cast<DWORD>(g_visibleMapMarkers[0].lastUpdateTick - (now - 100)) < 1000,
                "positioned cache hit updates visibility and timestamp while retaining position");
            CheckPerfFilter(g_visibleMapMarkers[1].kind == unrelated.kind &&
                g_visibleMapMarkers[1].x == unrelated.x && g_visibleMapMarkers[1].y == unrelated.y &&
                g_visibleMapMarkers[1].z == unrelated.z,
                "positioned cache hit leaves unrelated marker fields unchanged");
        }

        markerState[0] = 1;
        ResetMapMarkerFastPathTestCounters();
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record),
            "positioned cache refresh accepts visible transition");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 0,
            "visible transition reuses kind while visibility remains fresh");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers[0].visibility == 1 &&
                g_visibleMapMarkers[0].x == 111 && g_visibleMapMarkers[0].y == 222 && g_visibleMapMarkers[0].z == 333,
                "visible transition updates visibility and retains cached coordinates");
        }
        markerState[0] = 0;
        ResetMapMarkerFastPathTestCounters();
        CheckPerfFilter(TryPublishMapMarkerVisibilityRecord(record),
            "positioned cache refresh accepts hidden transition");
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 0,
            "hidden transition reuses kind without reusing visibility");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers[0].visibility == 0 &&
                g_visibleMapMarkers[0].x == 111 && g_visibleMapMarkers[0].y == 222 && g_visibleMapMarkers[0].z == 333,
                "hidden transition is immediate and retains cached coordinates");
        }

        markerState[0] = 1;
        markerRaw[1] = kFixtureUnknownMarkerIcon;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1,
            "changed raw words invalidate positioned icon reuse");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers[0].kind == 0x1234 &&
                std::memcmp(g_visibleMapMarkers[0].raw, markerRaw.data(), sizeof(g_visibleMapMarkers[0].raw)) == 0,
                "no-atlas unknown raw words preserve the marker-id fallback");
        }
        markerRaw[1] = kFixtureKnownMarkerIcon;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1,
            "restored raw words still invalidate the previous snapshot");
        markerRaw[2] = MAP_MARKER_QUEST_KIND;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].markerType == MAP_MARKER_QUEST_KIND &&
                g_visibleMapMarkers[0].kind == MAP_MARKER_QUEST_KIND,
                "changed marker type is resolved instead of reusing the positioned kind");
        }
        markerRaw[2] = 0;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == 0x1234,
                "restored marker type resolves its no-atlas fallback");
        }

        auto installOneIconAtlas = [](std::uint32_t key) {
            MutateMinimapIconAtlasForTest([key](MinimapIconAtlas& atlas) {
                atlas = {};
                atlas.attempted = true;
                atlas.loaded = true;
                atlas.path = "marker-kind-generation-fixture";
                MinimapIcon icon{};
                icon.key = key;
                atlas.icons.push_back(std::move(icon));
            });
        };
        const auto generationBeforeLoad = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        installOneIconAtlas(kFixtureKnownMarkerIcon);
        const auto loadedAtlasGeneration = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        CheckPerfFilter(loadedAtlasGeneration > generationBeforeLoad,
            "loading a fixture atlas advances its generation monotonically");
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1,
            "no-atlas cached result is invalidated after atlas loading");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers[0].kind == kFixtureKnownMarkerIcon &&
                g_visibleMapMarkers[0].iconAtlasGeneration == loadedAtlasGeneration,
                "loaded atlas resolves the known raw icon with its matching generation");
        }
        markerRaw[1] = kFixtureUnknownMarkerIcon;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == 0x1234,
                "unknown loaded-atlas icon retains the existing marker-id fallback");
        }
        markerRaw[1] = kFixtureKnownMarkerIcon;
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == kFixtureKnownMarkerIcon,
                "changed raw words resolve a known icon again");
        }

        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) {
            atlas = {};
            atlas.attempted = true;
        });
        const auto resetAtlasGeneration = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        CheckPerfFilter(resetAtlasGeneration > loadedAtlasGeneration,
            "atlas reset advances rather than reusing an earlier generation");
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == 0x1234 &&
                g_visibleMapMarkers[0].iconAtlasGeneration == resetAtlasGeneration,
                "atlas reset invalidates the old icon result without an ABA generation match");
        }
        installOneIconAtlas(kFixtureKnownMarkerIcon);
        const auto reloadedAtlasGeneration = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        CheckPerfFilter(reloadedAtlasGeneration > resetAtlasGeneration,
            "reloading after reset keeps atlas generations monotonic");
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == kFixtureKnownMarkerIcon &&
                g_visibleMapMarkers[0].iconAtlasGeneration == reloadedAtlasGeneration,
                "reloaded atlas replaces the fallback with the current icon result");
        }

        MutateMinimapIconAtlasForTest([](MinimapIconAtlas& atlas) { atlas = {}; });
        const auto generationBeforeLazyLoad = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        std::array<char, MAX_PATH> previousDirectory{};
        const DWORD previousDirectoryLength = GetCurrentDirectoryA(
            static_cast<DWORD>(previousDirectory.size()), previousDirectory.data());
        const bool hasRestorableDirectory = previousDirectoryLength > 0 &&
            previousDirectoryLength < static_cast<DWORD>(previousDirectory.size());
        const char* requestedAssetDirectory = atlasAssetDirectory != nullptr && atlasAssetDirectory[0] != '\0' ?
            atlasAssetDirectory : "assets";
        const bool changedToAssetDirectory = hasRestorableDirectory &&
            SetCurrentDirectoryA(requestedAssetDirectory) != FALSE;
        const auto savedLoadContext = g_modContext;
        g_modContext = nullptr;
        if (changedToAssetDirectory)
            EnsureMinimapIconsLoaded();
        const bool restoredDirectory = !changedToAssetDirectory ||
            SetCurrentDirectoryA(previousDirectory.data()) != FALSE;
        g_modContext = savedLoadContext;
        CheckPerfFilter(restoredDirectory, "lazy atlas fixture restores the original working directory");
        bool actualAtlasLoaded = false;
        {
            std::lock_guard<std::mutex> lock(g_minimapIconMutex);
            actualAtlasLoaded = g_minimapIcons.attempted && g_minimapIcons.loaded;
        }
        const auto generationAfterLazyLoad = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
        CheckPerfFilter(changedToAssetDirectory && actualAtlasLoaded &&
            generationAfterLazyLoad == generationBeforeLazyLoad + 1,
            "successful lazy atlas load publishes exactly one generation after the fixture is complete");
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(::g_testMapMarkerIconResolutionCount == 1 &&
                g_visibleMapMarkers[0].kind == 0x1234 &&
                g_visibleMapMarkers[0].iconAtlasGeneration == generationAfterLazyLoad,
                "successful lazy load invalidates a cached unknown icon using the current generation");
        }

        record.marker = reinterpret_cast<std::uint8_t*>(markerRaw.data());
        record.visibilityState = markerState.data();
        ClearMapMarkerFastPathTestCache(now, now - 5000);
        g_debugLoggingEnabled = true;
        auto debugMarker = MakeMapMarkerFastPathTestMarker(
            reinterpret_cast<uintptr_t>(record.marker), reinterpret_cast<uintptr_t>(record.visibilityState),
            0x3333, true, 777, 888, 999, now - 100);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(debugMarker);
        }
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 2 &&
            ::g_testMapMarkerIdentitySearchCount == 2 && ::g_testMapMarkerResolverCallCount == 0,
            "debug-enabled captures retain the established two-phase path");
        CheckPerfFilter(!logLines.empty() &&
            logLines.back().find("real map marker visibility feed | visible=1 | positioned=1") != std::string::npos,
            "debug-enabled capture keeps the throttled marker summary");

        markerState[0] = 0;
        record.visibilityState = markerState.data();
        auto staleMarker = MakeMapMarkerFastPathTestMarker(
            reinterpret_cast<uintptr_t>(record.marker), reinterpret_cast<uintptr_t>(record.visibilityState),
            0x4444, true, 12345, 23456, 34567, now - 181000);
        ClearMapMarkerFastPathTestCache(now - VISIBLE_MAP_MARKER_PRUNE_MS, now);
        g_debugLoggingEnabled = false;
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(staleMarker);
        }
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 2 &&
            ::g_testMapMarkerIdentitySearchCount == 2 && ::g_testMapMarkerResolverCallCount == 0,
            "due-prune captures keep the baseline two-phase path");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.size() == 0,
                "due pruning preserves the baseline visibility-zero removal");
        }

        markerState[0] = 1;
        staleMarker.visibility = 1;
        const DWORD reinsertNow = GetTickCount();
        ClearMapMarkerFastPathTestCache(reinsertNow - VISIBLE_MAP_MARKER_PRUNE_MS, reinsertNow);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(staleMarker);
        }
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 2 &&
            ::g_testMapMarkerIdentitySearchCount == 2 && ::g_testMapMarkerResolverCallCount == 0,
            "due-prune same-identity hits do not take the fast path");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.size() == 1 && g_visibleMapMarkers[0].visibility == 1 &&
                g_visibleMapMarkers[0].hasWorldPosition && g_visibleMapMarkers[0].x == 12345 &&
                g_visibleMapMarkers[0].y == 23456 && g_visibleMapMarkers[0].z == 34567,
                "due pruning retains the accepted expired-position reuse and reinsert behavior");
        }

        const DWORD pruneNow = GetTickCount();
        auto exactBoundary = MakeMapMarkerFastPathTestMarker(0x700000, 0x700100,
            1, true, 1, 2, 3, pruneNow - VISIBLE_MAP_MARKER_STALE_MS);
        auto beyondBoundary = MakeMapMarkerFastPathTestMarker(0x700200, 0x700300,
            1, true, 4, 5, 6, pruneNow - VISIBLE_MAP_MARKER_STALE_MS - 1);
        ClearMapMarkerFastPathTestCache(pruneNow - VISIBLE_MAP_MARKER_PRUNE_MS, pruneNow);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(exactBoundary);
            g_visibleMapMarkers.push_back(beyondBoundary);
            PruneVisibleMapMarkersLocked(pruneNow);
            CheckPerfFilter(g_visibleMapMarkers.size() == 1 &&
                g_visibleMapMarkers[0].stateAddress == exactBoundary.stateAddress,
                "pruning keeps an entry exactly at the stale threshold and removes one past it");
        }

        markerState[0] = 1;
        record.visibilityState = markerState.data();
        const DWORD concurrencyNow = GetTickCount();
        ClearMapMarkerFastPathTestCache(concurrencyNow, concurrencyNow);
        const auto unresolvedMarker = MakeMapMarkerFastPathTestMarker(
            reinterpret_cast<uintptr_t>(record.marker), reinterpret_cast<uintptr_t>(record.visibilityState),
            0x5555, false, 0, 0, 0, concurrencyNow - 20);
        g_concurrentMapMarkerPublication = MakeMapMarkerFastPathTestMarker(
            reinterpret_cast<uintptr_t>(record.marker), reinterpret_cast<uintptr_t>(record.visibilityState),
            0x6666, true, 444, 555, 666, concurrencyNow - 10);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(unresolvedMarker);
        }
        g_concurrentMapMarkerLockAvailable = false;
        g_concurrentMapMarkerPublicationCompleted = false;
        ::g_testBeforeMapMarkerResolver = PublishConcurrentMapMarkerDuringResolution;
        ResetMapMarkerFastPathTestCounters();
        ::g_testBeforeMapMarkerResolver = PublishConcurrentMapMarkerDuringResolution;
        TryPublishMapMarkerVisibilityRecord(record);
        ::g_testBeforeMapMarkerResolver = nullptr;
        CheckPerfFilter(::g_testMapMarkerResolverCallCount == 1 &&
            g_concurrentMapMarkerLockAvailable && g_concurrentMapMarkerPublicationCompleted,
            "native resolver runs outside the cache mutex and permits concurrent publication");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.size() == 1 && g_visibleMapMarkers[0].hasWorldPosition &&
                g_visibleMapMarkers[0].x == 444 && g_visibleMapMarkers[0].y == 555 && g_visibleMapMarkers[0].z == 666,
                "relock reconciliation retains a concurrent positioned update");
        }

        const DWORD capNow = GetTickCount();
        ClearMapMarkerFastPathTestCache(capNow, capNow);
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            for (uintptr_t index = 1; index <= VISIBLE_MAP_MARKER_MAX_ENTRIES; ++index)
                g_visibleMapMarkers.push_back(MakeMapMarkerFastPathTestMarker(
                    0x800000 + index, 0x900000 + index, 1, false, 0, 0, 0, capNow));
        }
        markerState[0] = 1;
        record.visibilityState = markerState.data();
        ResetMapMarkerFastPathTestCounters();
        TryPublishMapMarkerVisibilityRecord(record);
        CheckPerfFilter(::g_testMapMarkerCacheLockCount == 2 &&
            ::g_testMapMarkerIdentitySearchCount == 2 && ::g_testMapMarkerResolverCallCount == 1,
            "unresolved full-cap capture retains the resolver and reconciliation path");
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.size() == VISIBLE_MAP_MARKER_MAX_ENTRIES,
                "marker capacity remains capped at 2048 entries");
        }

        ClearMapMarkerFastPathTestCache(GetTickCount(), GetTickCount());
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            g_visibleMapMarkers.push_back(positioned);
        }
        ResetWorldSessionFromLog();
        {
            std::lock_guard<std::mutex> lock(g_visibleMapMarkerMutex);
            CheckPerfFilter(g_visibleMapMarkers.empty(), "session reset clears visible map markers");
        }
        MutateMinimapIconAtlasForTest([&](MinimapIconAtlas& atlas) {
            atlas = std::move(savedIconAtlas);
        });

        g_debugLoggingEnabled = savedDebugLogging;
        g_modContext = savedModContext;
        return g_perfFilterTestFailures == 0 ? 0 : 1;
    }

    std::atomic<bool> g_testAllocationTracking{ false };
    std::atomic<std::size_t> g_testAllocationFailureTriggered{ 0 };
    std::atomic<std::size_t> g_testAllocationFailureAt{ 0 };
    std::atomic<std::size_t> g_testAllocationCount{ 0 };
}

void* operator new(std::size_t size)
{
    if (g_testAllocationTracking.load(std::memory_order_relaxed))
    {
        const auto allocation = g_testAllocationCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (allocation == g_testAllocationFailureAt.load(std::memory_order_relaxed))
        {
            g_testAllocationFailureTriggered.store(1, std::memory_order_relaxed);
            throw std::bad_alloc();
        }
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
        std::cout << "PASS: " << message << '\n';
    }
    template<class T> void Put(void* data, std::size_t offset, T value)
    { std::memcpy(static_cast<std::uint8_t*>(data) + offset, &value, sizeof(value)); }
    void CheckMapAppearance()
    {
        RefreshMinimapConfig(nullptr);
        Check(g_defaultMapStyle, "HD Default is selected when unset");
        ModContext config{};
        for (const auto style : {"default", " BRIGHT ", "unknown", "original"}) {
            static std::string selected;
            selected = style;
            config.config.GetString=[](const char*,const char* key,std::string fallback) {
                return !std::strcmp(key,"map_style") ? selected : fallback;
            };
            RefreshMinimapConfig(&config);
            Check(g_defaultMapStyle == (selected != "original"), "Default, legacy Bright and Original settings");
        }
        RealMapTexture original{}, hd{};
        Check(TryLoadRealMapFromPath("assets/embervale_realmap_1280.rgba",original) &&
            TryLoadDefaultMap(original,hd) && hd.width==2048, "HD replacement loads independently of original resolution");
        g_realMap=original; g_defaultMap=hd; g_defaultMapStyle=true;
        Check(&ActiveRealMapLocked()==&g_defaultMap, "Default selects HD texture");
        g_defaultMapStyle=false;
        Check(&ActiveRealMapLocked()==&g_realMap, "Original selects original texture");
        g_defaultMapStyle=true; g_defaultMap.rgba.pop_back();
        Check(&ActiveRealMapLocked()==&g_realMap, "Malformed HD texture falls back to Original");
        original.path="missing-map-test/original.rgba";
        Check(!TryLoadDefaultMap(original,hd) && !hd.loaded, "Missing HD asset clears cache");
        g_defaultMap={};
        Check(&ActiveRealMapLocked()==&g_realMap, "Missing HD map keeps Original usable");
        g_realMap={}; g_defaultMap={}; RefreshMinimapConfig(nullptr);
    }

    std::int64_t Fixed(int value) { return static_cast<std::int64_t>(value) * 4294967296LL; }
    std::vector<std::uint8_t> state(DEATH_MAP_ARRAY_OFFSET + 32);
    std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE * 5> world{};
    std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE> custom{};
    void Entry(void* data, std::size_t index, int x, int z, std::uint32_t key)
    {
        const auto offset = index * WORLD_MAP_MARKER_STRIDE;
        Put(data, offset + 0x10, Fixed(x)); Put(data, offset + 0x18, Fixed(200));
        Put(data, offset + 0x20, Fixed(z)); Put(data, offset + 0x30, key);
    }
    void CurrentEntry(void* data, int x, int z, std::uint32_t key)
    {
        Put(data, 0x10, Fixed(x)); Put(data, 0x18, Fixed(200));
        Put(data, 0x20, Fixed(z)); Put(data, 0x30, key);
    }
    void PutCurrentList(std::vector<std::uint8_t>& state, std::size_t offset,
        void* entries, std::uint64_t count)
    {
        Put(state.data(), offset, reinterpret_cast<uintptr_t>(entries));
        Put(state.data(), offset + 8, count);
    }

    bool SameCapturedWorldMapMarkers(const std::vector<CapturedWorldMapMarker>& left,
        const std::vector<CapturedWorldMapMarker>& right)
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (left[index].x != right[index].x || left[index].y != right[index].y ||
                left[index].z != right[index].z || left[index].key != right[index].key ||
                left[index].custom != right[index].custom)
            {
                return false;
            }
        }
        return true;
    }

#if ESM_NATIVE_FILTER_PROBE
    std::array<NativeFilterProbe::ArrayHeader, 4> g_testNativeFilterProbeHeaders{};
    std::array<NativeFilterProbe::FilterEntryBytes, NativeFilterProbe::kMaxFilterEntries>
        g_testNativeFilterProbeEntries{};
    std::uint8_t g_testNativeFilterProbeEntryCount = 2;
    bool g_testNativeFilterProbeChangeCountAfter = false;
    bool g_testNativeFilterProbeMismatchPrimaryPointer = false;

    void SetNativeFilterProbeTestEntry(std::size_t index, std::uint32_t id, std::uint8_t enabled)
    {
        auto& entry = g_testNativeFilterProbeEntries[index];
        entry.bytes.fill(0);
        entry.bytes[0] = static_cast<std::uint8_t>(id & 0xffu);
        entry.bytes[1] = static_cast<std::uint8_t>((id >> 8) & 0xffu);
        entry.bytes[2] = static_cast<std::uint8_t>((id >> 16) & 0xffu);
        entry.bytes[3] = static_cast<std::uint8_t>((id >> 24) & 0xffu);
        entry.bytes[5] = enabled;
    }

    bool ReadNativeFilterProbeTestSnapshot(std::uintptr_t queryState, bool after,
        NativeFilterProbe::ReadSnapshot& snapshot)
    {
        snapshot.root.readable = true;
        snapshot.root.links = { 0x1000, 0x2000, 0x3000, queryState };
        snapshot.revisionsReadable = true;
        snapshot.revision36E9F8 = 13;
        snapshot.revision36E9FC = 17;
        snapshot.arrayHeadersReadable = true;
        snapshot.arrays = g_testNativeFilterProbeHeaders;
        if (g_testNativeFilterProbeMismatchPrimaryPointer)
            snapshot.arrays[static_cast<std::size_t>(NativeFilterProbe::ArrayKind::Primary)].pointer ^= 0x1000;
        snapshot.filterCountReadable = true;
        snapshot.filterCount = g_testNativeFilterProbeEntryCount;
        if (after && g_testNativeFilterProbeChangeCountAfter && snapshot.filterCount > 0)
            --snapshot.filterCount;
        snapshot.filterEntriesReadable = true;
        snapshot.filterTable = g_testNativeFilterProbeEntries;
        return true;
    }

#endif

    std::uintptr_t g_testNativeFilterProbeWorldState = 0;
    void __fastcall InitNativeFilterProbeMap(void*, void* record, std::uint32_t size)
    {
        if (size != 16)
            std::exit(2);
        static_cast<std::uintptr_t*>(record)[1] = g_testNativeFilterProbeWorldState;
    }

    int CheckNativeFilterProbeCallbackFocused()
    {
        const bool savedClient10 = g_client10;
        const auto savedProfile = g_clientProfile;
        const bool savedWorldLayout = g_worldMapLayoutSupported.load();
        const bool savedDeathLayout = g_deathMarkerLayoutSupported.load();
        const bool savedDeathVisibility = g_showDeathMarkers.load();
        const auto savedIterator = g_iterInit;
        const DWORD savedLastCaptureTick = g_lastWorldMapCaptureTick;
        std::vector<CapturedWorldMapMarker> savedWorld, savedDeaths;
        bool savedValid = false;
        DWORD savedWorldTick = 0;
        {
            std::lock_guard<std::mutex> lock(g_worldMapMutex);
            savedWorld = g_worldMapMarkers;
            savedDeaths = g_deathMarkers;
            savedValid = g_worldMapValid;
            savedWorldTick = g_worldMapTick;
        }

        constexpr std::size_t stateSize = 0x3E6A78 + 16;
        std::vector<std::uint8_t> probeState(stateSize);
        std::array<std::uint8_t, 0xA0 * 2> primary{};
        std::array<std::uint8_t, 0xA0> custom{};
        std::array<std::uint8_t, 0xA0> secondary{};
        std::array<std::uint8_t, 0xA0> deaths{};
        CurrentEntry(primary.data(), 900, 1000, 101);
        Put(primary.data(), 0x34, std::uint8_t{ 0 });
        CurrentEntry(primary.data() + 0xA0, -10000, 1000, 202);
        Put(primary.data(), 0xA0 + 0x34, std::uint8_t{ 1 });
        CurrentEntry(custom.data(), 1000, 1000, 404);
        CurrentEntry(secondary.data(), 1100, 1000, 303);
        Put(secondary.data(), 0x08, std::uint32_t{ 0x44556677 });
        CurrentEntry(deaths.data(), 1200, 1000, 505);

        PutCurrentList(probeState, 0x36EA00, primary.data(), 2);
        PutCurrentList(probeState, 0x396A28, custom.data(), 1);
        PutCurrentList(probeState, 0x3BEA50, secondary.data(), 1);
        PutCurrentList(probeState, 0x3E6A78, deaths.data(), 1);

        g_client10 = true;
        g_clientProfile = MinimapClient10::Profile::Build26082781;
        g_worldMapLayoutSupported = true;
        g_deathMarkerLayoutSupported = true;
        g_showDeathMarkers = true;
        g_iterInit = InitNativeFilterProbeMap;
        g_testNativeFilterProbeWorldState = reinterpret_cast<std::uintptr_t>(probeState.data());
        g_testNativeFilterProbeCaptureCount = 0;
        g_testNativeFilterProbeSnapshotReadCount = 0;
#if ESM_NATIVE_FILTER_PROBE
        const auto savedSnapshotReader = g_testNativeFilterProbeSnapshotReader;
        const bool savedGuardOverrideSet = g_testNativeFilterProbeGuardOverrideSet;
        const bool savedGuardOverride = g_testNativeFilterProbeGuardOverride;
        const auto savedTestStatus = g_testNativeFilterProbeLastStatus;
        const auto savedTestSummary = g_testNativeFilterProbeLastSummary;
        g_testNativeFilterProbeHeaders = {
            NativeFilterProbe::ArrayHeader{ reinterpret_cast<std::uintptr_t>(primary.data()), 2 },
            NativeFilterProbe::ArrayHeader{ reinterpret_cast<std::uintptr_t>(custom.data()), 1 },
            NativeFilterProbe::ArrayHeader{ reinterpret_cast<std::uintptr_t>(secondary.data()), 1 },
            NativeFilterProbe::ArrayHeader{ reinterpret_cast<std::uintptr_t>(deaths.data()), 1 }
        };
        g_testNativeFilterProbeEntries = {};
        SetNativeFilterProbeTestEntry(0, 101, 0);
        SetNativeFilterProbeTestEntry(1, 303, 1);
        g_testNativeFilterProbeEntryCount = 2;
        g_testNativeFilterProbeChangeCountAfter = false;
        g_testNativeFilterProbeSnapshotReader = ReadNativeFilterProbeTestSnapshot;
        SetNativeFilterProbeTestGuard(false);
#endif

        const auto runHook = [&]() {
            g_lastWorldMapCaptureTick = GetTickCount() - 100;
            std::uintptr_t context[2] = { 0x1234, 0x5678 };
            CaptureWorldMapHook(context, nullptr, nullptr, nullptr);
        };
        runHook();
        const DWORD firstTick = GetTickCount();
        const auto baselineMarkers = CopyWorldMapMarkers(firstTick);
        const auto baselineDeaths = CopyDeathMarkers(firstTick);
        Check(g_worldMapValid && baselineMarkers.size() == 3 && baselineDeaths.size() == 1,
            "callback fixture produces fresh baseline POI and death snapshots");
        Check(baselineMarkers.size() == 3 &&
            std::count_if(baselineMarkers.begin(), baselineMarkers.end(), [](const auto& marker) {
                return marker.key == 202;
            }) == 0,
            "out-of-bounds primary row stays absent from the normal marker vector");

#if ESM_NATIVE_FILTER_PROBE
        Check(g_testNativeFilterProbeCaptureCount == 0 && g_testNativeFilterProbeSnapshotReadCount == 0,
            "failed optional code guard performs no probe capture or snapshot reads");
        SetNativeFilterProbeTestGuard(true);
        g_testNativeFilterProbeChangeCountAfter = true;
        runHook();
        const DWORD rejectedTick = GetTickCount();
        const auto rejectedMarkers = CopyWorldMapMarkers(rejectedTick);
        const auto rejectedDeaths = CopyDeathMarkers(rejectedTick);
        Check(g_testNativeFilterProbeLastStatus == NativeFilterProbe::ValidationStatus::Unstable,
            "callback probe rejects a changed inline table snapshot");
        Check(g_testNativeFilterProbeLastSummary.sequence > 0 &&
            g_testNativeFilterProbeLastSummary.sessionGeneration == g_worldSessionGeneration.load() &&
            g_testNativeFilterProbeLastSummary.context == NativeFilterProbe::ContextState::Unknown,
            "rejected callback sample retains sequence, session and unknown context for diagnostics");
        Check(g_testNativeFilterProbeCaptureCount == 1 &&
            g_testNativeFilterProbeSnapshotReadCount == 2,
            "opt-in callback reads only the bounded before and after snapshots");
        Check(SameCapturedWorldMapMarkers(baselineMarkers, rejectedMarkers) &&
            SameCapturedWorldMapMarkers(baselineDeaths, rejectedDeaths) && g_worldMapValid &&
            rejectedTick - g_worldMapTick <= MinimapLive::PlayerStaleMs,
            "rejected probe preserves normal marker vectors and publishes a fresh snapshot");

        g_testNativeFilterProbeChangeCountAfter = false;
        g_testNativeFilterProbeMismatchPrimaryPointer = true;
        runHook();
        const DWORD headerRejectedTick = GetTickCount();
        const auto headerRejectedMarkers = CopyWorldMapMarkers(headerRejectedTick);
        const auto headerRejectedDeaths = CopyDeathMarkers(headerRejectedTick);
        Check(g_testNativeFilterProbeLastStatus == NativeFilterProbe::ValidationStatus::Unstable &&
            g_testNativeFilterProbeLastSummary.filterEntryCount == 0,
            "callback probe rejects a stable snapshot header that differs from the actual row reader");
        Check(SameCapturedWorldMapMarkers(baselineMarkers, headerRejectedMarkers) &&
            SameCapturedWorldMapMarkers(baselineDeaths, headerRejectedDeaths) && g_worldMapValid &&
            headerRejectedTick - g_worldMapTick <= MinimapLive::PlayerStaleMs,
            "header mismatch rejection leaves fresh normal marker and death snapshots unchanged");

        g_testNativeFilterProbeMismatchPrimaryPointer = false;
        runHook();
        Check(g_testNativeFilterProbeLastStatus == NativeFilterProbe::ValidationStatus::StableTable &&
            g_testNativeFilterProbeLastSummary.filters[0].id == 101 &&
            !g_testNativeFilterProbeLastSummary.filters[0].enabled &&
            g_testNativeFilterProbeLastSummary.filters[0].primaryMatches == 1 &&
            g_testNativeFilterProbeLastSummary.filters[1].id == 303 &&
            g_testNativeFilterProbeLastSummary.filters[1].enabled &&
            g_testNativeFilterProbeLastSummary.filters[1].secondaryMatches == 1 &&
            g_testNativeFilterProbeLastSummary.unresolvedSpecialPrimary == 1 &&
            g_testNativeFilterProbeLastSummary.context == NativeFilterProbe::ContextState::Unknown,
            "stable callback summary reports bounded category layers and special rows as unresolved");
        Check(g_testNativeFilterProbeLastSummary.sequence > 0 &&
            g_testNativeFilterProbeLastSummary.sessionGeneration == g_worldSessionGeneration.load(),
            "callback summary retains sample sequence and current session identity");

        g_testNativeFilterProbeSnapshotReader = savedSnapshotReader;
        g_testNativeFilterProbeGuardOverrideSet = savedGuardOverrideSet;
        g_testNativeFilterProbeGuardOverride = savedGuardOverride;
        g_testNativeFilterProbeLastStatus = savedTestStatus;
        g_testNativeFilterProbeLastSummary = savedTestSummary;
#else
        Check(g_testNativeFilterProbeCaptureCount == 0 &&
            g_testNativeFilterProbeSnapshotReadCount == 0,
            "default-off callback performs no filter probe capture or metadata reads");
#endif

        {
            std::lock_guard<std::mutex> lock(g_worldMapMutex);
            g_worldMapMarkers = std::move(savedWorld);
            g_deathMarkers = std::move(savedDeaths);
            g_worldMapValid = savedValid;
            g_worldMapTick = savedWorldTick;
        }
        g_client10 = savedClient10;
        g_clientProfile = savedProfile;
        g_worldMapLayoutSupported = savedWorldLayout;
        g_deathMarkerLayoutSupported = savedDeathLayout;
        g_showDeathMarkers = savedDeathVisibility;
        g_iterInit = savedIterator;
        g_lastWorldMapCaptureTick = savedLastCaptureTick;
        return 0;
    }

    void CheckLatestProfileMarkerRouting()
    {
        const bool savedClient10 = g_client10;
        const auto savedProfile = g_clientProfile;
        const bool savedDeathLayout = g_deathMarkerLayoutSupported.load();
        const bool savedDeathVisibility = g_showDeathMarkers.load();
        std::vector<CapturedWorldMapMarker> savedWorld, savedDeaths;
        bool savedValid = false;
        DWORD savedTick = 0;
        {
            std::lock_guard<std::mutex> lock(g_worldMapMutex);
            savedWorld = g_worldMapMarkers;
            savedDeaths = g_deathMarkers;
            savedValid = g_worldMapValid;
            savedTick = g_worldMapTick;
        }

        constexpr std::size_t latestStateSize = 0x3E6A78 + 16;
        std::vector<std::uint8_t> state(latestStateSize);
        std::array<std::uint8_t, 0xA0> base{}, custom{}, secondary{}, tombstone{};
        constexpr std::uint32_t sharedRasterKey = 0xCAFEBABE;
        CurrentEntry(base.data(), 900, 1000, sharedRasterKey);
        CurrentEntry(custom.data(), 1000, 1000, sharedRasterKey);
        CurrentEntry(secondary.data(), 1100, 1000, sharedRasterKey);
        CurrentEntry(tombstone.data(), 1200, 1000, sharedRasterKey);
        PutCurrentList(state, 0x36EA00, base.data(), 1);
        PutCurrentList(state, 0x396A28, custom.data(), 1);
        PutCurrentList(state, 0x3BEA50, secondary.data(), 1);
        PutCurrentList(state, 0x3E6A78, tombstone.data(), 1);

        g_client10 = true;
        g_clientProfile = MinimapClient10::Profile::Build26082781;
        g_deathMarkerLayoutSupported = true;
        g_showDeathMarkers = true;
        constexpr DWORD now = 1000;
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        auto worldMarkers = CopyWorldMapMarkers(now);
        auto deathMarkers = CopyDeathMarkers(now);
        Check(worldMarkers.size() == 3,
            "latest profile routes base, custom and secondary marker lists into ordinary POIs");
        Check(std::count_if(worldMarkers.begin(), worldMarkers.end(), [](const auto& marker) { return marker.custom; }) == 1,
            "latest profile preserves custom ownership while routing the secondary list");
        Check(std::count_if(worldMarkers.begin(), worldMarkers.end(), [&](const auto& marker) {
            return marker.key == sharedRasterKey;
        }) == 3, "identical raster keys do not change source-list classification");
        Check(deathMarkers.size() == 1 && deathMarkers[0].x == Fixed(1200) &&
            deathMarkers[0].key == sharedRasterKey,
            "latest profile reads tombstones only from the dedicated tombstone list");

        PutCurrentList(state, 0x3BEA50, reinterpret_cast<void*>(uintptr_t{1}), 1);
        PutCurrentList(state, 0x3E6A78, tombstone.data(), 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        worldMarkers = CopyWorldMapMarkers(now);
        deathMarkers = CopyDeathMarkers(now);
        Check(worldMarkers.size() == 2 && deathMarkers.size() == 1,
            "unreadable secondary POIs do not erase valid base and custom markers");

        PutCurrentList(state, 0x3BEA50, secondary.data(), DEATH_MAP_MAX_ENTRIES + 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        Check(CopyWorldMapMarkers(now).size() == 2,
            "secondary POI input keeps the existing bounded marker cap");

        PutCurrentList(state, 0x3BEA50, secondary.data(), 1);
        PutCurrentList(state, 0x3E6A78, reinterpret_cast<void*>(uintptr_t{1}), 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        worldMarkers = CopyWorldMapMarkers(now);
        deathMarkers = CopyDeathMarkers(now);
        Check(worldMarkers.size() == 3 && deathMarkers.empty(),
            "bad tombstone input clears only deaths and preserves ordinary POIs");

        PutCurrentList(state, 0x3E6A78, tombstone.data(), DEATH_MAP_MAX_ENTRIES + 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 3,
            "oversized tombstones clear only deaths and preserve ordinary POIs");

        g_deathMarkerLayoutSupported = false;
        PutCurrentList(state, 0x3BEA50, secondary.data(), 1);
        PutCurrentList(state, 0x3E6A78, tombstone.data(), 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        Check(CopyWorldMapMarkers(now).size() == 2 && CopyDeathMarkers(now).empty(),
            "missing layout guard excludes both new collections while base POIs remain available");

        g_deathMarkerLayoutSupported = true;
        g_clientProfile = MinimapClient10::Profile::Build25988036;
        PutCurrentList(state, 0x3BEA50, secondary.data(), 1);
        PutCurrentList(state, 0x3E6A78, tombstone.data(), 1);
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
        worldMarkers = CopyWorldMapMarkers(now);
        deathMarkers = CopyDeathMarkers(now);
        Check(worldMarkers.size() == 2 && deathMarkers.size() == 1 &&
            deathMarkers[0].x == Fixed(1100),
            "previous 1.0 profile retains its original secondary-list death route");

        std::vector<std::uint8_t> legacyState(DEATH_MAP_ARRAY_OFFSET + 16);
        std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE> legacyBase{}, legacyCustom{}, legacyDeath{};
        Entry(legacyBase.data(), 0, 900, 1000, sharedRasterKey);
        Entry(legacyCustom.data(), 0, 1000, 1000, sharedRasterKey);
        Entry(legacyDeath.data(), 0, 1200, 1000, sharedRasterKey);
        PutCurrentList(legacyState, WORLD_MAP_ARRAY_OFFSET, legacyBase.data(), 1);
        PutCurrentList(legacyState, CUSTOM_MAP_ARRAY_OFFSET, legacyCustom.data(), 1);
        PutCurrentList(legacyState, DEATH_MAP_ARRAY_OFFSET, legacyDeath.data(), 1);
        g_client10 = false;
        g_clientProfile = MinimapClient10::Profile::Build25988036;
        CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(legacyState.data()), now);
        worldMarkers = CopyWorldMapMarkers(now);
        deathMarkers = CopyDeathMarkers(now);
        Check(worldMarkers.size() == 2 && deathMarkers.size() == 1 &&
            deathMarkers[0].x == Fixed(1200),
            "legacy profile retains its 0x7C178 death-list behavior");

        {
            std::lock_guard<std::mutex> lock(g_worldMapMutex);
            g_worldMapMarkers = std::move(savedWorld);
            g_deathMarkers = std::move(savedDeaths);
            g_worldMapValid = savedValid;
            g_worldMapTick = savedTick;
        }
        g_client10 = savedClient10;
        g_clientProfile = savedProfile;
        g_deathMarkerLayoutSupported = savedDeathLayout;
        g_showDeathMarkers = savedDeathVisibility;
    }

    std::vector<std::string> haloOutcomeLogs;
    int haloObservationDraws = 0;
    int haloObservationLegacyDraws = 0;

    std::vector<std::string> HaloOutcomeLines()
    {
        std::vector<std::string> result;
        for (const auto& line : haloOutcomeLogs)
            if (line.find("[Minimap] local-player halo ") != std::string::npos)
                result.push_back(line);
        return result;
    }

    int haloThrowingLoggerCalls = 0;
    bool haloThrowNextLoggerCall = false;

    void ThrowingHaloStatusLogger(const char* message)
    {
        ++haloThrowingLoggerCalls;
        if (haloThrowNextLoggerCall)
        {
            haloThrowNextLoggerCall = false;
            throw std::runtime_error("forced halo logger callback failure");
        }
        haloOutcomeLogs.emplace_back(message);
    }

    void PrepareDirectHaloRenderer(bool enabled, bool unavailable, bool pending)
    {
        g_renderer = VulkanMinimapRenderer{};
        g_renderer.width = 512;
        g_renderer.height = 320;
        g_renderer.widgetScale = 1.0f;
        g_renderer.widgetCenterX = 256;
        g_renderer.widgetCenterY = 160;
        g_renderer.arrowGlowFrameEligible = true;
        g_renderer.arrowGlowUnavailable = unavailable;
        auto& resources = g_renderer.arrowGlowResources;
        resources.frameTextureReady = true;
        resources.frameTextureUploadPending = pending;
        resources.framePipeline = 0x301;
        resources.framePipelineLayout = 0x302;
        resources.frameDescriptorSet = 0x303;
        auto& fns = g_renderer.fns;
        fns.cmdClearAttachments = [](void*, std::uint32_t, const VkClearAttachment*, std::uint32_t, const VkClearRect*) {
            ++haloObservationLegacyDraws;
        };
        fns.cmdBindPipeline = [](void*, std::uint32_t, void*) {};
        fns.cmdBindDescriptorSets = [](void*, std::uint32_t, void*, std::uint32_t, std::uint32_t,
            void* const*, std::uint32_t, const std::uint32_t*) {};
        fns.cmdPushConstants = [](void*, void*, std::uint32_t, std::uint32_t, std::uint32_t, const void*) {};
        fns.cmdDraw = [](void*, std::uint32_t vertexCount, std::uint32_t instanceCount, std::uint32_t, std::uint32_t) {
            if (vertexCount == 6 && instanceCount == 1)
                ++haloObservationDraws;
        };
        g_playerArrow = {};
        g_playerArrow.attempted = true;
        g_playerArrow.loaded = true;
        g_playerArrow.width = 4;
        g_playerArrow.height = 4;
        g_playerArrow.rgba.assign(4 * 4 * 4, 255);
        g_playerArrowGlowEnabled = enabled;
        g_playerArrowGlowStyle = static_cast<int>(MinimapArrowGlow::Style::SameColour);
    }

    bool DrawDirectLocalArrow()
    {
        return TryDrawPremiumPlayerArrow(g_renderer, reinterpret_cast<void*>(uintptr_t{0x100}),
            256, 160, 80, 0.25f);
    }

    std::size_t MeasureHaloObserverAllocations()
    {
        ModContext context{};
        context.Log = [](const char*) {};
        const auto savedContext = g_modContext;
        g_modContext = &context;
        std::size_t allocationCount = 0;
        for (int warmup = 0; warmup < 2; ++warmup)
        {
            PrepareDirectHaloRenderer(true, false, false);
            g_testAllocationFailureAt = 0;
            g_testAllocationCount = 0;
            g_testAllocationTracking = true;
            const bool drawn = DrawDirectLocalArrow();
            g_testAllocationTracking = false;
            if (!drawn)
                return 0;
            allocationCount = g_testAllocationCount.load(std::memory_order_relaxed);
            DestroyVulkanMinimapRendererLocked();
        }
        g_modContext = savedContext;
        return allocationCount;
    }

    void CheckPlayerArrowHaloAllocationFailurePassivity()
    {
        const auto savedContext = g_modContext;
        auto savedRenderer = std::move(g_renderer);
        auto savedArrow = g_playerArrow;
        const bool savedEnabled = g_playerArrowGlowEnabled.load();
        const int savedStyle = g_playerArrowGlowStyle.load();
        ModContext context{};
        context.Log = [](const char*) {};
        g_modContext = &context;

        const auto allocations = MeasureHaloObserverAllocations();
        Check(allocations > 0, "halo status formatting performs an injectable allocation");
        PrepareDirectHaloRenderer(true, false, false);
        haloOutcomeLogs.clear();
        haloObservationDraws = haloObservationLegacyDraws = 0;
        const int bodyBeforeFailure = haloObservationLegacyDraws;
        g_testAllocationCount = 0;
        g_testAllocationFailureAt = allocations;
        g_testAllocationFailureTriggered = 0;
        g_testAllocationTracking = true;
        bool drawReturned = false;
        bool exceptionEscaped = false;
        try
        {
            drawReturned = DrawDirectLocalArrow();
        }
        catch (...)
        {
            exceptionEscaped = true;
        }
        g_testAllocationTracking = false;
        g_testAllocationFailureAt = 0;
        Check(g_testAllocationFailureTriggered.load(std::memory_order_relaxed) == 1,
            "the test injects bad_alloc during optional halo status formatting");
        Check(!exceptionEscaped && drawReturned && haloObservationDraws == 1 &&
            haloObservationLegacyDraws > bodyBeforeFailure,
            "halo status allocation failure does not interrupt command recording or the legacy arrow body");

        g_testAllocationCount = 0;
        g_testAllocationTracking = true;
        const bool secondDrawReturned = DrawDirectLocalArrow();
        g_testAllocationTracking = false;
        const auto secondFrameAllocations = g_testAllocationCount.load(std::memory_order_relaxed);
        Check(secondDrawReturned && haloObservationDraws == 2 && secondFrameAllocations == 0,
            "a failed initial status attempt is not reformatted or retried on later frames");

        g_renderer = std::move(savedRenderer);
        g_playerArrow = std::move(savedArrow);
        g_playerArrowGlowEnabled = savedEnabled;
        g_playerArrowGlowStyle = savedStyle;
        g_modContext = savedContext;
    }

    void CheckPlayerArrowHaloLoggerFailurePassivity()
    {
        const auto savedContext = g_modContext;
        auto savedRenderer = std::move(g_renderer);
        auto savedArrow = g_playerArrow;
        const bool savedEnabled = g_playerArrowGlowEnabled.load();
        const int savedStyle = g_playerArrowGlowStyle.load();
        ModContext context{};
        context.Log = ThrowingHaloStatusLogger;
        g_modContext = &context;
        haloOutcomeLogs.clear();
        haloObservationDraws = haloObservationLegacyDraws = 0;
        haloThrowingLoggerCalls = 0;
        haloThrowNextLoggerCall = true;

        PrepareDirectHaloRenderer(true, false, true);
        const int bodyBeforeFailure = haloObservationLegacyDraws;
        bool drawReturned = false;
        bool exceptionEscaped = false;
        try
        {
            drawReturned = DrawDirectLocalArrow();
        }
        catch (...)
        {
            exceptionEscaped = true;
        }
        Check(!exceptionEscaped && drawReturned && haloObservationDraws == 0 &&
            haloObservationLegacyDraws > bodyBeforeFailure && haloThrowingLoggerCalls == 1,
            "logger callback exception does not interrupt a pending-halo fallback draw");

        const int bodyBeforeSuccess = haloObservationLegacyDraws;
        g_renderer.arrowGlowResources.frameTextureUploadPending = false;
        Check(DrawDirectLocalArrow() && haloObservationDraws == 1 &&
            haloObservationLegacyDraws > bodyBeforeSuccess && haloThrowingLoggerCalls == 2 &&
            HaloOutcomeLines().size() == 1 && HaloOutcomeLines()[0].find("first-success") != std::string::npos,
            "one first-success status follows a failed initial log without retrying the initial attempt");
        Check(DrawDirectLocalArrow() && haloObservationDraws == 2 && haloThrowingLoggerCalls == 2 &&
            HaloOutcomeLines().size() == 1,
            "a failed initial callback and one later success stay within the two-line bound");

        DestroyVulkanMinimapRendererLocked();
        haloOutcomeLogs.clear();
        PrepareDirectHaloRenderer(true, false, false);
        const int bodyBeforeReset = haloObservationLegacyDraws;
        Check(DrawDirectLocalArrow() && haloObservationDraws == 3 &&
            haloObservationLegacyDraws > bodyBeforeReset && haloThrowingLoggerCalls == 3 &&
            HaloOutcomeLines().size() == 1 && HaloOutcomeLines()[0].find("initial") != std::string::npos,
            "renderer reset permits one fresh initial status attempt after a logger exception");
        Check(DrawDirectLocalArrow() && haloObservationDraws == 4 && haloThrowingLoggerCalls == 3 &&
            HaloOutcomeLines().size() == 1,
            "successful frames after reset do not retry completed status reporting");

        g_renderer = std::move(savedRenderer);
        g_playerArrow = std::move(savedArrow);
        g_playerArrowGlowEnabled = savedEnabled;
        g_playerArrowGlowStyle = savedStyle;
        g_modContext = savedContext;
    }

    void CheckPlayerArrowHaloObservation()
    {
        const auto savedContext = g_modContext;
        auto savedRenderer = std::move(g_renderer);
        auto savedArrow = g_playerArrow;
        const bool savedEnabled = g_playerArrowGlowEnabled.load();
        const int savedStyle = g_playerArrowGlowStyle.load();
        ModContext context{};
        context.Log = [](const char* message) { haloOutcomeLogs.emplace_back(message); };
        g_modContext = &context;
        haloOutcomeLogs.clear();
        haloObservationDraws = haloObservationLegacyDraws = 0;

        PrepareDirectHaloRenderer(true, false, true);
        Check(DrawDirectLocalArrow() && haloObservationLegacyDraws > 0 && haloObservationDraws == 0,
            "pending halo attempt preserves the legacy local arrow draw");
        auto lines = HaloOutcomeLines();
        Check(lines.size() == 1 && lines[0].find("initial") != std::string::npos &&
            lines[0].find("enabled=1") != std::string::npos && lines[0].find("eligible=1") != std::string::npos &&
            lines[0].find("unavailable=0") != std::string::npos && lines[0].find("ready=1") != std::string::npos &&
            lines[0].find("pending=1") != std::string::npos && lines[0].find("handles_geometry=1") != std::string::npos &&
            lines[0].find("recorded=0") != std::string::npos &&
            lines[0].size() < 240 && lines[0].find("0x") == std::string::npos,
            "initial halo outcome reports bounded effective state and pending result without identifiers");

        const int bodyBeforeSuccess = haloObservationLegacyDraws;
        g_renderer.arrowGlowResources.frameTextureUploadPending = false;
        Check(DrawDirectLocalArrow() && haloObservationLegacyDraws > bodyBeforeSuccess && haloObservationDraws == 1,
            "first successful halo attempt keeps the legacy arrow and records its GPU command");
        lines = HaloOutcomeLines();
        Check(lines.size() == 2 && lines[1].find("first-success") != std::string::npos &&
            lines[1].find("handles_geometry=1") != std::string::npos && lines[1].find("recorded=1") != std::string::npos,
            "one first-success line reports complete handles and geometry readiness");
        Check(DrawDirectLocalArrow() && haloObservationDraws == 2 && HaloOutcomeLines().size() == 2,
            "later successful frames draw normally without repeating halo status logging");

        DestroyVulkanMinimapRendererLocked();
        haloOutcomeLogs.clear();
        PrepareDirectHaloRenderer(false, false, false);
        Check(DrawDirectLocalArrow() && haloObservationLegacyDraws > 0 && haloObservationDraws == 2,
            "explicitly disabled halo leaves the legacy arrow active");
        lines = HaloOutcomeLines();
        Check(lines.size() == 1 && lines[0].find("initial") != std::string::npos &&
            lines[0].find("enabled=0") != std::string::npos && lines[0].find("recorded=0") != std::string::npos,
            "disabled initial attempt is reported once without recording a halo");

        DestroyVulkanMinimapRendererLocked();
        haloOutcomeLogs.clear();
        PrepareDirectHaloRenderer(true, true, false);
        const int bodyBeforeUnavailable = haloObservationLegacyDraws;
        Check(DrawDirectLocalArrow() && haloObservationLegacyDraws > bodyBeforeUnavailable && haloObservationDraws == 2,
            "unavailable halo keeps the legacy local arrow active");
        lines = HaloOutcomeLines();
        Check(lines.size() == 1 && lines[0].find("unavailable=1") != std::string::npos &&
            lines[0].find("recorded=0") != std::string::npos,
            "unavailable initial attempt is reported once without changing fallback");

        DestroyVulkanMinimapRendererLocked();
        haloOutcomeLogs.clear();
        PrepareDirectHaloRenderer(true, false, false);
        g_renderer.width = 0;
        Check(DrawDirectLocalArrow() && haloObservationDraws == 3,
            "incomplete halo viewport geometry preserves the existing command recording and local arrow result");
        lines = HaloOutcomeLines();
        Check(lines.size() == 1 && lines[0].find("handles_geometry=0") != std::string::npos &&
            lines[0].find("recorded=1") != std::string::npos,
            "combined readiness status stays distinct from the actual halo command result");

        DestroyVulkanMinimapRendererLocked();
        haloOutcomeLogs.clear();
        PrepareDirectHaloRenderer(true, false, false);
        const int bodyBeforeReset = haloObservationLegacyDraws;
        Check(DrawDirectLocalArrow() && haloObservationLegacyDraws > bodyBeforeReset && haloObservationDraws == 4,
            "renderer reset restores a fresh initial observation while drawing continues");
        Check(HaloOutcomeLines().size() == 1 && HaloOutcomeLines()[0].find("initial") != std::string::npos &&
            HaloOutcomeLines()[0].find("recorded=1") != std::string::npos,
            "initial successful attempt does not emit a separate first-success line");
        Check(DrawDirectLocalArrow() && haloObservationDraws == 5 && HaloOutcomeLines().size() == 1,
            "successful post-reset frames retain drawing with one bounded status line");

        g_renderer = std::move(savedRenderer);
        g_playerArrow = std::move(savedArrow);
        g_playerArrowGlowEnabled = savedEnabled;
        g_playerArrowGlowStyle = savedStyle;
        g_modContext = savedContext;
    }

    void __fastcall InitMap(void* ctx, void* record, std::uint32_t size)
    {
        if (size != 16) std::exit(2);
        static_cast<uintptr_t*>(ctx)[1] = 999;
        static_cast<uintptr_t*>(record)[1] = reinterpret_cast<uintptr_t>(state.data());
    }
    struct Pixel { float r = 0, g = 0, b = 0; } pixels[320][512];
    int clears = 0;
    void CheckToggleNotices()
    {
        ToggleNoticeState notice;
        bool on = false;
        Check(!notice.read(100, true, on), "no minimap notice at startup");
        notice.show(false, 100);
        Check(notice.read(1599, true, on) && !on, "off notice survives hiding the minimap for 1.5 seconds");
        Check(!notice.read(1600, true, on), "minimap notice expires at 1.5 seconds");
        notice.show(true, 2000);
        notice.show(false, 2100);
        Check(notice.read(2200, true, on) && !on, "rapid toggles replace the previous notice with actual latest state");
        Check(!notice.read(2201, false, on) && !notice.read(2202, true, on), "leaving gameplay dismisses rather than replays the notice");
        notice.show(true, 3000); notice.clear();
        Check(!notice.read(3001, true, on), "session reset clears pending notices");
    }
    void Raster(void*, std::uint32_t, const VkClearAttachment* a, std::uint32_t count, const VkClearRect* rects)
    {
        ++clears;
        for (std::uint32_t i = 0; i < count; ++i)
            for (std::uint32_t dy = 0; dy < rects[i].rect.extent.height; ++dy)
                for (std::uint32_t dx = 0; dx < rects[i].rect.extent.width; ++dx)
                {
                    int x = rects[i].rect.offset.x + dx, y = rects[i].rect.offset.y + dy;
                    if (x < 0 || x >= 512 || y < 0 || y >= 320) std::exit(3);
                    pixels[y][x] = { a->clearValue.color.float32[0], a->clearValue.color.float32[1], a->clearValue.color.float32[2] };
                }
    }
    void WritePreview(const char* path)
    {
        BITMAPFILEHEADER h{}; BITMAPINFOHEADER info{};
        h.bfType = 0x4D42; h.bfOffBits = sizeof(h) + sizeof(info); h.bfSize = h.bfOffBits + sizeof(pixels) / sizeof(Pixel) * 4;
        info.biSize = sizeof(info); info.biWidth = 512; info.biHeight = -320;
        info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h)); out.write(reinterpret_cast<const char*>(&info), sizeof(info));
        for (const auto& row : pixels) for (const auto& p : row)
        {
            const std::uint8_t color[] = { static_cast<std::uint8_t>(p.b * 255), static_cast<std::uint8_t>(p.g * 255), static_cast<std::uint8_t>(p.r * 255), 255 };
            out.write(reinterpret_cast<const char*>(color), 4);
        }
        Check(out.good(), "CPU preview written");
    }
    SHORT sizeKeys[256]{};
    float framePush[8]{};
    SHORT WINAPI ReadSizeKey(int key) {
        if (key <= 0 || key >= 255) {
            CheckPerfFilter(false, "resize polls only a valid virtual key");
            return 0;
        }
        return sizeKeys[key];
    }
    int CheckBraceResizeKeys()
    {
        const auto savedPath = g_shroudtopiaConfigPath;
        g_shroudtopiaConfigPath.clear();
        RefreshMinimapConfig(nullptr, true);
        auto release = [] {
            std::memset(sizeKeys, 0, sizeof(sizeKeys));
            UpdateMinimapSizeHotkeys(true, ReadSizeKey);
        };
        auto key = [&](int vk, bool gameplay = true) {
            release(); sizeKeys[vk] = SHORT(0x8000);
            UpdateMinimapSizeHotkeys(gameplay, ReadSizeKey);
        };
        key(VK_F9);
        CheckPerfFilter(g_minimapScalePercent == 100, "default resize leaves F9 free");
        RefreshMinimapConfig(nullptr, true); key(VK_F11);
        CheckPerfFilter(g_minimapScalePercent == 100, "default resize leaves F11 free");
        const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
        for (const char brace : {'{', '}'})
        {
            const SHORT mapping = VkKeyScanExA(brace, layout);
            CheckPerfFilter(mapping != -1, "test keyboard layout can produce a literal brace");
            if (mapping == -1) continue;
            const int vk = mapping & 0xff, mods = (mapping >> 8) & 7;
            RefreshMinimapConfig(nullptr, true);
            release(); sizeKeys[vk] = SHORT(0x8000);
            UpdateMinimapSizeHotkeys(true, ReadSizeKey);
            if (mods != 0) CheckPerfFilter(g_minimapScalePercent == 100,
                "unmodified bracket does not trigger a modified brace binding");
            release(); g_minimapScalePercent = 100;
            sizeKeys[vk] = SHORT(0x8000);
            sizeKeys[VK_SHIFT] = mods & 1 ? SHORT(0x8000) : 0;
            sizeKeys[VK_CONTROL] = mods & 2 ? SHORT(0x8000) : 0;
            sizeKeys[VK_MENU] = mods & 4 ? SHORT(0x8000) : 0;
            UpdateMinimapSizeHotkeys(true, ReadSizeKey);
            const int expected = brace == '{' ? 75 : 125;
            CheckPerfFilter(g_minimapScalePercent == expected, "literal brace resizes in the correct direction");
            UpdateMinimapSizeHotkeys(true, ReadSizeKey);
            CheckPerfFilter(g_minimapScalePercent == expected, "holding brace chord does not repeat");
            release(); g_minimapScalePercent = 100;
            sizeKeys[vk] = SHORT(0x8000);
            sizeKeys[VK_SHIFT] = mods & 1 ? SHORT(0x8000) : 0;
            sizeKeys[VK_CONTROL] = mods & 2 ? SHORT(0x8000) : 0;
            sizeKeys[VK_MENU] = mods & 4 ? SHORT(0x8000) : 0;
            UpdateMinimapSizeHotkeys(false, ReadSizeKey);
            UpdateMinimapSizeHotkeys(true, ReadSizeKey);
            CheckPerfFilter(g_minimapScalePercent == 100, "background brace press is not replayed in gameplay");
        }
        // Synthetic keyboard mappings exercise Shift and AltGr independently of
        // the build machine's layout. The live layout case above tests config wiring.
        for (const int modifiers : {0, 1, 6}) {
            const int binding = EncodeMinimapLiteralKey(static_cast<SHORT>((modifiers << 8) | VK_OEM_4));
            for (int actual = 0; actual < 8; ++actual) {
                std::memset(sizeKeys, 0, sizeof(sizeKeys));
                sizeKeys[VK_OEM_4] = SHORT(0x8000);
                sizeKeys[VK_SHIFT] = actual & 1 ? SHORT(0x8000) : 0;
                sizeKeys[VK_CONTROL] = actual & 2 ? SHORT(0x8000) : 0;
                sizeKeys[VK_MENU] = actual & 4 ? SHORT(0x8000) : 0;
                CheckPerfFilter(IsMinimapSizeKeyDown(binding, ReadSizeKey) == (actual == modifiers),
                    "literal resize requires exactly its mapped modifiers");
            }
        }
        CheckPerfFilter(EncodeMinimapLiteralKey(-1) == 0 && EncodeMinimapLiteralKey(0) == 0 &&
            EncodeMinimapLiteralKey(static_cast<SHORT>(0x8db)) == 0,
            "unmappable or unsupported literal keys stay disabled");
        const int fallback = EncodeMinimapLiteralKey(VkKeyScanExA('{', layout));
        CheckPerfFilter(ParseMinimapSizeKey(" { ", 0, layout) == fallback &&
            ParseMinimapSizeKey("invalid", fallback, layout) == fallback &&
            ParseMinimapSizeKey("0x78", fallback, layout) == VK_F9,
            "resize parser trims braces and preserves fallback and legacy hex");
        ModContext custom{};
        custom.config.GetString = [](const char*, const char* name, std::string fallback) {
            if (!std::strcmp(name, "size_decrease_key")) return std::string("F9");
            if (!std::strcmp(name, "size_increase_key")) return std::string("F11");
            return fallback;
        };
        RefreshMinimapConfig(&custom, true);
        key(VK_F9);
        CheckPerfFilter(g_minimapScalePercent == 75, "explicit custom F9 remains supported");
        key(VK_F11);
        CheckPerfFilter(g_minimapScalePercent == 100, "explicit custom F11 remains supported");
        custom.config.GetString = [](const char*, const char* name, std::string fallback) {
            if (!std::strcmp(name, "size_decrease_key") || !std::strcmp(name, "size_increase_key")) return std::string("none");
            return fallback;
        };
        RefreshMinimapConfig(&custom, true); key(VK_F9); key(VK_F11);
        CheckPerfFilter(g_minimapScalePercent == 100, "disabled resize bindings remain disabled");
        release(); g_shroudtopiaConfigPath = savedPath;
        return g_perfFilterTestFailures == 0 ? 0 : 1;
    }

    void CheckMinimapSizing()
    {
        ModContext legacy{};
        legacy.config.GetString = [](const char*, const char* key, std::string fallback) {
            if (!std::strcmp(key, "size_decrease_key")) return std::string("F9");
            if (!std::strcmp(key, "size_increase_key")) return std::string("F11");
            return fallback;
        };
        RefreshMinimapConfig(&legacy, true);
        const int zoom = g_minimapZoomStep.load();
        Check(g_minimapScalePercent == 100 && g_minimapSizeDecreaseKey == VK_F9 && g_minimapSizeIncreaseKey == VK_F11,
            "custom F9 and F11 shortcuts preserve default 100 percent size");
        auto press = [](int key, bool gameplay = true) {
            std::memset(sizeKeys, 0, sizeof(sizeKeys)); UpdateMinimapSizeHotkeys(gameplay, ReadSizeKey);
            sizeKeys[key] = SHORT(0x8000); UpdateMinimapSizeHotkeys(gameplay, ReadSizeKey);
        };
        press(VK_F9); Check(g_minimapScalePercent == 75, "smaller key selects 75 percent");
        UpdateMinimapSizeHotkeys(true, ReadSizeKey);
        Check(g_minimapScalePercent == 75, "holding the resize key does not repeat");
        RefreshMinimapConfig(&legacy);
        Check(g_minimapScalePercent == 75, "unchanged config poll preserves session size");
        press(VK_F9); press(VK_F9);
        Check(g_minimapScalePercent == 50, "smaller key stops at 50 percent");
        for (int i=0;i<8;++i) press(VK_F11);
        Check(g_minimapScalePercent == 200 && g_minimapZoomStep == zoom, "larger key stops at 200 percent without changing terrain zoom");
        press(VK_F9, false); UpdateMinimapSizeHotkeys(true, ReadSizeKey);
        Check(g_minimapScalePercent == 200, "background or menu press is not replayed on returning to gameplay");
        std::memset(sizeKeys,0,sizeof(sizeKeys)); UpdateMinimapSizeHotkeys(true,ReadSizeKey);
        sizeKeys[VK_F9]=sizeKeys[VK_F11]=SHORT(0x8000); UpdateMinimapSizeHotkeys(true,ReadSizeKey);
        Check(g_minimapScalePercent == 200, "simultaneous resize shortcuts cancel each other");
        ModContext config{};
        config.config.GetString=[](const char*,const char* key,std::string fallback) {
            if (!std::strcmp(key,"scale_percent")) return std::string("75");
            if (!std::strcmp(key,"size_decrease_key")) return std::string("none");
            if (!std::strcmp(key,"size_increase_key")) return std::string("F7");
            return fallback;
        };
        RefreshMinimapConfig(&config);
        Check(g_minimapScalePercent==75, "changed saved scale applies live");
        press(VK_F9); press(VK_F11);
        Check(g_minimapScalePercent==75, "disabled and replaced size bindings stop responding");
        press(VK_F7); Check(g_minimapScalePercent==100, "rebound size key works");
        VulkanMinimapRenderer renderer{};
        renderer.width=512; renderer.height=320; renderer.fns.cmdClearAttachments=Raster;
        renderer.frameTextureReady=true;
        renderer.framePipeline=renderer.framePipelineLayout=renderer.frameDescriptorSet=1;
        renderer.fns.cmdBindPipeline=[](void*,std::uint32_t,void*) {};
        renderer.fns.cmdBindDescriptorSets=[](void*,std::uint32_t,void*,std::uint32_t,std::uint32_t,void* const*,std::uint32_t,const std::uint32_t*) {};
        renderer.fns.cmdPushConstants=[](void*,void*,std::uint32_t,std::uint32_t,std::uint32_t size,const void* data) {std::memcpy(framePush,data,size);};
        renderer.fns.cmdDraw=[](void*,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t) {};
        for (int percent : {50,75,100,125,150,175,200}) {
            std::memset(pixels,0,sizeof(pixels));
            {
                MinimapScaleScope scope(renderer,percent/100.f,256,160);
                CmdClearRect(renderer,nullptr,1,0,0,1,216,120,80,80);
                std::vector<VkClearRect> rects;
                AppendClippedClearRect(renderer,rects,256,160,40,40);
                Check(rects.size()==1 && rects[0].rect.extent.width==unsigned(percent*40/100),
                    "batched terrain and marker rectangles share the widget scale");
                int count=0; for(auto& row:pixels) for(auto& p:row) if(p.r>0.9f) ++count;
                Check(count==(percent*80/100)*(percent*80/100), "rendered footprint matches every advertised scale");
                Check(TryDrawMinimapTexturedFrame(renderer,nullptr,256,160,100,20,0.4f) &&
                    std::abs((framePush[2]-framePush[0])*256-240*percent/100.f)<1.1f &&
                    std::abs((framePush[3]-framePush[1])*160-240*percent/100.f)<1.1f,
                    "Vulkan frame texture matches the scaled terrain footprint");
                rects.clear(); AppendClippedClearRect(renderer,rects,-100,-100,800,800);
                Check(rects.size()==1 && rects[0].rect.offset.x>=0 && rects[0].rect.offset.y>=0 &&
                    rects[0].rect.offset.x+rects[0].rect.extent.width<=512 && rects[0].rect.offset.y+rects[0].rect.extent.height<=320,
                    "scaled rectangles clip safely at the screen boundary");
            }
            Check(renderer.widgetScale==1.f, "widget drawing restores normal scale for notices");
        }
        RefreshMinimapConfig(nullptr,true);
        std::memset(sizeKeys,0,sizeof(sizeKeys)); UpdateMinimapSizeHotkeys(true,ReadSizeKey);
    }
    void CheckMinimapPlacements()
    {
        bool allSix = true;
        for (const char* position : {"top-right", "middle-right", "bottom-right",
            "top-left", "middle-left", "bottom-left"})
        {
            const auto parsed = ParseMinimapPlacement(position);
            if (std::strcmp(MinimapPlacementName(parsed), position) != 0)
            {
                std::cerr << "FAIL: position is accepted and preserved: " << position
                    << " parsed as " << MinimapPlacementName(parsed) << '\n';
                allSix = false;
            }
            g_minimapPlacement.store(static_cast<int>(parsed));
            const int centerX = ComputeMinimapCenterX(1920, 100, 30, 128);
            const int expectedX = IsLeftMinimapPlacement(parsed) ? 136 : 1784;
            if (centerX != expectedX)
            {
                std::cerr << "FAIL: position preserves the selected side: " << position
                    << " expected x=" << expectedX << " got x=" << centerX << '\n';
                allSix = false;
            }
        }
        Check(allSix, "all six minimap positions parse and report their configured side");
    }
    void CheckMinimapOffset()
    {
        std::string position = "middle-right";
        std::string offset = "0";
        ModContext config{};
        config.config.GetString = [&](const char*, const char* key, std::string fallback)
        {
            if (std::strcmp(key, "position") == 0) return position;
            if (std::strcmp(key, "offset") == 0) return offset;
            return fallback;
        };

        RefreshMinimapConfig(&config, true);
        bool correct = ComputeMinimapCenterY(1080, 100, 20) == 540;
        offset = "96";
        for (const auto& item : std::array<std::pair<const char*, int>, 6>{{
            {"top-right", 240}, {"middle-right", 540}, {"bottom-right", 960},
            {"top-left", 240}, {"middle-left", 540}, {"bottom-left", 960}}})
        {
            position = item.first;
            RefreshMinimapConfig(&config, true);
            const int actual = ComputeMinimapCenterY(1080, 100, 20);
            if (actual != item.second - 96)
            {
                std::cerr << "FAIL: +96 px offset at 1080p for " << position
                    << " expected y=" << item.second - 96 << " got y=" << actual << '\n';
                correct = false;
            }
        }
        position = "middle-left";
        offset = "-48";
        RefreshMinimapConfig(&config, true);
        if (ComputeMinimapCenterY(1080, 100, 20) != 588)
        {
            std::cerr << "FAIL: negative offset moves the selected position down\n";
            correct = false;
        }
        position = "bottom-left";
        offset = "96";
        RefreshMinimapConfig(&config, true);
        if (ComputeMinimapCenterY(2160, 200, 40) != 1728)
        {
            std::cerr << "FAIL: offset scales from 1080p at 2160p\n";
            correct = false;
        }
        RefreshMinimapConfig(nullptr, true);
        Check(correct, "single vertical offset defaults to zero, moves every position, and scales with resolution");
    }
    void CheckNorthUpOrientation()
    {
        RefreshMinimapConfig(nullptr, true);
        VulkanMinimapRenderer renderer{};
        const float heading = 1.57079632679f;
        const float smoothedHeading = SmoothMapHeading(renderer, heading, true, 100);
        const auto northUp = ResolveMinimapOrientationAngles(
            g_minimapOrientation.load(), smoothedHeading, true);
        const float mapHeading = northUp.mapHeading;
        bool fixedCardinals = std::fabs(mapHeading) < 0.0001f;
        float arrowX = 0.0f, arrowY = 0.0f;
        RotateArrowPoint(0.0f, -16.0f, northUp.playerArrowHeading, arrowX, arrowY);
        fixedCardinals = fixedCardinals && std::fabs(arrowX - 16.0f) < 0.001f &&
            std::fabs(arrowY) < 0.001f;
        const std::array<std::pair<float, float>, 4> landmarks = {{
            {0.0f, 100.0f}, {100.0f, 0.0f}, {0.0f, -100.0f}, {-100.0f, 0.0f}}};
        const std::array<std::pair<int, int>, 4> expected = {{{160, 60}, {260, 160}, {160, 260}, {60, 160}}};
        for (std::size_t i = 0; i < landmarks.size(); ++i)
        {
            int x = 0, y = 0; bool clipped = false;
            ProjectWorldToMinimap(landmarks[i].first, landmarks[i].second, 0, 0, 1,
                mapHeading, 160, 160, 130, x, y, clipped);
            fixedCardinals = fixedCardinals && x == expected[i].first && y == expected[i].second;
        }
        Check(fixedCardinals, "default north-up keeps cardinal landmarks fixed when player heading changes");

        ModContext config{};
        config.config.GetString = [](const char*, const char* key, std::string fallback)
        {
            return std::strcmp(key, "map_orientation") == 0 ? std::string("rotating") : fallback;
        };
        RefreshMinimapConfig(&config, true);
        renderer = {};
        const float rotatingSmoothedHeading = SmoothMapHeading(renderer, heading, true, 200);
        const auto rotating = ResolveMinimapOrientationAngles(
            g_minimapOrientation.load(), rotatingSmoothedHeading, true);
        Check(std::fabs(rotating.mapHeading - heading) < 0.0001f &&
            std::fabs(rotating.playerArrowHeading) < 0.0001f,
            "explicit rotating orientation keeps the existing heading-following map");
        RefreshMinimapConfig(nullptr, true);
    }
    void CheckRedDeathSkull()
    {
        VulkanMinimapRenderer renderer{};
        renderer.width = 512;
        renderer.height = 320;
        renderer.fns.cmdClearAttachments = Raster;
        std::memset(pixels, 0, sizeof(pixels));
        clears = 0;
        DrawDeathMarker(renderer, nullptr, 160, 160, 130, 210, 160, 0x1AD8F96E);
        const Pixel bone = pixels[155][210];
        Check(clears == 2 && bone.r > 0.8f && bone.g < 0.3f && bone.b < 0.3f,
            "death skull keeps its shape and outline while rendering its bone red");
    }
    void CheckDeaths(VulkanMinimapRenderer& renderer)
    {
        std::array<std::uint8_t, WORLD_MAP_MARKER_STRIDE * 3> deaths{};
        const uintptr_t ui = reinterpret_cast<uintptr_t>(state.data());
        const DWORD now = GetTickCount();
        Entry(deaths.data(), 0, 1020, 1000, 0x1AD8F96E);
        Entry(deaths.data(), 1, 1000, 1040, 0x1AD8F96E);
        Entry(deaths.data(), 2, -10000, 1000, 0x1AD8F96E);
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(deaths.data()));
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{3});
        g_deathMarkerLayoutSupported = g_worldMapLayoutSupported = true;
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).size() == 2, "multiple tombstones are captured; invalid coordinates are omitted");
        Check(CopyWorldMapMarkers(now).size() == 1, "death capture preserves existing POIs");
        Check(CopyDeathMarkers(now + 2001).empty(), "interrupted tombstone feed expires");
        CaptureWorldMapMarkers(ui, 0xFFFFFFF0u);
        Check(CopyDeathMarkers(0x10).size() == 2, "tombstone freshness survives tick wrap");
        Entry(deaths.data(), 0, 1050, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now)[0].x == Fixed(1050), "tombstone positions update from each completed snapshot");
        g_showDeathMarkers = false;
        Check(CopyDeathMarkers(now).empty(), "death visibility toggle hides existing tombstones");
        g_showDeathMarkers = true;
        Check(CopyDeathMarkers(now).size() == 2, "death visibility toggle restores the current snapshot");

        // Exercise the real draw path: ordinary POI limits and visibility cannot
        // suppress deaths; even coincident tombstones remain separate records.
        Entry(deaths.data(), 1, 1050, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, GetTickCount());
        g_showWorldMarkers = false; g_minimapMaxDrawnPoints = 8;
        std::memset(pixels, 0, sizeof(pixels)); clears = 0;
        DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, 1, 0, 160, 160, 130, 116);
        Check(clears == 4 && pixels[155][210].r > 0.8f && pixels[155][210].g < 0.3f && pixels[155][210].b < 0.3f,
            "death layer renders red skulls independently of POI visibility, limits and deduplication");
        Check(pixels[159][207].r < 0.1f, "skull fallback keeps dark eye sockets");
        g_showDeathMarkers = false; clears = 0;
        DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, 1, 0, 160, 160, 130, 116);
        Check(clears == 0, "disabled death layer issues no draw commands");
        g_showDeathMarkers = true; g_showWorldMarkers = true;
        g_minimapMaxDrawnPoints = MINIMAP_DEFAULT_MAX_DRAWN_POINTS;
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        Entry(deaths.data(), 0, 9000, 1000, 0x1AD8F96E);
        CaptureWorldMapMarkers(ui, GetTickCount());
        for (float heading : {0.0f, 1.5707963f, 3.1415926f, 4.7123889f})
        {
            for (float zoom : {0.5f, 12.0f})
            {
                std::memset(pixels, 0, sizeof(pixels)); clears = 0;
                DrawLiveMarkers(renderer, nullptr, {}, 1000, 1000, zoom, heading, 160, 160, 130, 116);
                bool outside = false;
                for (int y=0; y<320; ++y) for (int x=0; x<512; ++x)
                    if ((x-160)*(x-160)+(y-160)*(y-160)>130*130 && pixels[y][x].r != 0) outside=true;
                Check(clears == 2 && !outside, "distant skull remains inside the rim through rotation and zoom");
            }
        }
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty(), "recovering the final tombstone removes its marker");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).size() == 1, "a later death creates a fresh marker");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{DEATH_MAP_MAX_ENTRIES + 1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "oversized death array clears only deaths and preserves POIs");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, uintptr_t{1});
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "unreadable death array clears only deaths and preserves POIs");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(deaths.data()));
        CaptureWorldMapMarkers(ui, now);
        ClearLiveMarkers();
        Check(CopyDeathMarkers(now).empty(), "world exit clears tombstones");
        g_deathMarkerLayoutSupported = false;
        CaptureWorldMapMarkers(ui, now);
        Check(CopyDeathMarkers(now).empty() && CopyWorldMapMarkers(now).size() == 1,
            "unsupported death layout leaves existing POI capture working");
        Put(state.data(), DEATH_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
        ClearLiveMarkers();
        g_deathMarkerLayoutSupported = true;
    }

    std::array<std::uint8_t, 0x70> daytimeState{};
    void __fastcall InitDaytime(void* ctx, void* record, std::uint32_t size)
    {
        if (size != 0x28) std::exit(2);
        static_cast<uintptr_t*>(ctx)[1] = 999;
        static_cast<uintptr_t*>(record)[2] = reinterpret_cast<uintptr_t>(daytimeState.data());
    }
    void CheckWorldClock(VulkanMinimapRenderer& renderer)
    {
        const auto ns = CLOCK_NANOSECONDS_PER_MINUTE;
        const auto ptr = reinterpret_cast<uintptr_t>(daytimeState.data());
        Put(daytimeState.data(), 0x28, 360 * ns);
        Put(daytimeState.data(), 0x30, 1080 * ns);
        WorldClockSnapshot clock{};
        for (int minute : {0, 359, 360, 719, 720, 1079, 1080, 1439})
        {
            Put(daytimeState.data(), 0x48, minute * ns + ns - 1);
            Check(ReadWorldClock(ptr, clock) && clock.minuteOfDay == minute &&
                clock.daytime == (minute >= 360 && minute < 1080), "solar minute and sun/moon match day boundaries");
        }
        Put(daytimeState.data(), 0x28, 420 * ns); Put(daytimeState.data(), 0x30, 1200 * ns);
        Put(daytimeState.data(), 0x48, 1100 * ns);
        Check(ReadWorldClock(ptr, clock) && clock.daytime, "sun uses the world's configured daylight interval");
        for (auto bad : {-1LL, CLOCK_NANOSECONDS_PER_DAY, 0x7FFFFFFFFFFFFFFFLL})
        {
            Put(daytimeState.data(), 0x48, bad);
            Check(!ReadWorldClock(ptr, clock) && !clock.valid, "invalid time is hidden instead of formatted");
        }
        Check(!ReadWorldClock(0, clock) && !ReadWorldClock(0x10001, clock), "missing time object is safely rejected");
        Put(daytimeState.data(), 0x48, 725 * ns); Put(daytimeState.data(), 0x28, 1250 * ns);
        Check(!ReadWorldClock(ptr, clock), "reversed daylight interval is rejected");
        Put(daytimeState.data(), 0x28, 360 * ns); Put(daytimeState.data(), 0x30, 1080 * ns);
        const auto savedInit = g_iterInit;
        g_iterInit = InitDaytime; g_clockLayoutSupported = true; g_showClock = true;
        uintptr_t ctx[] = {0x1234, 7};
        auto capture = [&]() { g_lastClockCaptureTick = GetTickCount() - 100; CaptureDaytimeUiHook(ctx, nullptr, nullptr, nullptr); };
        capture();
        Check(ctx[1] == 7 && CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 725,
            "clock hook uses private iterator and copies only values");
        Put(daytimeState.data(), 0x48, 360 * ns); capture();
        Check(CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 360,
            "sleep or server time jumps are reflected immediately without wall-clock interpolation");
        Put(daytimeState.data(), 0x48, 1439 * ns); capture();
        Put(daytimeState.data(), 0x48, std::int64_t{0}); capture();
        Check(CopyWorldClock(clock, GetTickCount()) && clock.minuteOfDay == 0 && !clock.daytime, "midnight wraps to 00:00 and moon");
        Check(!CopyWorldClock(clock, GetTickCount() + 2001), "clock disappears when source stops updating");
        g_worldClock.tick = 0xFFFFFFF0;
        Check(CopyWorldClock(clock, 0x10), "clock freshness handles tick wrap");
        capture(); g_showClock = false;
        Check(!CopyWorldClock(clock, GetTickCount()), "show_clock hides current snapshot");
        g_showClock = true;
        Check(CopyWorldClock(clock, GetTickCount()), "show_clock restores fresh snapshot");
        ClearLiveMarkers();
        Check(!CopyWorldClock(clock, GetTickCount()), "world exit clears clock");
        capture(); ++g_worldSessionGeneration;
        Check(!CopyWorldClock(clock, GetTickCount()), "previous-session clock cannot appear in a new world");
        capture(); g_clockLayoutSupported = false;
        Check(!CopyWorldClock(clock, GetTickCount()), "unsupported clock layout never renders cached data");
        g_clockLayoutSupported = true;

        // Execute the actual entry trampoline against a synthetic function with
        // the verified prologue and a known return value. No game is modified.
        auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        Check(code != nullptr, "allocate isolated clock hook fixture");
        constexpr int offset = 128;
        std::memcpy(code + offset, g_daytimeUiExpected.data(), g_daytimeUiExpected.size());
        const std::uint8_t tail[] = {0x48,0x83,0xC4,0x60,0x41,0x5E,0xB8,0x7B,0,0,0,0xC3};
        std::memcpy(code + offset + g_daytimeUiExpected.size(), tail, sizeof(tail));
        const auto savedBase = g_exeBase; g_exeBase = reinterpret_cast<uintptr_t>(code);
        auto* hook = InstallEntryHook(offset, g_daytimeUiExpected, reinterpret_cast<void*>(&CaptureDaytimeUiHook), "clock fixture");
        Check(hook && ActivateHook(hook), "clock entry trampoline installs on verified whole instructions");
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        using Fixture = int(__fastcall*)(void*);
        ClearLiveMarkers(); g_lastClockCaptureTick = GetTickCount() - 100;
        Check(reinterpret_cast<Fixture>(code + offset)(ctx) == 123 && ctx[1] == 7 && CopyWorldClock(clock, GetTickCount()),
            "real clock trampoline preserves stack, context and original return");
        hook->deactivate();
        Check(std::memcmp(code + offset, g_daytimeUiExpected.data(), g_daytimeUiExpected.size()) == 0,
            "clock hook removal restores original instructions");
        auto allocation = reinterpret_cast<void*>(hook->shellcode->data->address);
        delete hook->shellcode; delete hook->patch; delete hook;
        VirtualFree(allocation, 0, MEM_RELEASE); VirtualFree(code, 0, MEM_RELEASE);
        g_exeBase = savedBase; g_iterInit = savedInit;

        Check(TryLoadMinimapFrameFromPath("assets/embervale_minimap_frame.rgba", g_minimapFrame), "load actual compass frame for layout bounds");
        g_minimapFrame.attempted = true;
        for (int height : {480,720,1080,1439,1440,2160})
        {
            const int radius = ClampValue(height / 11, 104, 142);
            const int extra = MinimapRasterFrameExtra(radius);
            const int half = MinimapRasterVisibleHalfHeight(radius, extra);
            const int yOffset = -MaxValue(1, (2 * (radius + extra) + 128) / 256);
            const int topExtra = half - yOffset - radius;
            const int bottomExtra = half + yOffset - radius;
            const int footer = bottomExtra + CLOCK_FRAME_GAP + 2 * CLOCK_PANEL_HALF_HEIGHT;
            for (auto placement : {MinimapPlacement::TopRight,MinimapPlacement::MiddleRight,MinimapPlacement::BottomRight})
            {
                g_minimapPlacement = static_cast<int>(placement);
                for (bool showClock : {false, true})
                {
                    const int cy = ComputeMinimapCenterY(height, radius, MaxValue(52,height/42), showClock ? footer : bottomExtra, topExtra);
                    Check(cy - half + yOffset >= 8 && cy + radius + (showClock ? footer : bottomExtra) <= height - 8,
                        "complete frame and optional clock fit every placement and screen height");
                    if (placement == MinimapPlacement::TopRight)
                        Check(cy - half + yOffset >= height / 9 + 20, "top-right frame leaves the localized quest title and Journal region clear");
                }
            }
            Check(CLOCK_FRAME_GAP < 0 && CLOCK_FRAME_GAP >= -8 && bottomExtra + CLOCK_FRAME_GAP >= 8,
                "clock slightly overlaps the ornament while staying outside the map circle");
            // The measured circular bound includes every opaque pixel at every
            // compass angle. Sample actual pixel corners, independently of the
            // production bound calculation, to guard against ornament overlap.
            bool containsAll = true;
            const auto& frame = g_minimapFrame;
            for (int y=0; y<frame.height; ++y) for (int x=0; x<frame.width; ++x)
            {
                if (!frame.rgba[(y * frame.width + x) * 4 + 3]) continue;
                const float dx = std::abs(x + 0.5f - frame.width * 0.5f) + 0.5f;
                const float dy = std::abs(y + 0.5f - frame.height * 0.5f) + 0.5f;
                if (std::hypot(dx,dy) * (2 * (radius + extra)) / frame.width > half) containsAll = false;
            }
            Check(containsAll, "clock clearance contains real frame ornaments at every rotation");
        }
        g_minimapPlacement = static_cast<int>(MinimapPlacement::BottomRight);
        clock.valid = true; clock.minuteOfDay = 725; clock.daytime = true;
        std::memset(pixels, 0, sizeof(pixels)); clears = 0;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(pixels[155][236].r > 0.9f && pixels[160][212].r == 1.0f && clears < 30,
            "weather-hidden clock retains time and its fallback sun icon");
        Check(pixels[142][256].r > 0 && pixels[139][256].r == 0, "clock frame has bounded vertical footprint");
        clock.daytime = false;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(pixels[160][207].b > 0.9f && pixels[157][216].r < 0.1f,
            "weather-hidden clock retains a crescent moon at night");
        clock.valid = false; clears = 0;
        DrawWorldClockPanel(renderer, nullptr, 256, 160, clock);
        Check(clears == 0, "invalid clock emits no drawing commands");
        ClearLiveMarkers();
    }

    float fixtureWeather[4] = {0, 1, 0, 0};
    int weatherCalls = 0;
    bool weatherArguments = false;
    float* __fastcall WeatherFixture(float* output, void* weather, const void* position, std::int64_t time)
    {
        ++weatherCalls;
        weatherArguments = weather == reinterpret_cast<void*>(0x2222) &&
            position == reinterpret_cast<void*>(0x3333) && time == 0x123456789abcdefLL;
        std::memcpy(output, fixtureWeather, sizeof(fixtureWeather));
        return output;
    }

    void CheckWeather(VulkanMinimapRenderer& renderer)
    {
        WorldWeatherSnapshot weather{};
        float values[4] = {};
        for (int i=0; i<4; ++i)
        {
            std::fill(std::begin(values), std::end(values), 0.0f); values[i]=1;
            Check(ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && weather.kind==i,
                "weather reader identifies each game-defined blend channel");
            Check(std::strlen(WeatherName(static_cast<WeatherKind>(weather.kind)))>=4,
                "each weather state has a readable label");
        }
        values[0]=0.2f; values[1]=0.3f; values[2]=0.5f; values[3]=0;
        Check(ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && weather.kind==2,
            "blended conditions choose the strongest local weather state");
        for (float invalid : {NAN, INFINITY, -0.1f, 1.1f})
        {
            values[0]=invalid;
            Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather) && !weather.valid,
                "nonfinite and out-of-range weather weights fail closed");
        }
        std::fill(std::begin(values),std::end(values),0.0f);
        Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather), "empty blend is unknown, never fabricated clear weather");
        std::fill(std::begin(values),std::end(values),1.0f);
        Check(!ReadWorldWeather(reinterpret_cast<uintptr_t>(values), weather), "invalid blend total is rejected");
        Check(!ReadWorldWeather(1, weather), "unreadable weather sample is rejected");

        const auto savedBase=g_exeBase, savedSize=g_exeImageSize;
        const auto savedSampler=g_sampleWeather;
        auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(code!=nullptr,"allocate isolated weather call-site fixture");
        // A real caller with Windows x64 shadow space, then a leaf target relay.
        const std::uint8_t caller[]={0x48,0x83,0xEC,0x28,0xE8,55,0,0,0,0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(code,caller,sizeof(caller));
        const std::uint8_t jump[]={0xFF,0x25,0,0,0,0};
        std::memcpy(code+64,jump,sizeof(jump));
        Put(code,70,reinterpret_cast<uintptr_t>(&WeatherFixture));
        g_exeBase=reinterpret_cast<uintptr_t>(code); g_exeImageSize=4096;
        Check(InstallWeatherSampleHook(4,65)==nullptr,"weather hook rejects a different original call target");
        code[4]=0xE9;
        Check(InstallWeatherSampleHook(4,64)==nullptr,"weather hook requires CALL, never accepts an unrelated jump");
        code[4]=0xE8;
        auto* hook=InstallWeatherSampleHook(4,64);
        Check(hook && ActivateHook(hook),"weather wrapper installs at the verified call site");
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        auto run=[&]() {
            g_lastWeatherCaptureTick=GetTickCount()-100;
            return reinterpret_cast<SampleWeatherFn>(code)(values,reinterpret_cast<void*>(0x2222),
                reinterpret_cast<void*>(0x3333),0x123456789abcdefLL);
        };
        g_weatherLayoutSupported=g_showWeather=true;
        ClearLiveMarkers(); weatherCalls=0;
        Check(run()==values && weatherCalls==1 && weatherArguments && std::memcmp(values,fixtureWeather,sizeof(values))==0,
            "actual relay preserves all four arguments, stack, return pointer and original output");
        Check(CopyWorldWeather(weather,GetTickCount()) && weather.kind==1,"wrapper captures completed local weather result");
        Check(!CopyWorldWeather(weather,weather.tick+2001),"stale weather disappears instead of sticking indefinitely");
        g_worldWeather.tick=0xfffffff0u;
        Check(CopyWorldWeather(weather,0x10),"weather freshness handles tick wrap");
        run(); g_showWeather=false;
        Check(!CopyWorldWeather(weather,GetTickCount()),"weather visibility option keeps snapshot hidden");
        Check(run()==values && weatherCalls==3,"hidden weather still preserves the original ambient sampler");
        g_showWeather=true;
        Check(CopyWorldWeather(weather,GetTickCount()),"weather visibility restores fresh data");
        ClearLiveMarkers();
        Check(!CopyWorldWeather(weather,GetTickCount()),"world exit clears weather");
        run(); ++g_worldSessionGeneration;
        Check(!CopyWorldWeather(weather,GetTickCount()),"previous-world weather cannot survive a session change");
        fixtureWeather[0]=1; fixtureWeather[1]=0; run();
        Check(CopyWorldWeather(weather,GetTickCount()) && weather.kind==0,"weather updates to clear after rain ends");
        fixtureWeather[0]=NAN; run();
        Check(!CopyWorldWeather(weather,GetTickCount()),"invalid new sample clears the previous valid state");
        fixtureWeather[0]=1;
        g_weatherLayoutSupported=false;
        const int before=weatherCalls;
        Check(run()==values && weatherCalls==before+1 && !CopyWorldWeather(weather,GetTickCount()),
            "disabled weather support forwards game behavior unchanged");
        DestroyCallSiteHook(hook);
        Check(!hook && std::memcmp(code,caller,sizeof(caller))==0,"removal restores the original call and frees the relay");
        Check(reinterpret_cast<SampleWeatherFn>(code)(values,reinterpret_cast<void*>(0x2222),
            reinterpret_cast<void*>(0x3333),0x123456789abcdefLL)==values,"original fixture runs normally after hook removal");
        VirtualFree(code,0,MEM_RELEASE); g_exeBase=savedBase; g_exeImageSize=savedSize; g_sampleWeather=savedSampler;

        WorldClockSnapshot clock{}; clock.valid=true; clock.minuteOfDay=12*60+35; clock.daytime=true;
        for (int i=0; i<4; ++i)
        {
            weather={}; weather.valid=true; weather.kind=static_cast<std::uint8_t>(i);
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
            Check(clears<25 && pixels[155][157].r>0.9f && pixels[155][272].r>0.9f,
                "compact weather panel retains time and its weather label in bounded batches");
            bool clipped=false;
            for (int y=0;y<320;++y) for(int x=0;x<512;++x)
                if(pixels[y][x].r!=0 && (y<142 || y>178 || x<124 || x>388)) clipped=true;
            Check(!clipped,"every weather label and icon fits the clock frame");
        }
        weather.valid=false; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
        Check(pixels[160][136].r==0 && pixels[155][236].r>.9f && pixels[160][212].r==1,
            "missing weather retains the compact clock and its single fallback sun");
        g_weatherLayoutSupported=true; ClearLiveMarkers();
    }

    int fogCalls = 0;
    bool fogArguments = false;
    NativeFogHeader* fixtureFog = nullptr;
    float fogPosition[2] = {252,260};
    void __fastcall FogFixture(void* fog, const float* position, float radius)
    {
        ++fogCalls;
        fogArguments = fog == fixtureFog && position == fogPosition && radius == 37.5f;
        if (fixtureFog->data) reinterpret_cast<std::uint8_t*>(fixtureFog->data)[0] = 255;
    }

    void CheckLiveFog(VulkanMinimapRenderer& renderer)
    {
        std::vector<std::uint8_t> bytes(4*1024);
        NativeFogHeader header{512,512,2,2,0,reinterpret_cast<uintptr_t>(bytes.data()),4};
        const auto address = reinterpret_cast<uintptr_t>(&header);
        LiveFogSnapshot fog;
        bytes[0]=255; bytes[1024]=192; bytes[2048]=128; bytes[3072]=64;
        Check(ReadLiveFog(address,fog),"native tile-major fog header and bounded grid decode");
        Check(SampleLiveFog(&fog,4,508)==1 && SampleLiveFog(&fog,260,508)==192/255.0f &&
            SampleLiveFog(&fog,4,252)==128/255.0f && SampleLiveFog(&fog,260,252)==64/255.0f,
            "all four tile origins match native orientation and stride");
        bytes[31]=255; bytes[32]=128;
        ReadLiveFog(address,fog);
        Check(SampleLiveFog(&fog,252,508)==1 && SampleLiveFog(&fog,4,500)==128/255.0f,
            "within-tile row stride is 32 bytes");
        Check(std::fabs(SampleLiveFog(&fog,256,508)-(255+192)/510.0f)<0.00001f,
            "bilinear coverage is continuous across tile boundaries");
        Check(SampleLiveFog(&fog,0,512)==1 && SampleLiveFog(&fog,512,0)==0,
            "world edges clamp to last pixel without crossing tile storage");
        Check(SampleLiveFog(&fog,-1,2)==0 && SampleLiveFog(&fog,513,2)==0 &&
            SampleLiveFog(&fog,NAN,2)==0 && SampleLiveFog(nullptr,4,4)==0,
            "outside world and unavailable fog never reveal terrain");
        header.worldHeight=768; header.rows=3; header.tileCount=6; bytes.resize(6*1024);
        header.data=reinterpret_cast<uintptr_t>(bytes.data()); bytes[5*1024]=255;
        Check(ReadLiveFog(address,fog) && SampleLiveFog(&fog,260,252)==1,
            "rectangular world uses its own height for north-up sampling");
        auto good=header;
        for (float value : {0.0f,-1.0f,NAN,INFINITY,65536.0f})
        {
            header.worldHeight=value;
            Check(!ReadLiveFog(address,fog) && fog.tiles.empty(),"invalid world extent rejects fog without retaining old bytes");
        }
        header=good; header.tileCount=9999999;
        Check(!ReadLiveFog(address,fog),"oversized or mismatched tile allocation rejected before allocation");
        header=good; header.columns=129;
        Check(!ReadLiveFog(address,fog),"fog grid dimensions have a fixed upper bound");
        header=good; header.rows=0;
        Check(!ReadLiveFog(address,fog),"empty tile grid rejected");
        header=good; header.data=0x10001;
        Check(!ReadLiveFog(address,fog) && fog.tiles.empty(),"unreadable fog bytes fail closed");
        Check(!ReadLiveFog(0x10001,fog),"unreadable fog header rejected");
        header=good;
        const auto savedBase=g_exeBase, savedSize=g_exeImageSize;
        const auto savedUpdate=g_updateFog;
        auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(code!=nullptr,"allocate isolated native fog call fixture");
        const std::uint8_t caller[]={0x48,0x83,0xEC,0x28,0xE8,55,0,0,0,0x48,0x83,0xC4,0x28,0xC3};
        const std::uint8_t jump[]={0xFF,0x25,0,0,0,0};
        std::memcpy(code,caller,sizeof(caller)); std::memcpy(code+64,jump,sizeof(jump));
        Put(code,70,reinterpret_cast<uintptr_t>(&FogFixture));
        g_exeBase=reinterpret_cast<uintptr_t>(code); g_exeImageSize=4096;
        auto* hook=InstallVerifiedCallHook(4,64,reinterpret_cast<uintptr_t>(&CaptureFogUpdate));
        g_updateFog=reinterpret_cast<UpdateFogFn>(code+64);
        Check(hook && ActivateHook(hook),"fog wrapper installs at original CALL site");
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        fixtureFog=&header; g_liveFogLayoutSupported=g_showFogOfWar=true;
        auto run=[&]() { g_lastFogCaptureTick=GetTickCount()-500;
            reinterpret_cast<UpdateFogFn>(code)(&header,fogPosition,37.5f); };
        ClearLiveMarkers(); bytes[0]=0; fogCalls=0; run();
        auto snapshot=CopyLiveFog(GetTickCount());
        Check(fogCalls==1 && fogArguments && snapshot && snapshot->tiles[0]==255,
            "real call relay preserves pointer and XMM2 radius and captures after the original update");
        bytes[0]=0;
        Check(snapshot->tiles[0]==255,"render snapshot owns bytes independently of game storage");
        reinterpret_cast<UpdateFogFn>(code)(&header,fogPosition,37.5f);
        Check(fogCalls==2 && CopyLiveFog(GetTickCount())==snapshot,"capture throttle still calls native exploration on every frame");
        Check(!CopyLiveFog(snapshot->tick+2001),"stale exploration snapshot cannot reveal terrain");
        auto wrapped=std::make_shared<LiveFogSnapshot>(*snapshot); wrapped->tick=0xfffffff0u; g_liveFog=wrapped;
        Check(CopyLiveFog(0x10)!=nullptr,"fog freshness handles tick wrap");
        run(); g_showFogOfWar=false; const auto old=g_liveFog; run();
        Check(!CopyLiveFog(GetTickCount()) && g_liveFog==old && fogArguments,
            "fog disabled skips copying but native exploration continues");
        g_showFogOfWar=true; run();
        Check(CopyLiveFog(GetTickCount())!=nullptr,"fog can be re-enabled live without restarting");
        ++g_worldSessionGeneration;
        Check(!CopyLiveFog(GetTickCount()),"previous-character exploration cannot leak into another session");
        run(); ClearLiveMarkers();
        Check(!CopyLiveFog(GetTickCount()),"world exit clears exploration snapshot");
        run(); header.data=0; run();
        Check(!CopyLiveFog(GetTickCount()),"invalid new grid removes old exploration snapshot");
        header=good; run(); g_liveFogLayoutSupported=false;
        Check(!CopyLiveFog(GetTickCount()),"unsupported executable never uses cached exploration");
        DestroyCallSiteHook(hook);
        Check(std::memcmp(code,caller,sizeof(caller))==0,"fog hook removal restores original native call");
        VirtualFree(code,0,MEM_RELEASE); g_exeBase=savedBase; g_exeImageSize=savedSize; g_updateFog=savedUpdate;
        g_liveFogLayoutSupported=true;

        const auto savedMap=g_realMap; const auto savedStep=g_minimapMapSampleStep.load();
        g_realMap={}; g_realMap.loaded=g_realMap.attempted=true; g_realMap.width=g_realMap.height=2;
        g_realMap.rgba.assign(16,230); g_minimapMapSampleStep=1;
        auto drawFog=std::make_shared<LiveFogSnapshot>();
        drawFog->worldWidth=drawFog->worldHeight=512; drawFog->columns=drawFog->rows=2;
        drawFog->tiles.assign(4096,0); drawFog->tick=GetTickCount(); drawFog->session=g_worldSessionGeneration;
        for(int tile : {1,3}) std::fill(drawFog->tiles.begin()+tile*1024,drawFog->tiles.begin()+(tile+1)*1024,255);
        g_liveFog=drawFog;
        for(float heading : {0.0f,1.5707963f,3.1415926f,4.7123889f}) for(float zoom : {0.5f,2.0f})
        {
            std::memset(pixels,0,sizeof(pixels));
            DrawRealMap(renderer,nullptr,256,160,60,256,256,zoom,heading);
            const int x=static_cast<int>(256+std::cos(heading)*30), y=static_cast<int>(160-std::sin(heading)*30);
            const int bx=512-x, by=320-y;
            Check(pixels[y][x].r>0.5f && pixels[by][bx].r<0.35f,
                "terrain fog remains attached to world coordinates at each heading and zoom");
        }
        g_showFogOfWar=false; DrawRealMap(renderer,nullptr,256,160,60,256,256,1,0);
        Check(pixels[160][226].r>0.5f,"fog-off draw reveals the original terrain");
        g_showFogOfWar=true; g_liveFog.reset(); DrawRealMap(renderer,nullptr,256,160,60,256,256,1,0);
        Check(pixels[160][226].r<0.35f && pixels[160][286].r<0.35f,"missing snapshot masks terrain instead of revealing the world");
        float r=.9f,g=.8f,b=.7f; ApplyExplorationFog(r,g,b,1);
        Check(std::fabs(r-.9f)<0.00001f && std::fabs(g-.8f)<0.00001f,"fully explored terrain keeps its original colour");
        ApplyExplorationFog(r,g,b,0);
        Check(r==.30f && g==.31f && b==.28f,"unexplored terrain is fully hidden by neutral fog");
        g_realMap=savedMap; g_minimapMapSampleStep=savedStep; g_showFogOfWar=false; ClearLiveMarkers();
    }

    void CheckFoggedIcons(VulkanMinimapRenderer& renderer)
    {
        ClearLiveMarkers();
        auto grid=std::make_shared<LiveFogSnapshot>();
        grid->worldWidth=grid->worldHeight=512; grid->columns=grid->rows=2;
        grid->tiles.assign(4096,0);
        for(int tile : {1,3}) std::fill(grid->tiles.begin()+tile*1024,grid->tiles.begin()+(tile+1)*1024,255);
        grid->tick=GetTickCount(); grid->session=g_worldSessionGeneration;
        g_liveFogLayoutSupported=g_showFogOfWar=true; g_liveFog=grid;
        const auto fog=CaptureMinimapFogFrame();
        const std::vector<MinimapWorldPoint> locations={
            {100,252,12},{100,252,31},{100,252,0xABCDEF01u},
            {400,252,12},{400,252,31},{400,252,0xABCDEF01u}};
        auto points=locations;
        FilterFoggedWorldPoints(points,fog);
        Check(points.size()==3 && std::all_of(points.begin(),points.end(),[](const auto& p){return p.x==400;}),
            "fog hides NPCs, original world locations and native location icons only at unexplored world positions");
        points={{100,252,MAP_MARKER_QUEST_KIND},{100,252,MAP_MARKER_QUEST_IMPORTANT_KIND},
            {100,252,10},{100,252,11},{100,252,13},{100,252,0xABCDEF01u,true},{100,252,31,false,true}};
        FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==7,"custom pins, quests and fallback navigation stay visible even when exploration data is missing");
        points=locations; FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.empty(),"missing fog data hides location icons consistently with covered terrain");
        points=locations; FilterFoggedWorldPoints(points,{false,nullptr});
        Check(points.size()==locations.size(),"disabling fog restores every normal location icon without a grid");
        points={{252,508,31},{260,508,31}};
        FilterFoggedWorldPoints(points,fog);
        Check(points.size()==1 && points[0].x==260,"fog icon edge follows tile boundary without leaking concealed locations");
        grid->tick=GetTickCount()-2001; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.empty(),"stale grid cannot leave icons visible over opaque terrain");
        grid->tick=GetTickCount();
        ++g_worldSessionGeneration; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.empty(),"previous-world explored icons disappear after a session change");
        grid->session=g_worldSessionGeneration;
        auto revealed=std::make_shared<LiveFogSnapshot>(*grid); revealed->tiles.assign(4096,255);
        g_liveFog=revealed; points=locations;
        FilterFoggedWorldPoints(points,CaptureMinimapFogFrame());
        Check(points.size()==6,"new exploration reveals previously hidden icons on the next frame");
        points=locations; FilterFoggedWorldPoints(points,fog);
        Check(points.size()==3,"one captured frame keeps terrain and icons on the same immutable snapshot");
        const auto savedLimit=g_minimapMaxDrawnPoints.load(); g_minimapMaxDrawnPoints=8;
        points.clear(); for(int i=0;i<16;++i) points.push_back({100.0f+i,252,31});
        for(int i=0;i<8;++i) points.push_back({400.0f+i,252,12});
        FilterFoggedWorldPoints(points,fog); LimitMinimapWorldPoints(points,100,252);
        Check(points.size()==8 && points[0].x>=400,"hidden nearby icons do not consume the visible icon budget");
        g_minimapMaxDrawnPoints=savedLimit;

        // Native quest types remain recognizable without matching atlas artwork.
        g_worldMapLayoutSupported=g_showWorldMarkers=true; g_worldMapValid=true; g_worldMapTick=GetTickCount();
        g_worldMapMarkers={{Fixed(100),0,Fixed(200),0,true},
            {Fixed(100),0,Fixed(200),MAP_MARKER_QUEST_IMPORTANT_KIND,false},
            {Fixed(130),0,Fixed(200),MAP_MARKER_QUEST_KIND,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {}); FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==2 && points[0].kind==MAP_MARKER_QUEST_IMPORTANT_KIND && points[1].kind==MAP_MARKER_QUEST_KIND,
            "both native quest markers survive fog and a coincident custom pin");
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        for(const auto& p:points) CmdClearPoiIcon(renderer,nullptr,256,160,130,static_cast<int>(p.x)+100,160,p.kind,false);
        Check(clears>0,"quest markers still issue visible drawing commands through fog");

        // The navigation draw layer must remain entirely independent of fog.
        g_liveMarkerLayoutSupported=g_showOtherPlayers=g_showPings=g_showWaypoints=g_showDeathMarkers=true;
        g_deathMarkerLayoutSupported=true; g_liveFog.reset();
        g_remotePlayers={{1,Fixed(260),0,Fixed(220),GetTickCount(),0}};
        g_pings={{2,Fixed(220),0,Fixed(220),GetTickCount(),0}};
        g_deathMarkers={{Fixed(220),0,Fixed(260),0x1AD8F96Eu,false}};
        CapturedWaypoint waypoint{}; waypoint.x=Fixed(260); waypoint.z=Fixed(260);
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        DrawLiveMarkers(renderer,nullptr,{waypoint},240,240,1,0,256,160,100,86);
        auto anyInk=[](int cx,int cy) {
            for(int y=cy-15;y<=cy+15;++y) for(int x=cx-15;x<=cx+15;++x)
                if(pixels[y][x].r>0.5f || pixels[y][x].g>0.5f || pixels[y][x].b>0.5f) return true;
            return false;
        };
        Check(anyInk(276,180) && anyInk(236,180) && anyInk(236,140) && anyInk(276,140),
            "players, pings, tombstones and waypoint outlines render through fully opaque fog");
        g_showFogOfWar=false; ClearLiveMarkers();
    }

    void CheckCustomPinOutlines(VulkanMinimapRenderer& renderer)
    {
        for(bool pinFirst : {false,true})
        {
            std::vector<MinimapWorldPoint> points;
            const MinimapWorldPoint location{250,250,31}, pin{250,250,11};
            PushWorldPointUnique(points,pinFirst?pin:location);
            PushWorldPointUnique(points,pinFirst?location:pin);
            Check(points.size()==1 && points[0].kind==31 && points[0].customPin,
                "pin-on-marker preserves the original icon in either capture order");
            FilterFoggedWorldPoints(points,{true,nullptr});
            Check(points.size()==1,"pinned location retains its underlying icon through fog");
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWorldPointIcons(renderer,nullptr,points,250,250,1,0,256,160,100,100);
            Check(pixels[160][274].r==1 && pixels[160][274].g<0.3f && pixels[160][256].r>0,
                "production icon draw keeps the location artwork and adds the red outline");
        }
        std::vector<MinimapWorldPoint> points;
        PushWorldPointUnique(points,{250,250,11});
        Check(points.size()==1 && points[0].kind==11 && !points[0].customPin,
            "custom pin on empty terrain retains the standalone flag");
        std::memset(pixels,0,sizeof(pixels)); clears=0;
        DrawWorldPointIcons(renderer,nullptr,points,250,250,1,0,256,160,100,100);
        Check(pixels[154][259].r>0.9f && pixels[160][274].r==0,"standalone pin draws its flag without a marker outline");

        std::memset(pixels,0,sizeof(pixels));
        CmdClearRect(renderer,nullptr,.4f,.7f,.9f,1,253,157,7,7);
        DrawCustomPinOutline(renderer,nullptr,256,160);
        Check(pixels[160][256].b==.9f && pixels[160][260].r==0,
            "red outline has a transparent interior and preserves original icon pixels");
        DrawWaypointDiamond(renderer,nullptr,256,160);
        Check(pixels[160][271].g==.86f && pixels[160][274].g==.20f && pixels[160][256].b==.9f,
            "yellow selected-waypoint edge and red custom-pin edge remain separately visible");

        g_worldMapLayoutSupported=g_showWorldMarkers=true; g_worldMapValid=true; g_worldMapTick=GetTickCount();
        g_worldMapMarkers={{Fixed(250),0,Fixed(250),0,true},
            {Fixed(250),0,Fixed(250),WORLD_MAP_FLAME_ALTAR,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {});
        Check(points.size()==1 && points[0].kind==31 && points[0].customPin,"completed native map lists produce a pinned altar icon");
        g_worldMapMarkers.erase(g_worldMapMarkers.begin());
        points.clear(); BuildWorldPoints(points,{}, {}, {});
        Check(points.size()==1 && points[0].kind==31 && !points[0].customPin,
            "removing a custom pin clears its outline while preserving the original location");
        FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==1 && points[0].navigation && !points[0].customPin,
            "placed Flame Altar remains visible after its custom pin is removed");
        g_worldMapMarkers={{Fixed(250),0,Fixed(250),WORLD_MAP_FLAME_ALTAR,false},
            {Fixed(300),0,Fixed(250),0xABCDEF01u,false},
            {Fixed(350),0,Fixed(250),0,false}};
        points.clear(); BuildWorldPoints(points,{}, {}, {}); FilterFoggedWorldPoints(points,{true,nullptr});
        Check(points.size()==1 && points[0].kind==31 && points[0].navigation,
            "only the placed altar bypasses fog while original world locations and NPCs remain hidden");
        ClearLiveMarkers();
    }

    void CheckIndependentPanels(VulkanMinimapRenderer& renderer)
    {
        WorldClockSnapshot clock{}; clock.valid=true; clock.daytime=true; clock.minuteOfDay=12*60+35;
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=3;
        for(bool time : {false,true}) for(bool conditions : {false,true})
        {
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            weather.valid=conditions;
            DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,time);
            Check((clears>0)==(time||conditions),"all four independent clock and weather visibility combinations render correctly");
            if (time!=conditions) Check(pixels[160][136].r==0 && pixels[160][256+(conditions?84:72)].r>0,
                "single-feature panel uses a frame sized for its content");
        }
        weather.valid=true; clock.valid=false; clears=0;
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,false);
        Check(clears>0,"weather remains visible when clock source is unavailable");
        g_worldClock={}; g_worldClock.valid=true; g_worldClock.tick=GetTickCount();
        g_worldClock.session=g_worldSessionGeneration; g_showClock=false; g_clockLayoutSupported=true;
        Check(!CopyWorldClock(clock,GetTickCount()) && CopyWorldClock(clock,GetTickCount(),false),
            "hidden clock still supplies real daylight state to the weather icon");
        g_showClock=true; ClearLiveMarkers();
    }

    void CheckCombinedWeatherSymbols(VulkanMinimapRenderer& renderer)
    {
        for(int kind=0;kind<4;++kind) for(bool daytime : {true,false})
        {
            std::memset(pixels,0,sizeof(pixels)); clears=0;
            DrawWeatherSymbol(renderer,nullptr,256,160,static_cast<WeatherKind>(kind),daytime);
            int celestial=0, precipitation=0; bool outside=false;
            for(int y=0;y<320;++y) for(int x=0;x<512;++x)
            {
                const auto& p=pixels[y][x];
                celestial += daytime ? p.r==1 && p.g==.78f : p.r==.74f && p.b==.98f;
                precipitation += p.r==.60f && p.b==1;
                outside |= p.r!=0 && (std::abs(x-256)>14 || std::abs(y-160)>12);
            }
            Check(celestial>0 && !outside && clears<=3,"each weather state has a bounded sun or moon variant");
            Check((precipitation>0)==(kind!=0),"clear skies omit precipitation while rain, snow and blizzard retain it");
            if(kind!=0) Check(pixels[159][252].r==.75f && pixels[159][252].b==.87f,
                "foreground cloud covers the lower sun or moon");
        }
        std::memset(pixels,0,sizeof(pixels));
        DrawWeatherSymbol(renderer,nullptr,256,160,WeatherKind::Clear,false);
        Check(pixels[160][251].b==.98f && pixels[160][259].r==0,"clear night has a crescent with an open cutout");
        std::memset(pixels,0,sizeof(pixels));
        DrawWeatherSymbol(renderer,nullptr,256,160,WeatherKind::Rain,false,false);
        Check(pixels[159][252].r==.75f && pixels[156][246].r==0,
            "unknown daylight keeps rain without fabricating a sun or moon");
        WorldClockSnapshot clock{}; clock.valid=true; clock.daytime=true; clock.minuteOfDay=12*60+35;
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=1;
        std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather);
        int celestialSlots=0; bool outsideWeatherSlot=false;
        for(int y=148;y<=172;++y) for(int x=132;x<=380;++x)
            if(pixels[y][x].r==1 && pixels[y][x].g==.78f)
            {
                ++celestialSlots;
                outsideWeatherSlot |= x<232 || x>260;
            }
        Check(celestialSlots>0 && !outsideWeatherSlot,"combined panel has one weather sun and no duplicate clock sun");
        std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,{},true);
        Check(pixels[160][212].r==1 && pixels[160][212].g==.78f,
            "weather-off panel retains its clock sun");
        clock.daytime=false; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,{},true);
        Check(pixels[160][207].b==.98f && pixels[160][215].r<.1f,
            "weather-off panel retains its clock moon");
        weather.valid=true; std::memset(pixels,0,sizeof(pixels));
        DrawWorldClockPanel(renderer,nullptr,256,160,clock,weather,false);
        Check(pixels[156][188].b==.98f && pixels[160][212].r<.1f,
            "weather-only panel retains the night variant with no clock icon");
    }

    void CheckClientLayout(const char* path)
    {
        Check(MinimapClient10::MatchesFile(path), "installed Steam executable matches current 1.0 profile");
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        Check(input.good(), "open optional client executable read-only");
        std::vector<char> file(static_cast<std::size_t>(input.tellg())); input.seekg(0);
        Check(input.read(file.data(), file.size()).good() && file.size() > sizeof(IMAGE_DOS_HEADER), "read client executable");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        Check(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) < file.size(), "client PE header bounds");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        Check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
            nt->OptionalHeader.SizeOfImage < 512 * 1024 * 1024, "client image is bounded x64 PE");
        std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& s = sections[i];
            Check(static_cast<std::uint64_t>(s.PointerToRawData) + s.SizeOfRawData <= file.size() &&
                static_cast<std::uint64_t>(s.VirtualAddress) + s.SizeOfRawData <= image.size(), "client section bounds");
            std::memcpy(image.data() + s.VirtualAddress, file.data() + s.PointerToRawData, s.SizeOfRawData);
        }
        const auto savedBase = g_exeBase; const auto savedSize = g_exeImageSize;
        const auto savedInit = g_iterInit; const auto savedNext = g_iterNext;
        const auto savedClient10 = g_client10;
        g_exeBase = reinterpret_cast<uintptr_t>(image.data()); g_exeImageSize = image.size();
        for (const auto rva : { 0x1e02290, 0x1e0c200, 0x1e02460, 0x1e06420, 0x1e03360,
            0x1e08760, 0x1e041d0, 0x1e07210, 0x1e0d770, 0x1e11c00, 0x1de8520 })
            for (int word : { 0, 2 })
            {
                uintptr_t value = 0; std::memcpy(&value, image.data() + rva + word * 8, 8);
                value = value - nt->OptionalHeader.ImageBase + g_exeBase;
                Put(image.data(), rva + word * 8, value);
            }
        for (int i=0;i<4;++i)
        {
            const auto rva=0x1bbb4d0+i*0x28;
            uintptr_t value=0; std::memcpy(&value,image.data()+rva,8);
            Put(image.data(),rva,value-nt->OptionalHeader.ImageBase+g_exeBase);
        }
        g_iterInit = reinterpret_cast<IterInitFn>(g_exeBase + 0x9bab60);
        g_iterNext = reinterpret_cast<IterNextFn>(g_exeBase + 0x9b5a50);
        g_client10 = true;
        Check(HasVerifiedWorldMapLayout(), "production layout gate matches installed Steam executable");
        Check(HasVerifiedDeathMarkerLayout(), "death layout gate matches the installed Steam executable");
        Check(HasVerifiedWorldClockLayout(), "clock layout matches the installed Steam executable");
        Check(HasVerifiedLiveFogLayout(), "live exploration layout matches installed Steam client and local ownership gate");
        for (auto rva : {0x307040,0x3070a2,0x3070e0,0x39b860,0x39c2d4,0x39c430,0x13b7edc,0x1e08770})
        {
            image[rva]^=1;
            Check(!HasVerifiedLiveFogLayout() && HasVerifiedWorldClockLayout(),
                "changed fog layout disables exploration capture independently of the clock");
            image[rva]^=1;
        }
        Check(HasVerifiedWorldWeatherLayout(), "weather layout matches installed Steam client, local-player guard and enum mapping");
        for (auto rva : {0x2fd800,0x2fdb4e,0xa9b9e0,0xa9bf1b,0x1bbb4e0,0x1e03370})
        {
            image[rva]^=1;
            Check(!HasVerifiedWorldWeatherLayout() && HasVerifiedWorldClockLayout(),
                "changed weather call, blend or enum disables weather but preserves the clock");
            image[rva]^=1;
        }
        for (auto rva : {0x2b31a0,0xe89080,0xe7172e,0xea7df2,0x1e02470,0x1e06430})
        {
            image[rva] ^= 1;
            Check(!HasVerifiedWorldClockLayout() && HasVerifiedWorldMapLayout(),
                "changed time layout disables clock without disabling map markers");
            image[rva] ^= 1;
        }
        for (auto rva : {0x310b10 + 0x40})
        {
            image[rva] ^= 1;
            Check(!HasVerifiedDeathMarkerLayout() && HasVerifiedWorldMapLayout(),
                "changed tombstone layout disables deaths without disabling POIs");
            image[rva] ^= 1;
        }
        image[0x303207] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed custom marker layout disables the reader");
        image[0x303207] ^= 1; image[0x2fee40] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed marker clearing layout disables the reader");
        image[0x2fee40] ^= 1; image[0x1e07220] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed named custom-marker owner disables the reader");
        image[0x1e07220] ^= 1; image[0x1e041e0] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed named marker-clear owner disables the reader");
        image[0x1e041e0] ^= 1; image[0x1e0d780] ^= 1;
        Check(!HasVerifiedWorldMapLayout(), "changed named marker-replication owner disables the reader");
        image[0x1e0d780] ^= 1;
        g_exeBase = savedBase; g_exeImageSize = savedSize;
        g_iterInit = savedInit; g_iterNext = savedNext; g_client10 = savedClient10;
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--native-filter-probe-focused") == 0)
    {
        std::cout << std::unitbuf;
        return CheckNativeFilterProbeCallbackFocused();
    }
    if (argc == 2 && std::strcmp(argv[1], "--brace-keys-focused") == 0)
    {
        std::cout << std::unitbuf;
        const int result = CheckBraceResizeKeys();
        CheckMinimapSizing();
        return result;
    }
    if (argc == 2 && std::strcmp(argv[1], "--marker-retry-focused") == 0)
    {
        std::cout << std::unitbuf;
        return CheckUnresolvedMarkerRetries();
    }
    if (argc == 2 && std::strcmp(argv[1], "--marker-hidden-miss-focused") == 0)
    {
        std::cout << std::unitbuf;
        return CheckHiddenMarkerMissFastPath();
    }
    if (argc == 3 && std::strcmp(argv[1], "--config-refresh-focused") == 0)
    {
        std::cout << std::unitbuf;
        return CheckMinimapConfigRefreshSnapshot(argv[2]);
    }
    if ((argc == 2 || argc == 3) && std::strcmp(argv[1], "--marker-fast-path-focused") == 0)
    {
        std::cout << std::unitbuf;
        return CheckVisibleMapMarkerFastPath(argc == 3 ? argv[2] : nullptr);
    }
    if (argc == 2 && std::strcmp(argv[1], "--halo-passivity-allocation-focused") == 0)
    {
        std::cout << std::unitbuf;
        CheckPlayerArrowHaloAllocationFailurePassivity();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--halo-passivity-logger-focused") == 0)
    {
        std::cout << std::unitbuf;
        CheckPlayerArrowHaloLoggerFailurePassivity();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--halo-observation-focused") == 0)
    {
        std::cout << std::unitbuf;
        CheckPlayerArrowHaloObservation();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--marker-routing-focused") == 0)
    {
        std::cout << std::unitbuf;
        CheckLatestProfileMarkerRouting();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--r2-backport-positions") == 0)
    {
        CheckMinimapPlacements();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--r2-backport-offset") == 0)
    {
        CheckMinimapOffset();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--r2-backport-north-up") == 0)
    {
        CheckNorthUpOrientation();
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--r2-backport-red-skull") == 0)
    {
        CheckRedDeathSkull();
        return 0;
    }
    if(argc==3 && std::strcmp(argv[1],"--appearance-preview")==0)
    {
        Check(TryLoadRealMapFromPath("assets/embervale_realmap_1280.rgba",g_realMap) && TryLoadDefaultMap(g_realMap,g_defaultMap),
            "appearance preview loads original terrain and color-only cache");
        g_realMap.attempted=true;
        VulkanMinimapRenderer renderer{};
        renderer.width=512; renderer.height=320;
        renderer.fns.cmdClearAttachments=Raster;
        MinimapFogFrame fog{}; fog.enabled=false;
        g_minimapMapSampleStep=1;
        for(int light:{0,55,100})
        {
            std::memset(pixels,0,sizeof(pixels)); g_minimapMapLight=light;
            g_defaultMapStyle=false;
            DrawRealMap(renderer,nullptr,128,160,115,5120,5120,44,0,fog);
            g_defaultMapStyle=true;
            DrawRealMap(renderer,nullptr,384,160,115,5120,5120,44,0,fog);
            Check(pixels[160][128].r>0 && pixels[160][384].r>0, "both appearance previews contain rendered terrain");
            WritePreview((std::string(argv[2])+"-light-"+std::to_string(light)+".bmp").c_str());
            std::memset(pixels,0,sizeof(pixels));
            g_defaultMapStyle=false;
            DrawRealMap(renderer,nullptr,128,160,115,2200,2200,2,0,fog);
            g_defaultMapStyle=true;
            DrawRealMap(renderer,nullptr,384,160,115,2200,2200,2,0,fog);
            Check(pixels[160][128].r>0 && pixels[160][384].r>0, "both detailed appearance previews contain rendered terrain");
            WritePreview((std::string(argv[2])+"-detail-light-"+std::to_string(light)+".bmp").c_str());
        }
        return 0;
    }
    std::cout << std::unitbuf;
    CheckPlayerArrowHaloAllocationFailurePassivity();
    CheckPlayerArrowHaloLoggerFailurePassivity();
    CheckPlayerArrowHaloObservation();
    CheckLatestProfileMarkerRouting();
    CheckToggleNotices();
    CheckMapAppearance();
    CheckMinimapSizing();
    CheckMinimapPlacements();
    CheckMinimapOffset();
    const auto generationBeforeAtlasFixture = g_minimapIconAtlasGeneration.load(std::memory_order_acquire);
    bool actualAtlasLoaded = false;
    MutateMinimapIconAtlasForTest([&](MinimapIconAtlas& atlas) {
        atlas = {};
        actualAtlasLoaded = TryLoadMinimapIconsFromPath("assets/embervale_minimap_icons.bin", atlas);
        if (actualAtlasLoaded) atlas.attempted = true;
    });
    Check(actualAtlasLoaded, "load actual icon atlas");
    Check(g_minimapIconAtlasGeneration.load(std::memory_order_acquire) > generationBeforeAtlasFixture,
        "successful atlas fixture publishes a new monotonic generation after loading");
    const auto nativeIcon = std::find_if(g_minimapIcons.icons.begin(), g_minimapIcons.icons.end(), [](const auto& icon) { return icon.key > 0xFFFFu; });
    Check(nativeIcon != g_minimapIcons.icons.end(), "atlas contains native world-map keys");
    const auto iconKey = nativeIcon->key;
    Check(std::count(nativeIcon->glyph.begin(), nativeIcon->glyph.end(), 1) > 0 &&
        std::count(nativeIcon->glyph.begin(), nativeIcon->glyph.end(), 1) < 200, "native glyph excludes its dark tile background");
    Entry(world.data(), 0, 1000, 1000, WORLD_MAP_FLAME_ALTAR);
    Entry(world.data(), 1, 1100, 1000, 0);
    Entry(world.data(), 2, 1200, 1000, iconKey);
    Entry(world.data(), 3, 1300, 1000, WORLD_MAP_PING);
    Entry(world.data(), 4, 1400, 1000, 0xDEADBEEF);
    Entry(custom.data(), 0, 1500, 1000, iconKey);
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(world.data()));
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{5});
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(custom.data()));
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
    const DWORD now = GetTickCount();
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(CopyWorldMapMarkers(now).size() == 6, "both completed map lists are captured");
    g_worldMapLayoutSupported = g_liveMarkerLayoutSupported = true;
    std::vector<MinimapWorldPoint> points;
    BuildWorldPoints(points, {}, {}, {});
    Check(points.size() == 5, "world-map mirror excludes the dedicated live ping duplicate");
    const auto countKind = [&](std::uint32_t kind) { return std::count_if(points.begin(), points.end(), [kind](const auto& p) { return p.kind == kind; }); };
    Check(countKind(11) == 1 && countKind(12) == 1 && countKind(31) == 1 && countKind(iconKey) == 1 && countKind(55) == 1,
        "custom pin, NPC fallback, altar, native icon and unknown fallback remain distinct");
    Entry(custom.data(), 0, 1200, 1000, iconKey);
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    points.clear(); BuildWorldPoints(points, {}, {}, {});
    Check(points.size() == 4 && countKind(11) == 0 && countKind(iconKey) == 1 &&
        std::count_if(points.begin(),points.end(),[&](const auto& p){return p.kind==iconKey && p.customPin;})==1,
        "custom pin decorates the original coincident POI icon");
    Check(CopyWorldMapMarkers(now + 2001).empty(), "interrupted world-map feed expires");
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), 0xFFFFFFF0u);
    Check(CopyWorldMapMarkers(0x10).size() == 6, "world-map freshness survives tick wrap");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
    Put(state.data(), CUSTOM_MAP_ARRAY_OFFSET + 8, std::uint64_t{0});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(g_worldMapValid && CopyWorldMapMarkers(now).empty(), "empty frame removes all earlier map markers");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{WORLD_MAP_MAX_ENTRIES + 1});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(!g_worldMapValid && CopyWorldMapMarkers(now).empty(), "oversized array is rejected before allocation or traversal");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET + 8, std::uint64_t{1});
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, uintptr_t{1});
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(!g_worldMapValid, "unreadable marker pointer clears the feed");
    Put(state.data(), WORLD_MAP_ARRAY_OFFSET, reinterpret_cast<uintptr_t>(world.data()));
    Entry(world.data(), 0, -10000, 1000, iconKey);
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now);
    Check(g_worldMapValid && CopyWorldMapMarkers(now).empty(), "out-of-world coordinates are omitted");
    Entry(world.data(), 0, 1000, 1000, iconKey);
    uintptr_t context[] = { 0x1234, 7 };
    g_iterInit = InitMap; g_lastWorldMapCaptureTick = GetTickCount() - 100;
    CaptureWorldMapHook(context, nullptr, nullptr, nullptr);
    Check(context[1] == 7 && CopyWorldMapMarkers(GetTickCount()).size() == 1, "named entry hook uses a private iterator context");
    g_worldMapLayoutSupported = false;
    ClearLiveMarkers(); CaptureWorldMapHook(context, nullptr, nullptr, nullptr);
    Check(CopyWorldMapMarkers(GetTickCount()).empty(), "unsupported layout does not invoke the reader");
    CaptureWorldMapMarkers(reinterpret_cast<uintptr_t>(state.data()), now); ClearLiveMarkers();
    Check(CopyWorldMapMarkers(now).empty(), "world exit clears world-map snapshots");

    ModContext config{};
    config.config.GetString = [](const char*, const char* key, std::string fallback) {
        if (std::strcmp(key, "map_light") == 0) return std::string("150");
        if (std::strcmp(key, "heading_smoothing_ms") == 0) return std::string("0");
        if (std::strcmp(key, "icon_style") == 0) return std::string("world-map");
        if (std::strcmp(key, "show_clock") == 0) return std::string("false");
        if (std::strcmp(key, "show_fog_of_war") == 0) return std::string("false");
        if (std::strcmp(key, "show_weather") == 0) return std::string("false");
        if (std::strcmp(key, "show_death_markers") == 0) return std::string("false");
        if (std::strcmp(key, "show_world_markers") == 0) return std::string("false");
        return fallback;
    };
    RefreshMinimapConfig(&config);
    Check(g_minimapMapLight == 100 && g_headingSmoothingMs == 0 && g_worldMapIconStyle && !g_showWorldMarkers,
        "runtime config enables gold icons and applies bounded display options");
    Check(!g_showDeathMarkers, "runtime config hides death markers");
    Check(!g_showClock, "runtime config hides world clock");
    Check(!g_showFogOfWar, "fog option applies live without reinstalling");
    Check(!g_showWeather, "runtime config hides weather without reinstalling");
    Check(g_showOtherPlayers && g_showPings && g_showWaypoints, "world-map option preserves independent live-marker settings");
    config.config.GetString = [](const char*, const char* key, std::string fallback) {
        return std::strcmp(key, "heading_smoothing_ms") == 0 ? std::string("-1") : fallback;
    };
    RefreshMinimapConfig(&config);
    Check(g_headingSmoothingMs == 55, "invalid negative smoothing restores its default");
    Check(ParseConfigInteger("1.5", 55, 0, 100) == 55 && ParseConfigInteger("150junk", 55, 0, 100) == 55,
        "numeric config rejects punctuation and trailing garbage");
    Check(ParseConfigInteger("999999999999999999999", 55, 0, 100) == 100 && ParseConfigInteger(" 0 ", 55, 0, 100) == 0,
        "numeric config handles large values and surrounding whitespace");
    RefreshMinimapConfig(nullptr);
    Check(g_minimapMapLight == 55 && g_headingSmoothingMs == 55 && !g_worldMapIconStyle && g_showWorldMarkers,
        "original icons and default display settings are restored without reinstalling");

    Check(g_showDeathMarkers, "death markers default to enabled without configuration");
    Check(g_showClock, "world clock defaults to enabled without configuration");
    Check(g_showFogOfWar, "fog of war defaults to ON without configuration");
    Check(g_showWeather, "weather defaults to enabled without configuration");

    float r = 0.2f, g = 0.3f, b = 0.4f;
    ApplyMapLighting(r, g, b, 0.0f, 0);
    Check(std::fabs(r - 0.208f) < 0.0001f && std::fabs(b - 0.412f) < 0.0001f, "zero lighting retains original terrain treatment");
    float brightR = 0.2f, brightG = 0.3f, brightB = 0.4f;
    ApplyMapLighting(brightR, brightG, brightB, 0.0f, 55);
    Check(brightR > r && brightG > g && brightB > b && brightR <= 1, "default parchment lighting lifts terrain colors");
    r = g = b = 1.0f; ApplyMapLighting(r, g, b, 1.0f, 500);
    Check(r <= 1 && g <= 1 && b <= 1 && r > b, "lighting clamps input and warms the edge");
    VulkanMinimapRenderer renderer{};
    renderer.width = 512; renderer.height = 320; renderer.fns.cmdClearAttachments = Raster;
    for (unsigned dpi : {96U,120U,144U,192U}) for (bool on : {false,true})
    {
        std::memset(pixels,0,sizeof(pixels));
        DrawMinimapNotice(renderer,nullptr,on,dpi);
        int textPixels=0;for(auto& row:pixels) for(auto& pixel:row) if(pixel.r>0.8f && pixel.g>0.8f) ++textPixels;
        Check(textPixels>40,"popup text renders at common Windows display scales");
        Check(pixels[213][256-120*dpi/96].g>0.5f,"popup border appears at the expected position");
    }
    std::memset(pixels,0,sizeof(pixels));
    CheckDeaths(renderer);
    CheckWeather(renderer);
    CheckWorldClock(renderer);
    CheckLiveFog(renderer);
    CheckFoggedIcons(renderer);
    CheckCustomPinOutlines(renderer);
    CheckIndependentPanels(renderer);
    CheckCombinedWeatherSymbols(renderer);
    Check(SmoothMapHeading(renderer, 3.1f, true, 100) == 3.1f, "first heading is immediate");
    Check(std::abs(SmoothMapHeading(renderer, -3.1f, true, 116)) > 3, "heading smoothing crosses the short arc at north");
    Check(SmoothMapHeading(renderer, 1, true, 2000) == 1, "stale heading resets after a pause");
    g_headingSmoothingMs = 0;
    Check(SmoothMapHeading(renderer, 2, true, 2001) == 2, "zero smoothing follows the camera immediately");
    g_headingSmoothingMs = 55; ClearLiveMarkers();
    Check(SmoothMapHeading(renderer, -1, true, 2002) == -1, "world re-entry resets smoothing");
    Check(SmoothMapHeading(renderer, 2, false, 2003) == 0, "missing heading returns to north");
    Check(MINIMAP_MAX_ZOOM == 7 && MINIMAP_MIN_ZOOM == -3, "zoom range includes four additional close-up steps");

    MinimapIcon glyph{}; glyph.width = glyph.height = 8; glyph.rgba.resize(8 * 8 * 4);
    for (int i : { 9, 10, 17, 18 }) for (int c = 0; c < 4; ++c) glyph.rgba[i * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(std::count(glyph.glyph.begin(), glyph.glyph.end(), 1) == 4, "transparent white glyph keeps its alpha shape");
    glyph.rgba.assign(8 * 8 * 4, 255); for (int c = 0; c < 3; ++c) glyph.rgba[9 * 4 + c] = 0;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "dark glyph on an opaque light tile is preserved");
    glyph.rgba.assign(8 * 8 * 4, 0); for (int i = 0; i < 64; ++i) glyph.rgba[i * 4 + 3] = 255;
    for (int c = 0; c < 3; ++c) glyph.rgba[9 * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "light glyph on an opaque dark tile is preserved");
    for (int y = 1; y < 7; ++y) for (int x = 1; x < 7; ++x)
        for (int c = 0; c < 3; ++c) glyph.rgba[(y * 8 + x) * 4 + c] = 255;
    PrepareMapIconGlyph(glyph);
    Check(glyph.glyph[9] == 1 && glyph.glyph[0] == 0, "large light glyph is not mistaken for the tile background");
    g_worldMapIconStyle = true;
    Check(TryDrawMinimapRasterIcon(renderer, nullptr, 64, 64, 40, 100, 64, iconKey, true), "actual atlas icon draws in the world-map style");
    bool outside = false;
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x)
        if ((x-64)*(x-64) + (y-64)*(y-64) > 40*40 && pixels[y][x].r != 0) outside = true;
    Check(!outside, "gold diamond and glyph stay clipped to the circular map");
    g_worldMapIconStyle = false;
    Check(TryDrawMinimapRasterIcon(renderer, nullptr, 64, 64, 40, 64, 64, iconKey, false), "original icon style remains available");
    if (argc > 1) CheckClientLayout(argv[1]);
    if (argc > 2)
    {
        std::memset(pixels, 0, sizeof(pixels));
        g_worldMapIconStyle = true;
        Check(TryLoadRealMapFromPath("assets/embervale_realmap_1024.rgba", g_realMap), "load real terrain for CPU preview");
        g_realMap.attempted = true;
        DrawRealMap(renderer, nullptr, 160, 160, 130, 2200, 2200, 2.0f, 0);
        for (int i = 0; i < 5; ++i)
            CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 95 + i * 33, 130, (nativeIcon + i)->key, false);
        CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 120, 195, 11, false);
        CmdClearPoiIcon(renderer, nullptr, 160, 160, 130, 195, 195, 12, false);
        DrawWaypointDiamond(renderer, nullptr, 160, 160); DrawPingDiamond(renderer, nullptr, 195, 90);
        DrawDeathMarker(renderer, nullptr, 160, 160, 130, 85, 185, 0x1AD8F96E);
        // Separate compact preview of both clock states using production draws.
        WritePreview(argv[2]);
        std::memset(pixels, 0, sizeof(pixels));
        WorldClockSnapshot clock{}; clock.valid = true; clock.minuteOfDay = 9 * 60 + 42; clock.daytime = true;
        DrawWorldClockPanel(renderer, nullptr, 256, 80, clock);
        clock.minuteOfDay = 23 * 60 + 7; clock.daytime = false;
        DrawWorldClockPanel(renderer, nullptr, 256, 145, clock);
        WritePreview((std::string(argv[2]) + ".clock.bmp").c_str());
        // Preview every day/night state through the production panel renderer.
        for (bool day : {true,false})
        {
            std::memset(pixels,0,sizeof(pixels));
            for (int i=0;i<4;++i)
            {
                WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=static_cast<std::uint8_t>(i);
                clock.daytime=day; clock.minuteOfDay=day?12*60+35:23*60+7;
                DrawWorldClockPanel(renderer,nullptr,256,40+i*75,clock,weather);
            }
            WritePreview((std::string(argv[2])+(day?".weather-day.bmp":".weather-night.bmp")).c_str());
        }
        std::memset(pixels, 0, sizeof(pixels));
        Check(TryLoadMinimapFrameFromPath("assets/embervale_minimap_frame.rgba", g_minimapFrame), "load actual compass frame for clock preview");
        g_minimapFrame.attempted = true;
        const int previewRadius = 90, previewExtra = MinimapRasterFrameExtra(previewRadius);
        DrawRealMap(renderer, nullptr, 256, 138, MinimapRasterMapRadius(previewRadius, previewExtra), 2200, 2200, 2, 0);
        TryDrawMinimapRasterFrame(renderer, nullptr, 256, 138, previewRadius, previewExtra);
        DrawPingDiamond(renderer, nullptr, 300, 100);
        DrawWaypointDiamond(renderer, nullptr, 212, 173);
        CmdClearPlayerArrow(renderer, nullptr, 256, 138, previewRadius);
        clock.minuteOfDay = 12 * 60 + 35; clock.daytime = true;
        const int previewHalf = MinimapRasterVisibleHalfHeight(previewRadius, previewExtra);
        const int previewYOffset = -MaxValue(1, (2 * (previewRadius + previewExtra) + 128) / 256);
        WorldWeatherSnapshot weather{}; weather.valid=true; weather.kind=1;
        DrawWorldClockPanel(renderer, nullptr, 256, 138 + previewHalf + previewYOffset + CLOCK_FRAME_GAP + CLOCK_PANEL_HALF_HEIGHT, clock,weather);
        WritePreview((std::string(argv[2]) + ".combined.bmp").c_str());
    }
    return 0;
}
