#include "../src/dllmain.cpp"
#include <iostream>
#include <thread>
#include <future>
#include <unordered_map>

namespace
{
    std::unordered_map<int, SHORT> menuTestKeyStates;
    std::atomic<int> menuRendererCalls{0};
    bool menuFinalizeTestCalled = false;
    bool menuFinalizeTestSawPublishedPatch = false;
    bool menuFinalizeTestRestoredProtection = false;

    bool SimulateMenuCallsiteCacheFlushFailure(uintptr_t alignedAddress, bool replaced, DWORD previousProtection)
    {
        menuFinalizeTestCalled = true;
        menuFinalizeTestSawPublishedPatch = replaced;
        DWORD ignoredProtection = 0;
        menuFinalizeTestRestoredProtection = VirtualProtect(
            reinterpret_cast<void*>(alignedAddress), sizeof(std::uint64_t),
            previousProtection, &ignoredProtection) != FALSE;
        const bool simulatedCacheFlushSucceeded = !replaced;
        return menuFinalizeTestRestoredProtection && simulatedCacheFlushSucceeded;
    }

    void CountMenuRendererCall()
    {
        ++menuRendererCalls;
    }

    SHORT WINAPI PollMenuTestKey(int key)
    {
        const auto found = menuTestKeyStates.find(key);
        if (found == menuTestKeyStates.end()) return 0;
        const SHORT state = found->second;
        found->second = static_cast<SHORT>(found->second & static_cast<SHORT>(~1));
        return state;
    }

    bool MakeSyntheticCall(uintptr_t callsite, uintptr_t target, std::uint8_t (&call)[5])
    {
        const auto relative = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(callsite + 5);
        if (relative < INT32_MIN || relative > INT32_MAX) return false;
        call[0] = 0xE8;
        const auto relative32 = static_cast<std::int32_t>(relative);
        std::memcpy(call + 1, &relative32, sizeof(relative32));
        return true;
    }

    bool SyntheticCallEquals(uintptr_t callsite, const std::uint8_t (&expected)[5])
    {
        std::uint8_t actual[5]{};
        return SafeRead(callsite, actual, sizeof(actual)) && std::memcmp(actual, expected, sizeof(actual)) == 0;
    }

    void DestroySyntheticCallsiteHook(Mem::Detour*& hook)
    {
        if (!hook) return;
        void* relay = hook->shellcode && hook->shellcode->data
            ? reinterpret_cast<void*>(hook->shellcode->data->address) : nullptr;
        // This isolated test has no executing caller; free only its synthetic relay.
        delete hook->shellcode;
        delete hook->patch;
        delete hook;
        hook = nullptr;
        if (relay) VirtualFree(relay, 0, MEM_RELEASE);
    }

    void ConfigureSyntheticCallsiteHook(Mem::Detour* hook)
    {
        const std::uint8_t callOpcode = 0xE8;
        hook->patch->updateOpcode(0, const_cast<std::uint8_t*>(&callOpcode), 1);
    }
}

