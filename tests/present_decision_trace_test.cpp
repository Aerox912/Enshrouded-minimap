#include "../src/dllmain.cpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace MinimapPresentDecision;

int RunCompileOutProbe();

namespace
{
    int failures = 0;
    void Check(bool passed, const char* label);
    std::int32_t fakeFenceStatus = VK_SUCCESS;
    std::int32_t fakeCommandReset = VK_SUCCESS;
    std::int32_t fakeCommandBegin = VK_SUCCESS;
    std::int32_t fakeCommandEnd = VK_SUCCESS;
    std::int32_t fakeFenceResetResult = VK_SUCCESS;
    std::int32_t fakeQueueSubmitResult = VK_SUCCESS;
    int fakeFenceResetCalls = 0;
    int fakeQueueSubmitCalls = 0;
    std::uint32_t fakeSubmittedWaitCount = 0;
    std::uint32_t fakeSubmittedSignalCount = 0;
    int testRefreshCalls = 0;
    int testNoticeCalls = 0;
    int testDrawCalls = 0;
    bool testShouldDraw = false;
    std::vector<std::string> hookLogLines;
    int hookOriginalCalls = 0;
    std::size_t hookLogLinesAtOriginal = 0;
    const void* hookRawPresentInfo = nullptr;
    void* hookExpectedQueue = nullptr;
    bool hookExpectAdjusted = false;

    std::int32_t __fastcall TraceFenceStatus(void*, void*)
    {
        return fakeFenceStatus;
    }

    std::int32_t __fastcall TraceCommandReset(void*, std::uint32_t)
    {
        return fakeCommandReset;
    }

    std::int32_t __fastcall TraceCommandBegin(void*, const VkCommandBufferBeginInfo*)
    {
        return fakeCommandBegin;
    }

    std::int32_t __fastcall TraceCommandEnd(void*)
    {
        return fakeCommandEnd;
    }

    void __fastcall TraceBeginRenderPass(void*, const VkRenderPassBeginInfo*, std::uint32_t)
    {
    }

    void __fastcall TraceEndRenderPass(void*)
    {
    }

    std::int32_t __fastcall TraceFenceReset(void*, std::uint32_t, void* const*)
    {
        ++fakeFenceResetCalls;
        return fakeFenceResetResult;
    }

    std::int32_t __fastcall TraceQueueSubmit(void*, std::uint32_t submitCount,
        const VkSubmitInfo* submits, void*)
    {
        ++fakeQueueSubmitCalls;
        if (submitCount == 1 && submits != nullptr)
        {
            fakeSubmittedWaitCount = submits[0].waitSemaphoreCount;
            fakeSubmittedSignalCount = submits[0].signalSemaphoreCount;
        }
        return fakeQueueSubmitResult;
    }

    void TestRefreshCachedCamera()
    {
        ++testRefreshCalls;
    }

    bool TestReadNotice(bool& on, AdmissionTrace*)
    {
        ++testNoticeCalls;
        on = false;
        return false;
    }

    bool TestShouldDraw(bool* visibleObserved, bool* gameplayObserved, AdmissionTrace*)
    {
        ++testDrawCalls;
        if (visibleObserved != nullptr) *visibleObserved = testShouldDraw;
        if (gameplayObserved != nullptr) *gameplayObserved = testShouldDraw;
        return testShouldDraw;
    }

    void TestUpdateRuntimeControls(void*, bool*)
    {
    }

    void TestScanVulkanDeviceFunctions()
    {
    }

    void TestTraceLogLine(const char* line)
    {
        hookLogLines.emplace_back(line != nullptr ? line : "<null>");
    }

    std::int32_t __fastcall TestOriginalQueuePresent(void* queue, const void* presentInfo)
    {
        ++hookOriginalCalls;
        hookLogLinesAtOriginal = hookLogLines.size();
        Check(queue == hookExpectedQueue, "actual HookQueuePresent forwards the original queue");
        if (hookExpectAdjusted)
        {
            Check(presentInfo != hookRawPresentInfo, "actual HookQueuePresent selects adjusted present info on success");
            const auto* adjusted = static_cast<const VkPresentInfoKHR*>(presentInfo);
            Check(adjusted->waitSemaphoreCount == 1 && adjusted->pWaitSemaphores != nullptr &&
                adjusted->pWaitSemaphores[0] == reinterpret_cast<void*>(0x400),
                "actual adjusted present info uses the renderer completion semaphore");
        }
        else
        {
            Check(presentInfo == hookRawPresentInfo, "actual HookQueuePresent preserves raw info on rejection");
        }
        return 73;
    }