int main() {
    std::atomic<int> readers{0};
    std::atomic<bool> entered{false}, overlap{false};
    static std::atomic<int>* readersPtr=&readers;
    static std::atomic<bool>* enteredPtr=&entered;
    static std::atomic<bool>* overlapPtr=&overlap;
    ModContext config{};
    config.config.GetString=[](const char*,const char*,std::string fallback) {
        if (++*readersPtr > 1) *overlapPtr=true;
        g_lastConfigPollTick=0; // Force both callers to poll during this test.
        *enteredPtr=true;
        Sleep(2);
        --*readersPtr;
        return fallback;
    };
    g_lastConfigPollTick=0;
    std::thread worker([&]{UpdateMinimapRuntimeControls(&config);});
    while (!entered.load()) std::this_thread::yield();
    std::thread present([&]{UpdateMinimapRuntimeControls(&config);});
    worker.join();present.join();
    if (overlap) {std::cerr<<"FAIL: worker and present overlap the controls/configuration critical section\n";return 1;}
    std::cout<<"PASS: worker and present serialize the full controls/configuration update\n";
    static std::atomic<bool> paused{false}, resume{false};
    config.config.GetString=[](const char*,const char*,std::string fallback) {
        paused=true;
        while (!resume.load()) std::this_thread::yield();
        return fallback;
    };
    g_lastConfigPollTick=0;
    std::thread reconfigure([&]{UpdateMinimapRuntimeControls(&config);});
    while(!paused.load()) std::this_thread::yield();
    auto reset=std::async(std::launch::async,[]{ResetWorldSessionFromLog();});
    const bool resetOverlapped=reset.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    resume=true;reconfigure.join();reset.get();
    if(resetOverlapped) {std::cerr<<"FAIL: session reset overlaps a configuration/control update\n";return 1;}
    std::cout<<"PASS: session reset serializes with configuration and hotkeys\n";
    std::array<std::uint8_t,CLIENT_CAMERA_SIZE> camera{};
    const auto cameraAddress=reinterpret_cast<uintptr_t>(camera.data());
    g_runtimeCallbacksActive=true; g_minimapVisible=false; g_playerCameraAddress=cameraAddress;
    VkPresentInfoKHR info{},adjusted{};const void* semaphore=nullptr;
    TrySubmitVulkanMinimap(reinterpret_cast<void*>(1),info,1,0,adjusted,&semaphore);
    if (g_playerCameraAddress!=cameraAddress) {std::cerr<<"FAIL: hidden minimap still probes the fallback camera\n";return 1;}
    std::cout<<"PASS: hidden minimap skips fallback camera probing\n";
    g_staticPois.attempted=g_staticPois.loaded=true;
    g_staticPois.pois={{0,0,0,1}};
    g_fogOfWar={};
    std::vector<MinimapWorldPoint> points;
    AppendVisibleStaticPoiPoints(points,{}, {},5120,5120,100);
    if (g_fogOfWar.attempted) {std::cerr<<"FAIL: distant fallback POIs trigger fog work\n";return 1;}
    std::cout<<"PASS: distant fallback POIs are rejected before fog work\n";

    g_minimapToggleKey = VK_F10;
    g_minimapToggleSecondary = 0;
    g_minimapZoomInKey = VK_F11;
    g_minimapZoomInSecondary = 0;
    g_minimapZoomOutKey = VK_F12;
    g_minimapZoomOutSecondary = 0;
    g_minimapSizeDecreaseKey = VK_F7;
    g_minimapSizeIncreaseKey = VK_F8;
    g_minimapVisible = true;
    g_minimapZoomStep = 4;
    g_minimapScalePercent = 100;
    g_sizeDecreaseDown = false;
    g_sizeIncreaseDown = false;
    menuTestKeyStates.clear();
    menuTestKeyStates[VK_F10] = 1;
    menuTestKeyStates[VK_F11] = 1;
    menuTestKeyStates[VK_F7] = static_cast<SHORT>(0x8000);

    bool menuInputOk = true;
    const auto checkMenuInput = [&menuInputOk](bool condition, const char* message) {
        if (condition) std::cout << "PASS: " << message << '\n';
        else { std::cerr << "FAIL: " << message << '\n'; menuInputOk = false; }
    };
    UpdateMinimapVisibilityHotkey(PollMenuTestKey, false);
    UpdateMinimapZoomHotkeys(PollMenuTestKey, false);
    UpdateMinimapSizeHotkeys(false, PollMenuTestKey);
    checkMenuInput(g_minimapVisible.load(), "menu-time visibility press does not toggle the minimap");
    checkMenuInput(g_minimapZoomStep.load() == 4, "menu-time zoom press does not change zoom");
    checkMenuInput(g_minimapScalePercent.load() == 100, "menu-time held size key does not resize the minimap");
    checkMenuInput(g_sizeDecreaseDown, "menu-time size polling records the held key edge");

    const bool visibleAfterMenu = g_minimapVisible.load();
    const int zoomAfterMenu = g_minimapZoomStep.load();
    const int scaleAfterMenu = g_minimapScalePercent.load();
    UpdateMinimapVisibilityHotkey(PollMenuTestKey, true);
    UpdateMinimapZoomHotkeys(PollMenuTestKey, true);
    UpdateMinimapSizeHotkeys(true, PollMenuTestKey);
    checkMenuInput(g_minimapVisible.load() == visibleAfterMenu &&
        g_minimapZoomStep.load() == zoomAfterMenu && g_minimapScalePercent.load() == scaleAfterMenu,
        "menu-time key presses and held size input do not replay on return to gameplay");
    if (!menuInputOk) return 1;

    bool menuCallsiteOwnershipTestsOk = true;
    const auto checkMenuCallsite = [&menuCallsiteOwnershipTestsOk](bool condition, const char* message) {
        if (condition) std::cout << "PASS: " << message << '\n';
        else { std::cerr << "FAIL: " << message << '\n'; menuCallsiteOwnershipTestsOk = false; }
    };
    const auto syntheticTarget = reinterpret_cast<uintptr_t>(&CountMenuRendererCall);
    auto* rejectedCode = static_cast<std::uint8_t*>(Mem::AllocateMemoryNearAddress(
        reinterpret_cast<LPVOID>(syntheticTarget), 0x1000));
    if (!rejectedCode) {
        std::cerr << "FAIL: allocate synthetic menu callsite for pre-activation ownership test\n";
        return 1;
    }
    const uintptr_t rejectedCallsite = reinterpret_cast<uintptr_t>(rejectedCode + 0x100);
    const uintptr_t foreignTarget = reinterpret_cast<uintptr_t>(rejectedCode + 0x200);
    std::uint8_t expectedCall[5]{}, foreignCall[5]{};
    if (!MakeSyntheticCall(rejectedCallsite, syntheticTarget, expectedCall) ||
        !MakeSyntheticCall(rejectedCallsite, foreignTarget, foreignCall)) {
        std::cerr << "FAIL: encode synthetic relative menu calls\n";
        VirtualFree(rejectedCode, 0, MEM_RELEASE);
        return 1;
    }
    std::memcpy(rejectedCode + 0x100, expectedCall, sizeof(expectedCall));
    std::uint8_t menuRelay[14] = {0xFF, 0x25, 0, 0, 0, 0};
    const auto menuWrapper = reinterpret_cast<uintptr_t>(&CaptureMenuVisibilityCallsiteHook);
    std::memcpy(menuRelay + 6, &menuWrapper, sizeof(menuWrapper));
    auto* rejectedHook = new Mem::Detour(rejectedCallsite, menuRelay, sizeof(menuRelay), false);
    ConfigureSyntheticCallsiteHook(rejectedHook);
    if (!rejectedHook->shellcode || !rejectedHook->shellcode->data || !rejectedHook->shellcode->data->address) {
        std::cerr << "FAIL: allocate actual Mem::Detour relay for pre-activation ownership test\n";
        DestroySyntheticCallsiteHook(rejectedHook);
        VirtualFree(rejectedCode, 0, MEM_RELEASE);
        return 1;
    }
    Mem::Write(rejectedCallsite, foreignCall, sizeof(foreignCall));
    std::atomic<bool> rejectedEverActivated{false};
    std::atomic<bool> rejectedPublicationFinalized{false};
    int rejectedActivationCalls = 0;
    const bool foreignWasRejected = !ActivateMenuCallsiteOnce(
        rejectedHook, rejectedEverActivated, rejectedPublicationFinalized,
        [rejectedCallsite, syntheticTarget]() { return IsCallsiteCalling(rejectedCallsite, syntheticTarget); },
        [rejectedCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, rejectedCallsite, syntheticTarget, menuWrapper);
        },
        [&rejectedActivationCalls, rejectedCallsite](Mem::Detour* hook) {
            ++rejectedActivationCalls;
            std::uint64_t originalAlignedWord = 0;
            if (!ReadMenuCallsitePreimageWord(rejectedCallsite, originalAlignedWord)) return false;
            return ActivateMenuDetourWithCompareExchange(
                hook, rejectedCallsite, originalAlignedWord, []() {});
        });
    checkMenuCallsite(foreignWasRejected && rejectedActivationCalls == 0 &&
        !rejectedEverActivated.load() && !rejectedHook->active && !rejectedHook->patch->patched &&
        SyntheticCallEquals(rejectedCallsite, foreignCall),
        "foreign callsite bytes reject first Mem::Detour activation without patching");
    DestroySyntheticCallsiteHook(rejectedHook);
    VirtualFree(rejectedCode, 0, MEM_RELEASE);

    auto* raceCode = static_cast<std::uint8_t*>(Mem::AllocateMemoryNearAddress(
        reinterpret_cast<LPVOID>(syntheticTarget), 0x1000));
    if (!raceCode) {
        std::cerr << "FAIL: allocate synthetic menu callsite for compare-exchange race test\n";
        return 1;
    }
    const uintptr_t raceCallsite = reinterpret_cast<uintptr_t>(raceCode + 0x102);
    const uintptr_t raceForeignTarget = reinterpret_cast<uintptr_t>(raceCode + 0x280);
    std::uint8_t raceExpectedCall[5]{}, raceForeignCall[5]{};
    if (!MakeSyntheticCall(raceCallsite, syntheticTarget, raceExpectedCall) ||
        !MakeSyntheticCall(raceCallsite, raceForeignTarget, raceForeignCall)) {
        std::cerr << "FAIL: encode compare-exchange race fixture calls\n";
        VirtualFree(raceCode, 0, MEM_RELEASE);
        return 1;
    }
    for (std::size_t i = 0; i < 8; ++i) raceCode[0x100 + i] = static_cast<std::uint8_t>(0x40 + i);
    std::memcpy(raceCode + 0x102, raceExpectedCall, sizeof(raceExpectedCall));
    std::uint64_t raceOriginalAlignedWord = 0;
    if (!ReadMenuCallsitePreimageWord(raceCallsite, raceOriginalAlignedWord)) {
        std::cerr << "FAIL: read synthetic aligned preimage for compare-exchange race test\n";
        VirtualFree(raceCode, 0, MEM_RELEASE);
        return 1;
    }
    auto* raceHook = new Mem::Detour(raceCallsite, menuRelay, sizeof(menuRelay), false);
    ConfigureSyntheticCallsiteHook(raceHook);
    int raceActivationCalls = 0;
    std::atomic<bool> raceEverActivated{false};
    std::atomic<bool> racePublicationFinalized{false};
    const bool racedCallsiteRejected = !ActivateMenuCallsiteOnce(
        raceHook, raceEverActivated, racePublicationFinalized,
        [raceCallsite, syntheticTarget]() { return IsCallsiteCalling(raceCallsite, syntheticTarget); },
        [raceCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, raceCallsite, syntheticTarget, menuWrapper);
        },
        [&raceActivationCalls, raceCallsite, raceOriginalAlignedWord,
            raceForeignCall](Mem::Detour* hook) {
            ++raceActivationCalls;
            return ActivateMenuDetourWithCompareExchange(
                hook, raceCallsite, raceOriginalAlignedWord,
                [raceCallsite, raceForeignCall]() {
                    Mem::Write(raceCallsite, raceForeignCall, sizeof(raceForeignCall));
                });
        });
    checkMenuCallsite(racedCallsiteRejected && raceActivationCalls == 1 &&
        !raceEverActivated.load() && !raceHook->active && !raceHook->patch->patched &&
        SyntheticCallEquals(raceCallsite, raceForeignCall),
        "foreign replacement after guard and relay preparation is preserved by the atomic write gate");
    DestroySyntheticCallsiteHook(raceHook);
    VirtualFree(raceCode, 0, MEM_RELEASE);

    auto* finalizationCode = static_cast<std::uint8_t*>(Mem::AllocateMemoryNearAddress(
        reinterpret_cast<LPVOID>(syntheticTarget), 0x1000));
    if (!finalizationCode) {
        std::cerr << "FAIL: allocate synthetic menu callsite for post-CAS finalization test\n";
        return 1;
    }
    const uintptr_t finalizationCallsite = reinterpret_cast<uintptr_t>(finalizationCode + 0x102);
    std::uint8_t finalizationExpectedCall[5]{};
    if (!MakeSyntheticCall(finalizationCallsite, syntheticTarget, finalizationExpectedCall)) {
        std::cerr << "FAIL: encode post-CAS finalization fixture call\n";
        VirtualFree(finalizationCode, 0, MEM_RELEASE);
        return 1;
    }
    for (std::size_t i = 0; i < 8; ++i)
        finalizationCode[0x100 + i] = static_cast<std::uint8_t>(0x70 + i);
    std::memcpy(finalizationCode + 0x102, finalizationExpectedCall, sizeof(finalizationExpectedCall));
    std::uint64_t finalizationOriginalAlignedWord = 0;
    if (!ReadMenuCallsitePreimageWord(finalizationCallsite, finalizationOriginalAlignedWord)) {
        std::cerr << "FAIL: read post-CAS finalization fixture preimage\n";
        VirtualFree(finalizationCode, 0, MEM_RELEASE);
        return 1;
    }
    auto* finalizationHook = new Mem::Detour(finalizationCallsite, menuRelay, sizeof(menuRelay), false);
    ConfigureSyntheticCallsiteHook(finalizationHook);
    if (!finalizationHook->shellcode || !finalizationHook->shellcode->data ||
        !finalizationHook->shellcode->data->address) {
        std::cerr << "FAIL: allocate actual Mem::Detour relay for post-CAS finalization test\n";
        DestroySyntheticCallsiteHook(finalizationHook);
        VirtualFree(finalizationCode, 0, MEM_RELEASE);
        return 1;
    }
    std::atomic<bool> finalizationEverPublished{false};
    std::atomic<bool> finalizationPublicationReady{false};
    int finalizationActivationCalls = 0;
    menuFinalizeTestCalled = false;
    menuFinalizeTestSawPublishedPatch = false;
    menuFinalizeTestRestoredProtection = false;
    const auto activateThenReportFinalizationFailure = [&](Mem::Detour* hook) {
        ++finalizationActivationCalls;
        return ActivateMenuDetourWithCompareExchange(
            hook, finalizationCallsite, finalizationOriginalAlignedWord, []() {},
            SimulateMenuCallsiteCacheFlushFailure);
    };
    const bool initialFinalizationReady = ActivateMenuCallsiteOnce(
        finalizationHook, finalizationEverPublished, finalizationPublicationReady,
        []() { return true; },
        [finalizationCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, finalizationCallsite, syntheticTarget, menuWrapper);
        },
        activateThenReportFinalizationFailure);
    const bool retainedCallsiteOwnedAfterFailure = IsRetainedMenuCallsiteOwned(
        finalizationHook, finalizationCallsite, syntheticTarget, menuWrapper);
    const bool retryFinalizationReady = ActivateMenuCallsiteOnce(
        finalizationHook, finalizationEverPublished, finalizationPublicationReady,
        []() { return false; },
        [finalizationCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, finalizationCallsite, syntheticTarget, menuWrapper);
        },
        activateThenReportFinalizationFailure);
    std::uint8_t finalizationPublishedCall[5]{};
    MakeSyntheticCall(finalizationCallsite, finalizationHook->shellcode->data->address,
        finalizationPublishedCall);
    checkMenuCallsite(!initialFinalizationReady && !retryFinalizationReady &&
        finalizationEverPublished.load() && finalizationHook->active && finalizationHook->patch->patched &&
        !finalizationPublicationReady.load() && menuFinalizeTestCalled && menuFinalizeTestSawPublishedPatch &&
        menuFinalizeTestRestoredProtection && retainedCallsiteOwnedAfterFailure && finalizationActivationCalls == 1 &&
        SyntheticCallEquals(finalizationCallsite, finalizationPublishedCall),
        "post-CAS finalization failure keeps the retained owned relay but refuses capture on retry");
    Mem::Write(finalizationCallsite, finalizationExpectedCall, sizeof(finalizationExpectedCall));
    DestroySyntheticCallsiteHook(finalizationHook);
    VirtualFree(finalizationCode, 0, MEM_RELEASE);

    auto* retainedCode = static_cast<std::uint8_t*>(Mem::AllocateMemoryNearAddress(
        reinterpret_cast<LPVOID>(syntheticTarget), 0x1000));
    if (!retainedCode) {
        std::cerr << "FAIL: allocate synthetic menu callsite for retained ownership test\n";
        return 1;
    }
    const uintptr_t retainedCallsite = reinterpret_cast<uintptr_t>(retainedCode + 0x102);
    const uintptr_t retainedForeignTarget = reinterpret_cast<uintptr_t>(retainedCode + 0x200);
    std::uint8_t retainedExpectedCall[5]{}, retainedForeignCall[5]{};
    if (!MakeSyntheticCall(retainedCallsite, syntheticTarget, retainedExpectedCall) ||
        !MakeSyntheticCall(retainedCallsite, retainedForeignTarget, retainedForeignCall)) {
        std::cerr << "FAIL: encode retained synthetic relative menu calls\n";
        VirtualFree(retainedCode, 0, MEM_RELEASE);
        return 1;
    }
    for (std::size_t i = 0; i < 8; ++i) retainedCode[0x100 + i] = static_cast<std::uint8_t>(0x50 + i);
    std::memcpy(retainedCode + 0x102, retainedExpectedCall, sizeof(retainedExpectedCall));
    std::uint64_t retainedOriginalAlignedWord = 0;
    if (!ReadMenuCallsitePreimageWord(retainedCallsite, retainedOriginalAlignedWord)) {
        std::cerr << "FAIL: read synthetic aligned preimage for valid ownership test\n";
        VirtualFree(retainedCode, 0, MEM_RELEASE);
        return 1;
    }
    auto* retainedRealHook = new Mem::Detour(retainedCallsite, menuRelay, sizeof(menuRelay), false);
    ConfigureSyntheticCallsiteHook(retainedRealHook);
    if (!retainedRealHook->shellcode || !retainedRealHook->shellcode->data || !retainedRealHook->shellcode->data->address) {
        std::cerr << "FAIL: allocate actual Mem::Detour relay for retained ownership test\n";
        DestroySyntheticCallsiteHook(retainedRealHook);
        VirtualFree(retainedCode, 0, MEM_RELEASE);
        return 1;
    }
    std::atomic<bool> retainedEverActivated{false};
    std::atomic<bool> retainedPublicationFinalized{false};
    int retainedActivationCalls = 0;
    const bool firstRetainedActivation = ActivateMenuCallsiteOnce(
        retainedRealHook, retainedEverActivated, retainedPublicationFinalized,
        [retainedCallsite, syntheticTarget]() { return IsCallsiteCalling(retainedCallsite, syntheticTarget); },
        [retainedCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, retainedCallsite, syntheticTarget, menuWrapper);
        },
        [&retainedActivationCalls, retainedCallsite, retainedOriginalAlignedWord](Mem::Detour* hook) {
            ++retainedActivationCalls;
            return ActivateMenuDetourWithCompareExchange(
                hook, retainedCallsite, retainedOriginalAlignedWord, []() {});
        });
    std::uint8_t patchedWord[8]{};
    SafeRead(retainedCallsite - 2, patchedWord, sizeof(patchedWord));
    checkMenuCallsite(firstRetainedActivation && retainedEverActivated.load() && retainedRealHook->active &&
        retainedRealHook->patch->patched && retainedActivationCalls == 1 &&
        patchedWord[0] == 0x50 && patchedWord[1] == 0x51 && patchedWord[7] == 0x57,
        "verified synthetic callsite installs its actual Mem::Detour and preserves adjacent bytes");

    std::uint8_t expectedRelayCall[5]{};
    MakeSyntheticCall(retainedCallsite, retainedRealHook->shellcode->data->address, expectedRelayCall);
    std::uint8_t alteredRelay[14]{};
    std::memcpy(alteredRelay, menuRelay, sizeof(alteredRelay));
    alteredRelay[6] ^= 0x80;
    Mem::Write(retainedRealHook->shellcode->data->address, alteredRelay, sizeof(alteredRelay));
    const bool alteredRelayRejected = !ActivateMenuCallsiteOnce(
        retainedRealHook, retainedEverActivated, retainedPublicationFinalized,
        [retainedCallsite, syntheticTarget]() { return IsCallsiteCalling(retainedCallsite, syntheticTarget); },
        [retainedCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, retainedCallsite, syntheticTarget, menuWrapper);
        },
        [&retainedActivationCalls, retainedCallsite, retainedOriginalAlignedWord](Mem::Detour* hook) {
            ++retainedActivationCalls;
            return ActivateMenuDetourWithCompareExchange(
                hook, retainedCallsite, retainedOriginalAlignedWord, []() {});
        });
    checkMenuCallsite(alteredRelayRejected && retainedActivationCalls == 1 &&
        SyntheticCallEquals(retainedCallsite, expectedRelayCall),
        "altered retained relay fails ownership without changing the callsite");
    Mem::Write(retainedRealHook->shellcode->data->address, menuRelay, sizeof(menuRelay));
    const bool restoredRelayAccepted = ActivateMenuCallsiteOnce(
        retainedRealHook, retainedEverActivated, retainedPublicationFinalized,
        [retainedCallsite, syntheticTarget]() { return IsCallsiteCalling(retainedCallsite, syntheticTarget); },
        [retainedCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, retainedCallsite, syntheticTarget, menuWrapper);
        },
        [&retainedActivationCalls, retainedCallsite, retainedOriginalAlignedWord](Mem::Detour* hook) {
            ++retainedActivationCalls;
            return ActivateMenuDetourWithCompareExchange(
                hook, retainedCallsite, retainedOriginalAlignedWord, []() {});
        });
    checkMenuCallsite(restoredRelayAccepted && retainedActivationCalls == 1,
        "restored retained relay passes ownership without another activation");

    g_runtimeCallbacksActive.store(false, std::memory_order_release);
    g_menuVisibilityHookActive.store(firstRetainedActivation, std::memory_order_release);
    g_menuVisibilityCaptureEpoch.store(0, std::memory_order_release);
    g_menuVisibilitySnapshot.Capture(1, 1000, true);
    const auto retainedAddress = retainedRealHook->shellcode->data->address;
    auto* afterSyntheticDeactivate = DisableMenuCaptureAndRetainCallsite(
        retainedRealHook, g_menuVisibilityHookActive,
        g_menuVisibilityCaptureEpoch, g_menuVisibilitySnapshot);
    menuRendererCalls.store(0);
    g_originalMenuRenderer.store(syntheticTarget, std::memory_order_release);
    CaptureMenuVisibilityCallsiteHook();
    checkMenuCallsite(afterSyntheticDeactivate == retainedRealHook && retainedRealHook->active &&
        retainedRealHook->shellcode->data->address == retainedAddress && retainedActivationCalls == 1 &&
        menuRendererCalls.load() == 1 && !g_menuVisibilityHookActive.load() &&
        !g_menuVisibilitySnapshot.AllowsGameplay(1000),
        "deactivation retains the real relay, clears capture, and late calls forward once");

    Mem::Write(retainedCallsite, retainedForeignCall, sizeof(retainedForeignCall));
    const bool foreignAfterActivationRejected = !ActivateMenuCallsiteOnce(
        retainedRealHook, retainedEverActivated, retainedPublicationFinalized,
        [retainedCallsite, syntheticTarget]() { return IsCallsiteCalling(retainedCallsite, syntheticTarget); },
        [retainedCallsite, syntheticTarget, menuWrapper](Mem::Detour* hook) {
            return IsRetainedMenuCallsiteOwned(hook, retainedCallsite, syntheticTarget, menuWrapper);
        },
        [&retainedActivationCalls, retainedCallsite, retainedOriginalAlignedWord](Mem::Detour* hook) {
            ++retainedActivationCalls;
            return ActivateMenuDetourWithCompareExchange(
                hook, retainedCallsite, retainedOriginalAlignedWord, []() {});
        });
    checkMenuCallsite(foreignAfterActivationRejected && retainedActivationCalls == 1 &&
        retainedRealHook->active && retainedRealHook->patch->patched &&
        retainedRealHook->shellcode->data->address == retainedAddress &&
        SyntheticCallEquals(retainedCallsite, retainedForeignCall),
        "foreign replacement after activation fails ownership without a second patch write");
    CaptureMenuVisibilityCallsiteHook();
    checkMenuCallsite(menuRendererCalls.load() == 2,
        "retained wrapper continues forwarding the original renderer once after ownership loss");
    DisableMenuCaptureAndRetainCallsite(
        retainedRealHook, g_menuVisibilityHookActive,
        g_menuVisibilityCaptureEpoch, g_menuVisibilitySnapshot);
    g_originalMenuRenderer.store(0, std::memory_order_release);
    DestroySyntheticCallsiteHook(retainedRealHook);
    VirtualFree(retainedCode, 0, MEM_RELEASE);
    if (!menuCallsiteOwnershipTestsOk) return 1;

    struct MenuHookLifetimeProbe
    {
        bool active = false;
        int activationCalls = 0;
        int patchWrites = 0;
        int deactivationCalls = 0;
        int* destructions = nullptr;

        explicit MenuHookLifetimeProbe(int* destructionCount) : destructions(destructionCount) {}
        ~MenuHookLifetimeProbe() { ++*destructions; }
        bool activate() { ++activationCalls; ++patchWrites; return active = true; }
        void deactivate() { active = false; ++deactivationCalls; ++patchWrites; }
    };
    int menuHookDestructions = 0;
    auto* retainedMenuHook = new MenuHookLifetimeProbe(&menuHookDestructions);
    auto* originalMenuHook = retainedMenuHook;
    std::atomic<bool> hookEverActivated{false};
    std::atomic<bool> hookPublicationFinalized{false};
    const bool initiallyInstalled = ActivateMenuCallsiteOnce(
        retainedMenuHook, hookEverActivated, hookPublicationFinalized,
        []() { return true; },
        [](MenuHookLifetimeProbe* hook) { return hook->active; },
        [](MenuHookLifetimeProbe* hook) { return hook->activate(); });
    g_menuVisibilityHookActive.store(initiallyInstalled, std::memory_order_release);
    g_menuVisibilityCaptureEpoch.store(0, std::memory_order_release);
    g_menuVisibilitySnapshot.Capture(1, 1000, true);
    if (!initiallyInstalled || !retainedMenuHook->active || retainedMenuHook->patchWrites != 1) {
        std::cerr << "FAIL: first menu UI hook activation installs one callsite patch\n";
        delete retainedMenuHook;
        return 1;
    }
    std::cout << "PASS: first menu UI hook activation installs one callsite patch\n";

    g_runtimeCallbacksActive = false;
    auto* afterDeactivate = DisableMenuCaptureAndRetainCallsite(
        retainedMenuHook, g_menuVisibilityHookActive,
        g_menuVisibilityCaptureEpoch, g_menuVisibilitySnapshot);
    const bool relayRetained = afterDeactivate == originalMenuHook && retainedMenuHook->active &&
        retainedMenuHook->deactivationCalls == 0 && menuHookDestructions == 0 &&
        retainedMenuHook->patchWrites == 1 && !g_menuVisibilityHookActive.load() &&
        !g_menuVisibilitySnapshot.AllowsGameplay(1000);
    if (!relayRetained) {
        std::cerr << "FAIL: deactivation disables capture but retains the active callsite and clears its snapshot\n";
        delete retainedMenuHook;
        return 1;
    }
    std::cout << "PASS: deactivation disables capture but retains the active callsite and clears its snapshot\n";

    menuRendererCalls.store(0);
    g_originalMenuRenderer.store(reinterpret_cast<uintptr_t>(&CountMenuRendererCall));
    CaptureMenuVisibilityCallsiteHook();
    if (menuRendererCalls.load() != 1) {
        std::cerr << "FAIL: retained menu UI relay forwards the original renderer exactly once after deactivation\n";
        delete retainedMenuHook;
        return 1;
    }
    std::cout << "PASS: retained menu UI relay forwards the original renderer exactly once after deactivation\n";

    auto* afterUnload = DisableMenuCaptureAndRetainCallsite(
        retainedMenuHook, g_menuVisibilityHookActive,
        g_menuVisibilityCaptureEpoch, g_menuVisibilitySnapshot);
    CaptureMenuVisibilityCallsiteHook();
    if (afterUnload != originalMenuHook || !retainedMenuHook->active || retainedMenuHook->patchWrites != 1 ||
        menuRendererCalls.load() != 2 || g_originalMenuRenderer.load() != reinterpret_cast<uintptr_t>(&CountMenuRendererCall)) {
        std::cerr << "FAIL: unload retains the relay and its original target for late UI calls\n";
        delete retainedMenuHook;
        return 1;
    }
    std::cout << "PASS: unload retains the relay and its original target for late UI calls\n";

    const bool reactivated = ActivateMenuCallsiteOnce(
        retainedMenuHook, hookEverActivated, hookPublicationFinalized,
        []() { return true; },
        [](MenuHookLifetimeProbe* hook) { return hook->active; },
        [](MenuHookLifetimeProbe* hook) { return hook->activate(); });
    g_menuVisibilityHookActive.store(reactivated, std::memory_order_release);
    g_runtimeCallbacksActive.store(true, std::memory_order_release);
    CaptureMenuVisibilityCallsiteHook();
    if (!reactivated || retainedMenuHook->activationCalls != 1 || retainedMenuHook->patchWrites != 1 ||
        menuRendererCalls.load() != 3) {
        std::cerr << "FAIL: reactivation reuses the installed relay without another patch and forwards once\n";
        g_runtimeCallbacksActive.store(false, std::memory_order_release);
        delete retainedMenuHook;
        return 1;
    }
    std::cout << "PASS: reactivation reuses the installed relay without another patch and forwards once\n";
    g_runtimeCallbacksActive.store(false, std::memory_order_release);
    DisableMenuCaptureAndRetainCallsite(
        retainedMenuHook, g_menuVisibilityHookActive,
        g_menuVisibilityCaptureEpoch, g_menuVisibilitySnapshot);
    g_originalMenuRenderer.store(0, std::memory_order_release);
    delete retainedMenuHook;
    return 0;
}