    void PrepareFakeSubmission()
    {
        g_renderer = VulkanMinimapRenderer{};
        g_renderer.ready = true;
        g_renderer.device = 0x100;
        g_renderer.renderPass = 0x500;
        g_renderer.width = 32;
        g_renderer.height = 32;
        g_renderer.commandBuffers = { 0x200 };
        g_renderer.framebuffers = { 0x300 };
        g_renderer.renderCompleteSemaphores = { 0x400 };
        g_renderer.submissionFences = { 0x600 };
        g_renderer.fns.getFenceStatus = TraceFenceStatus;
        g_renderer.fns.resetCommandBuffer = TraceCommandReset;
        g_renderer.fns.beginCommandBuffer = TraceCommandBegin;
        g_renderer.fns.endCommandBuffer = TraceCommandEnd;
        g_renderer.fns.cmdBeginRenderPass = TraceBeginRenderPass;
        g_renderer.fns.cmdEndRenderPass = TraceEndRenderPass;
        g_renderer.fns.resetFences = TraceFenceReset;
        g_renderer.fns.queueSubmit = TraceQueueSubmit;
        fakeFenceStatus = VK_SUCCESS;
        fakeCommandReset = VK_SUCCESS;
        fakeCommandBegin = VK_SUCCESS;
        fakeCommandEnd = VK_SUCCESS;
        fakeFenceResetResult = VK_SUCCESS;
        fakeQueueSubmitResult = VK_SUCCESS;
        fakeFenceResetCalls = 0;
        fakeQueueSubmitCalls = 0;
        fakeSubmittedWaitCount = 0;
        fakeSubmittedSignalCount = 0;
    }

    void PrepareActualHookSubmission()
    {
        constexpr uintptr_t device = 0xA00;
        constexpr uintptr_t swapchain = 0xA03;
        g_swapchains.clear();
        g_lastVulkanDevice.store(device);
        g_patchedVulkanDevice.store(0);
        g_client10 = false;

        VkSwapchainCreateInfoKHR create{};
        create.minImageCount = 2;
        create.imageFormat = 44;
        create.imageColorSpace = 100;
        create.imageExtent = { 32, 32 };
        RememberSwapchainCreate(reinterpret_cast<void*>(device), &create,
            reinterpret_cast<void*>(swapchain), VK_SUCCESS);

        SwapchainRuntimeInfo snapshot{};
        Check(TryGetSwapchainSnapshot(swapchain, snapshot), "actual HookQueuePresent fixture captures its local swapchain");
        PrepareFakeSubmission();
        g_renderer.device = snapshot.device;
        g_renderer.swapchain = snapshot.handle;
        g_renderer.swapchainGeneration = snapshot.generation;
        g_renderer.format = snapshot.format;
        g_renderer.colorSpace = snapshot.colorSpace;
        g_renderer.width = snapshot.width;
        g_renderer.height = snapshot.height;
        g_renderer.images = snapshot.images;
        g_renderer.submissionFailed = false;
        g_minimapVisible.store(false);
        g_renderCameraFallbackEnabled.store(false);
        g_runtimeCallbacksActive.store(true);

        g_presentDecisionTestHooks.refreshCachedCamera = TestRefreshCachedCamera;
        g_presentDecisionTestHooks.readNotice = TestReadNotice;
        g_presentDecisionTestHooks.shouldDraw = TestShouldDraw;
        g_presentDecisionTestHooks.updateRuntimeControls = TestUpdateRuntimeControls;
        g_presentDecisionTestHooks.scanVulkanDeviceFunctions = TestScanVulkanDeviceFunctions;
        g_presentDecisionTestHooks.logPresentTraceLine = TestTraceLogLine;
    }

    void CheckActualLateSubmitCallbacks()
    {
        VkPresentInfoKHR info{};
        VkPresentInfoKHR adjusted{};
        const void* adjustedWait[1]{};
        void* queue = reinterpret_cast<void*>(0x700);
        g_presentDecisionTestHooks.readNotice = TestReadNotice;
        g_minimapVisible.store(false);

        PrepareFakeSubmission();
        fakeCommandEnd = -21;
        Trace commandEnd{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &commandEnd) &&
            commandEnd.firstRejection == Rejection::CommandEndFailed &&
            commandEnd.recordStage == RecordStage::End && commandEnd.recordResult == -21 &&
            fakeFenceResetCalls == 0 && fakeQueueSubmitCalls == 0,
            "actual guarded recording callback reports command-end failure before fence reset or submit");

        PrepareFakeSubmission();
        fakeFenceResetResult = -22;
        Trace fenceReset{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &fenceReset) &&
            fenceReset.firstRejection == Rejection::FenceResetFailed &&
            fenceReset.fenceResetResultObserved == Observation::Yes && fenceReset.fenceResetResult == -22 &&
            fakeFenceResetCalls == 1 && fakeQueueSubmitCalls == 0,
            "actual fence-reset callback reports failure and skips queue submit");

        PrepareFakeSubmission();
        fakeQueueSubmitResult = -23;
        Trace queueSubmit{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &queueSubmit) &&
            queueSubmit.firstRejection == Rejection::QueueSubmitFailed &&
            queueSubmit.submitResultObserved == Observation::Yes && queueSubmit.submitResult == -23 &&
            fakeFenceResetCalls == 1 && fakeQueueSubmitCalls == 1 && fakeSubmittedSignalCount == 1,
            "actual queue-submit callback records its result after successful recording and fence reset");

        PrepareFakeSubmission();
        Trace adjustedSuccess{};
        Check(SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &adjustedSuccess) &&
            adjustedSuccess.adjustedPrepared == Observation::Yes && adjusted.waitSemaphoreCount == 1 &&
            adjusted.pWaitSemaphores == adjustedWait && adjustedWait[0] == reinterpret_cast<void*>(0x400) &&
            fakeQueueSubmitCalls == 1,
            "actual fake callback chain prepares adjusted present info after successful submit");
    }

    void CheckActualHookQueuePresent()
    {
        PrepareActualHookSubmission();
        hookLogLines.clear();
        hookOriginalCalls = 0;
        hookLogLinesAtOriginal = 0;
        hookExpectedQueue = reinterpret_cast<void*>(0x700);
        g_originalQueuePresent.store(reinterpret_cast<uintptr_t>(&TestOriginalQueuePresent));
        g_debugLoggingEnabled.store(false);

        const void* swapchains[] = { reinterpret_cast<void*>(0xA03) };
        const std::uint32_t imageIndices[] = { 0 };
        VkPresentInfoKHR raw{};
        raw.waitSemaphoreCount = 1;
        raw.pWaitSemaphores = nullptr;
        raw.swapchainCount = 1;
        raw.pSwapchains = swapchains;
        raw.pImageIndices = imageIndices;
        hookRawPresentInfo = &raw;
        hookExpectAdjusted = false;
        testShouldDraw = true;
        testRefreshCalls = 0;
        testNoticeCalls = 0;
        testDrawCalls = 0;
        g_lastVulkanPresentInfoTick = GetTickCount() - 5000;
        const std::int32_t rejectedResult = HookQueuePresent(hookExpectedQueue, &raw);
        Check(rejectedResult == 73 && hookOriginalCalls == 1 && hookLogLinesAtOriginal == 0,
            "actual sampled HookQueuePresent invokes original once before emitting a rejected decision");
        Check(testDrawCalls == 1 && testNoticeCalls == 1 && testRefreshCalls == 1,
            "actual invalid-wait path reaches admission callbacks once before rejecting semaphores");
        Check(hookLogLines.size() == 4 && !hookLogLines.empty() &&
            hookLogLines[0].find("reject=wait-semaphores-invalid") != std::string::npos &&
            hookLogLines[0].find("adjusted=n") != std::string::npos,
            "actual HookQueuePresent emits the bounded raw-present rejection after the original callback");

        hookLogLines.clear();
        hookOriginalCalls = 0;
        hookLogLinesAtOriginal = 0;
        hookExpectAdjusted = true;
        raw.waitSemaphoreCount = 0;
        raw.pWaitSemaphores = nullptr;
        hookRawPresentInfo = &raw;
        testShouldDraw = true;
        g_lastVulkanPresentInfoTick = GetTickCount() - 5000;
        const std::int32_t adjustedResult = HookQueuePresent(hookExpectedQueue, &raw);
        Check(adjustedResult == 73 && hookOriginalCalls == 1 && hookLogLinesAtOriginal == 0,
            "actual sampled successful HookQueuePresent invokes original once before emitting its decision");
        Check(hookLogLines.size() == 4 && !hookLogLines.empty() &&
            hookLogLines[0].find("reject=none") != std::string::npos &&
            hookLogLines[0].find("adjusted=y") != std::string::npos &&
            hookLogLines[0].find("present=y:73") != std::string::npos,
            "actual HookQueuePresent emits the adjusted success and original result after presentation");

        hookLogLines.clear();
        hookOriginalCalls = 0;
        hookLogLinesAtOriginal = 0;
        hookExpectAdjusted = false;
        testShouldDraw = false;
        const std::int32_t unsampledResult = HookQueuePresent(hookExpectedQueue, &raw);
        Check(unsampledResult == 73 && hookOriginalCalls == 1 && hookLogLinesAtOriginal == 0 &&
            hookLogLines.empty(), "unsampled callback keeps one raw original present and emits no trace lines");

        g_presentDecisionTestHooks = {};
        g_runtimeCallbacksActive.store(false);
        g_originalQueuePresent.store(0);
    }

    void CheckActualInputGates()
    {
        VkPresentInfoKHR info{};
        VkPresentInfoKHR adjusted{};
        const void* adjustedWait[1]{};
        void* queue = reinterpret_cast<void*>(0x700);

        g_runtimeCallbacksActive.store(false);
        Trace inactive{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0x900, 0, adjusted, adjustedWait, false, &inactive) &&
            inactive.firstRejection == Rejection::RuntimeInactive,
            "actual present-submit seam reports inactive runtime before later gates");

        g_runtimeCallbacksActive.store(true);
        Trace nullQueue{};
        Check(!TrySubmitVulkanMinimap(nullptr, info, 0x900, 0, adjusted, adjustedWait, false, &nullQueue) &&
            nullQueue.firstRejection == Rejection::QueueNull &&
            nullQueue.swapchainValid == Observation::NotReached,
            "actual present-submit seam marks short-circuited swapchain gate not-reached");

        Trace zeroSwapchain{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0, 0, adjusted, adjustedWait, false, &zeroSwapchain) &&
            zeroSwapchain.firstRejection == Rejection::SwapchainZero,
            "actual present-submit seam reports a zero swapchain");

        info.waitSemaphoreCount = 9;
        Trace waitLimit{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0x900, 0, adjusted, adjustedWait, false, &waitLimit) &&
            waitLimit.firstRejection == Rejection::WaitCountTooHigh && waitLimit.waitSemaphoreCount == 9,
            "actual present-submit seam reports excessive wait semaphore count before snapshot access");
        info.waitSemaphoreCount = 0;

        g_swapchains.clear();
        Trace unknown{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0x901, 0, adjusted, adjustedWait, false, &unknown) &&
            unknown.firstRejection == Rejection::SnapshotUnknown,
            "actual present-submit seam reports an unknown swapchain snapshot");

        std::uint32_t imageCount = 0;
        RememberSwapchainImages(reinterpret_cast<void*>(0x100), reinterpret_cast<void*>(0x902),
            &imageCount, nullptr, VK_SUCCESS);
        g_lastVulkanDevice.store(0x100);
        Trace noProvenance{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0x902, 0, adjusted, adjustedWait, false, &noProvenance) &&
            noProvenance.firstRejection == Rejection::CreateProvenanceMissing,
            "actual present-submit seam distinguishes an image-only record from a successful create record");

        VkSwapchainCreateInfoKHR create{};
        create.minImageCount = 2;
        create.imageFormat = 44;
        create.imageColorSpace = 100;
        create.imageExtent = { 32, 32 };
        RememberSwapchainCreate(reinterpret_cast<void*>(0x110), &create,
            reinterpret_cast<void*>(0x903), VK_SUCCESS);
        g_lastVulkanDevice.store(0x111);
        Trace deviceMismatch{};
        Check(!TrySubmitVulkanMinimap(queue, info, 0x903, 0, adjusted, adjustedWait, false, &deviceMismatch) &&
            deviceMismatch.createProvenance == Observation::Yes &&
            deviceMismatch.firstRejection == Rejection::DeviceMismatch,
            "actual present-submit seam reports device mismatch after verified create provenance");
        g_runtimeCallbacksActive.store(false);
    }

    void CheckActualRendererGates()
    {
        VkPresentInfoKHR info{};
        VkPresentInfoKHR adjusted{};
        const void* adjustedWait[1]{};
        void* queue = reinterpret_cast<void*>(0x700);

        g_renderer = VulkanMinimapRenderer{};
        Trace notReady{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &notReady) &&
            notReady.firstRejection == Rejection::RendererNotReady,
            "actual renderer submit seam reports a renderer that is not ready");

        PrepareFakeSubmission();
        Trace outOfRange{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 1, adjusted, adjustedWait, false, &outOfRange) &&
            outOfRange.firstRejection == Rejection::ImageIndexOutOfRange &&
            outOfRange.fenceStatusObserved == Observation::NotReached,
            "actual renderer submit seam short-circuits image index before the fence query");

        PrepareFakeSubmission();
        g_renderer.submissionFailed = true;
        Trace latchedFailure{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &latchedFailure) &&
            latchedFailure.firstRejection == Rejection::RendererSubmissionFailed,
            "actual renderer submit seam reports the latched submission failure");

        PrepareFakeSubmission();
        fakeFenceStatus = VK_NOT_READY;
        Trace busy{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &busy) &&
            busy.firstRejection == Rejection::FenceBusy && busy.fenceStatusObserved == Observation::Yes &&
            busy.recordStage == RecordStage::NotReached,
            "actual fake fence callback reports busy without recording or resetting");

        PrepareFakeSubmission();
        fakeFenceStatus = -9;
        Trace fenceError{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &fenceError) &&
            fenceError.firstRejection == Rejection::FenceStatusFailed && fenceError.fenceStatus == -9,
            "actual fake fence callback reports the observed fence error result");

        PrepareFakeSubmission();
        fakeCommandReset = -11;
        Trace commandReset{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &commandReset) &&
            commandReset.firstRejection == Rejection::CommandResetFailed &&
            commandReset.recordStage == RecordStage::Reset && commandReset.recordResult == -11,
            "actual fake command reset callback reports the first recording failure");

        PrepareFakeSubmission();
        fakeCommandBegin = -12;
        Trace commandBegin{};
        Check(!SubmitVulkanMinimapFrameLocked(queue, info, 0, adjusted, adjustedWait, false, &commandBegin) &&
            commandBegin.firstRejection == Rejection::CommandBeginFailed &&
            commandBegin.recordStage == RecordStage::Begin && commandBegin.recordResult == -12,
            "actual fake command begin callback reports the first recording failure");

        SwapchainRuntimeInfo missingCreate{};
        Trace build{};
        Check(!BuildVulkanMinimapRendererLocked(missingCreate, &build) &&
            build.buildStage == BuildStage::Snapshot && build.buildResultObserved == Observation::No,
            "actual renderer build seam identifies the existing provenance guard stage");
    }

    void Check(bool passed, const char* label)
    {
        if (!passed)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", label);
        }
    }
}

int main(int argc, char** argv)
{
    g_debugLoggingEnabled.store(false);
    g_lastVulkanPresentInfoTick = GetTickCount() - 5000;
    Check(ShouldSamplePresentDecisionTrace(),
        "diagnostic sample is automatic when debug logging is disabled");

    g_runtimeCallbacksActive.store(false);
    g_debugLoggingEnabled.store(false);
    g_vulkanDeviceTable.store(0);
    g_lastVulkanDevice.store(0);
    g_minimapToggleKey.store(0);
    g_minimapToggleSecondary.store(0);
    g_minimapZoomInKey.store(0);
    g_minimapZoomInSecondary.store(0);
    g_minimapZoomOutKey.store(0);
    g_minimapZoomOutSecondary.store(0);
    g_minimapSizeDecreaseKey.store(0);
    g_minimapSizeIncreaseKey.store(0);
    if (argc == 2 && std::strcmp(argv[1], "--r2-focused") == 0)
    {
        CheckActualLateSubmitCallbacks();
        CheckActualHookQueuePresent();
        Check(RunCompileOutProbe() == 0,
            "trace-only macro compiles out its side effects in a normal translation unit");
        if (failures != 0)
        {
            std::fprintf(stderr, "%d focused R2 decision-trace checks failed\n", failures);
            return 1;
        }
        std::printf("present decision R2 focused checks passed\n");
        return 0;
    }
    CheckActualInputGates();
    CheckActualRendererGates();
    CheckActualLateSubmitCallbacks();
    CheckActualHookQueuePresent();

    Trace trace{};
    Check(trace.firstRejection == Rejection::None, "trace starts without a rejection");
    Check(trace.fallback.hudHook == Observation::NotReached, "unvisited HUD hook remains not-reached");
    Check(trace.notice.cursorRead == Observation::NotReached, "short-circuited cursor remains not-reached");

    MinimapMenu10::Snapshot snapshot;
    snapshot.Capture(1, 1000, true);
    std::uint64_t packed = UINT64_MAX;
    const bool allowed = snapshot.AllowsGameplay(1500, &packed);
    CaptureHud(trace.draw, true, packed, 1500, MinimapMenu10::MaxAgeMs, allowed);
    Check(allowed, "HUD admission accepts the exact 500ms boundary");
    Check(trace.draw.screen == 1, "trace records the screen from the decision snapshot");
    Check(trace.draw.ageMs == 500, "trace records age from the decision tick");
    Check(trace.draw.hudAllowed == Observation::Yes, "trace records the decision result");

    packed = UINT64_MAX;
    const bool stale = !snapshot.AllowsGameplay(1501, &packed);
    CaptureHud(trace.notice, true, packed, 1501, MinimapMenu10::MaxAgeMs, !stale);
    Check(stale, "HUD admission rejects a stale snapshot");
    Check(trace.notice.ageMs == 501, "stale trace reports the same decision age");
    Check(trace.notice.hudAllowed == Observation::No, "stale trace records the decision rejection");
    Check(trace.notice.firstRejection == AdmissionRejection::HudStale,
        "stale HUD observation identifies its first admission rejection");

    MinimapMenu10::Snapshot nonGameSnapshot;
    nonGameSnapshot.Capture(2, 2000, true);
    packed = 0;
    const bool nonGame = !nonGameSnapshot.AllowsGameplay(2000, &packed);
    AdmissionTrace nonGameAdmission{};
    CaptureHud(nonGameAdmission, true, packed, 2000, MinimapMenu10::MaxAgeMs, nonGame);
    Check(nonGame && nonGameAdmission.screen == 2, "non-game HUD screen is observed from the decision snapshot");
    Check(nonGameAdmission.hudAge == Observation::NotReached,
        "non-game screen short-circuits the age check just like the admission predicate");
    Check(nonGameAdmission.firstRejection == AdmissionRejection::HudScreenMismatch,
        "non-game HUD screen identifies the first admission rejection");

    AdmissionTrace missingHud{};
    CaptureHud(missingHud, true, 0, 3000, MinimapMenu10::MaxAgeMs, false);
    Check(missingHud.hudAllowed == Observation::No &&
        missingHud.firstRejection == AdmissionRejection::HudSnapshotMissing,
        "missing HUD snapshot reports the actual failed admission");

    AdmissionTrace inactiveHud{};
    CaptureHud(inactiveHud, false, 0, 3000, MinimapMenu10::MaxAgeMs, false);
    Check(inactiveHud.hudSnapshot == Observation::NotReached &&
        inactiveHud.firstRejection == AdmissionRejection::HudHookInactive,
        "inactive HUD hook leaves the snapshot not reached");

    AdmissionTrace futureHud{};
    CaptureHud(futureHud, true, (std::uint64_t{4000} << 8) | 1, 3999,
        MinimapMenu10::MaxAgeMs, false);
    Check(futureHud.hudAge == Observation::Error &&
        futureHud.firstRejection == AdmissionRejection::HudTimestampFuture,
        "future HUD timestamp is distinguished from a stale snapshot");

    CaptureCursor(trace.draw, true, false);
    CapturePose(trace.draw, true);
    Check(trace.draw.cursorRead == Observation::Yes, "cursor success is captured without another query");
    Check(trace.draw.cursorShowing == Observation::No, "cursor visibility is captured from the same result");
    Check(trace.draw.poseFresh == Observation::Yes, "fresh pose result is captured from the same check");

    AdmissionTrace cursorBlocked{};
    CaptureCursor(cursorBlocked, true, true);
    Check(cursorBlocked.firstRejection == AdmissionRejection::CursorShowing,
        "visible cursor rejection is captured from the single cursor result");

    AdmissionTrace poseBlocked{};
    CapturePose(poseBlocked, false);
    Check(poseBlocked.firstRejection == AdmissionRejection::PoseStale,
        "stale pose rejection is captured from the single freshness check");

    trace.RejectFirst(Rejection::NoEligibleContent);
    trace.RejectFirst(Rejection::QueueSubmitFailed);
    Check(trace.firstRejection == Rejection::NoEligibleContent, "first rejection is sticky");

    Trace gateTrace{};
    int gateOrder = 0;
    const bool runtimeReady = EvaluateGate(&gateTrace, Rejection::RuntimeInactive, [&]()
    {
        gateOrder = 1;
        return true;
    });
    const bool snapshotKnown = runtimeReady && EvaluateGate(&gateTrace, Rejection::SnapshotUnknown, [&]()
    {
        gateOrder = 2;
        return false;
    });
    const bool deviceMatch = snapshotKnown && EvaluateGate(&gateTrace, Rejection::DeviceMismatch, [&]()
    {
        gateOrder = 3;
        return true;
    });
    Check(!snapshotKnown && !deviceMatch, "a rejected seam gate short-circuits later checks");
    Check(gateOrder == 2, "seam gates preserve order and evaluate each reached callback once");
    Check(gateTrace.firstRejection == Rejection::SnapshotUnknown, "seam reports the first rejected gate");
    Check(gateTrace.queueValid == Observation::NotReached &&
        gateTrace.waitCountValid == Observation::NotReached,
        "short-circuited gates retain distinct not-reached values");

    Trace presentTrace{};
    presentTrace.adjustedUsed = Observation::Yes;
    int originalCalls = 0;
    int emitCalls = 0;
    const std::int32_t presentResult = CompleteAfterOriginalPresent(presentTrace,
        [&](bool adjusted)
        {
            ++originalCalls;
            Check(adjusted, "present seam forwards adjusted-info selection");
            return -7;
        },
        [&](const Trace& completed)
        {
            ++emitCalls;
            Check(originalCalls == 1, "trace emission follows one original present invocation");
            Check(completed.presentResultObserved == Observation::Yes && completed.presentResult == -7,
                "trace contains the original present result before emission");
        });
    Check(originalCalls == 1 && emitCalls == 1 && presentResult == -7,
        "present seam invokes original once and emits once");

    FormattedSample formatted{};
    Check(FormatSample(trace, formatted), "sample formatter succeeds");
    Check(formatted.count == 4, "sample uses four fixed lines including the schema legend");
    for (std::size_t line = 0; line < formatted.count; ++line)
        Check(std::strlen(formatted.lines[line]) <= kLoggerPayloadLimit, "formatted line stays within logger limit");
    Check(std::strstr(formatted.lines[0], "pd1") != nullptr, "first line identifies the versioned schema");
    Check(std::strstr(formatted.lines[0], "no-eligible-content") != nullptr, "output names the first rejection");
    Check(std::strstr(formatted.lines[3], "fence=-:0") != nullptr &&
        std::strstr(formatted.lines[3], "submit=-:0") != nullptr,
        "unreached Vulkan scalar results are explicitly marked rather than presented as observed zero");
    Check(RunCompileOutProbe() == 0, "trace-only macro compiles out its side effects in a normal translation unit");

    if (failures != 0)
    {
        std::fprintf(stderr, "%d focused decision-trace checks failed\n", failures);
        return 1;
    }
    std::printf("present decision trace checks passed\n");
    return 0;
}
