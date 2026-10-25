#pragma once

#if defined(ESM_PRESENT_DECISION_TRACE)
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace MinimapPresentDecision
{
    constexpr std::size_t kLoggerPayloadLimit = 400;
    constexpr std::size_t kSampleLineCount = 4;
    constexpr std::size_t kSampleLineStorage = kLoggerPayloadLimit + 1;

    enum class Observation : std::uint8_t
    {
        NotReached,
        No,
        Yes,
        Error
    };

    enum class Rejection : std::uint8_t
    {
        None,
        PresentInfoNull,
        PresentInfoUnreadable,
        PresentTargetsInvalid,
        RuntimeInactive,
        QueueNull,
        SwapchainZero,
        WaitCountTooHigh,
        SnapshotUnknown,
        CreateProvenanceMissing,
        DeviceMismatch,
        NoEligibleContent,
        InvalidWaitSemaphores,
        DeactivatedAfterLock,
        RendererBuildFailed,
        RendererNotReady,
        RendererSubmissionFailed,
        ImageIndexOutOfRange,
        FenceBusy,
        FenceStatusFailed,
        CommandResetFailed,
        CommandBeginFailed,
        CommandEndFailed,
        RecordingAccessViolation,
        FenceResetFailed,
        QueueSubmitFailed
    };

    enum class AdmissionRejection : std::uint8_t
    {
        None,
        Hidden,
        FocusLost,
        HudHookInactive,
        HudSnapshotMissing,
        HudScreenMismatch,
        HudTimestampFuture,
        HudStale,
        HudRejected,
        CursorShowing,
        PoseStale
    };

    enum class BuildStage : std::uint8_t
    {
        NotReached,
        Reused,
        Snapshot,
        DeviceMatch,
        DeviceFunctions,
        SwapchainImages,
        ImageCount,
        CommandPool,
        RenderPass,
        TexturedFrame,
        ImageView,
        Framebuffer,
        CommandBuffers,
        Semaphore,
        Fence,
        Ready
    };

    enum class RecordStage : std::uint8_t
    {
        NotReached,
        Reset,
        Begin,
        End,
        AccessViolation,
        Complete
    };

    struct AdmissionTrace
    {
        Observation focus = Observation::NotReached;
        Observation visible = Observation::NotReached;
        Observation hudHook = Observation::NotReached;
        Observation hudSnapshot = Observation::NotReached;
        Observation hudAge = Observation::NotReached;
        Observation hudAllowed = Observation::NotReached;
        Observation cursorRead = Observation::NotReached;
        Observation cursorShowing = Observation::NotReached;
        Observation poseFresh = Observation::NotReached;
        Observation admitted = Observation::NotReached;
        AdmissionRejection firstRejection = AdmissionRejection::None;
        std::uint8_t screen = 0;
        std::uint64_t ageMs = 0;

        void RejectFirst(AdmissionRejection reason) noexcept
        {
            if (firstRejection == AdmissionRejection::None)
                firstRejection = reason;
        }
    };

    struct Trace
    {
        Rejection firstRejection = Rejection::None;
        Observation presentInfo = Observation::NotReached;
        Observation presentTargets = Observation::NotReached;
        Observation runtimeActive = Observation::NotReached;
        Observation queueValid = Observation::NotReached;
        Observation swapchainValid = Observation::NotReached;
        Observation waitCountValid = Observation::NotReached;
        Observation snapshotKnown = Observation::NotReached;
        Observation createProvenance = Observation::NotReached;
        Observation deviceMatch = Observation::NotReached;
        Observation fallbackInitialPose = Observation::NotReached;
        Observation fallbackEnabled = Observation::NotReached;
        Observation fallbackCaptureAttempted = Observation::NotReached;
        Observation noticeVisible = Observation::NotReached;
        Observation noticeOn = Observation::NotReached;
        Observation contentEligible = Observation::NotReached;
        Observation waitSemaphoresValid = Observation::NotReached;
        Observation activeAfterRendererLock = Observation::NotReached;
        Observation rendererReady = Observation::NotReached;
        Observation rendererSubmissionFailed = Observation::NotReached;
        Observation rendererBuildTextured = Observation::NotReached;
        Observation buildResultObserved = Observation::NotReached;
        Observation fenceStatusObserved = Observation::NotReached;
        Observation recordResultObserved = Observation::NotReached;
        Observation fenceResetResultObserved = Observation::NotReached;
        Observation submitResultObserved = Observation::NotReached;
        Observation commandIndexValid = Observation::NotReached;
        Observation framebufferIndexValid = Observation::NotReached;
        Observation completionSemaphoreIndexValid = Observation::NotReached;
        Observation fenceIndexValid = Observation::NotReached;
        Observation adjustedPrepared = Observation::NotReached;
        Observation adjustedUsed = Observation::NotReached;
        Observation presentResultObserved = Observation::NotReached;
        std::uint32_t waitSemaphoreCount = 0;
        std::uint32_t swapchainCount = 0;
        std::uint32_t imageIndex = 0;
        std::uint32_t snapshotFormat = 0;
        std::uint32_t snapshotWidth = 0;
        std::uint32_t snapshotHeight = 0;
        std::uint32_t snapshotImageCount = 0;
        std::uint32_t snapshotCachedImageCount = 0;
        std::int32_t buildResult = 0;
        std::int32_t fenceStatus = 0;
        std::int32_t recordResult = 0;
        std::int32_t fenceResetResult = 0;
        std::int32_t submitResult = 0;
        std::int32_t presentResult = 0;
        BuildStage buildStage = BuildStage::NotReached;
        RecordStage recordStage = RecordStage::NotReached;
        AdmissionTrace fallback{};
        AdmissionTrace notice{};
        AdmissionTrace draw{};
        AdmissionTrace recordNotice{};

        void RejectFirst(Rejection reason) noexcept
        {
            if (firstRejection == Rejection::None)
                firstRejection = reason;
        }
    };

    template <typename Condition>
    inline bool EvaluateGate(Trace* trace, Rejection reason, Condition&& condition)
    {
        const bool passed = condition();
        if (trace != nullptr && !passed)
            trace->RejectFirst(reason);
        return passed;
    }

    template <typename OriginalPresent, typename EmitTrace>
    inline std::int32_t CompleteAfterOriginalPresent(Trace& trace,
        OriginalPresent&& originalPresent, EmitTrace&& emitTrace)
    {
        const bool useAdjusted = trace.adjustedUsed == Observation::Yes;
        const std::int32_t result = originalPresent(useAdjusted);
        trace.presentResult = result;
        trace.presentResultObserved = Observation::Yes;
        emitTrace(trace);
        return result;
    }

    struct FormattedSample
    {
        char lines[kSampleLineCount][kSampleLineStorage]{};
        std::size_t count = 0;
    };

    inline Observation Observe(bool value) noexcept
    {
        return value ? Observation::Yes : Observation::No;
    }

    inline void CaptureHud(AdmissionTrace& admission, bool hookActive,
        std::uint64_t packed, std::uint64_t now, std::uint64_t maxAgeMs, bool allowed) noexcept
    {
        admission.hudHook = Observe(hookActive);
        if (!hookActive)
        {
            admission.RejectFirst(AdmissionRejection::HudHookInactive);
            return;
        }

        admission.hudSnapshot = Observe(packed != 0);
        admission.hudAllowed = Observe(allowed);
        if (packed == 0)
        {
            admission.RejectFirst(AdmissionRejection::HudSnapshotMissing);
            return;
        }

        const std::uint64_t captured = packed >> 8;
        admission.screen = static_cast<std::uint8_t>(packed & 0xff);
        if (admission.screen != 1)
        {
            admission.RejectFirst(AdmissionRejection::HudScreenMismatch);
            return;
        }
        if (now < captured)
        {
            admission.hudAge = Observation::Error;
            admission.RejectFirst(AdmissionRejection::HudTimestampFuture);
            return;
        }

        admission.ageMs = now - captured;
        admission.hudAge = Observation::Yes;
        if (admission.ageMs > maxAgeMs)
        {
            admission.RejectFirst(AdmissionRejection::HudStale);
            return;
        }
        if (!allowed)
            admission.RejectFirst(AdmissionRejection::HudRejected);
    }

    inline void CaptureCursor(AdmissionTrace& admission, bool readSucceeded,
        bool showing) noexcept
    {
        admission.cursorRead = Observe(readSucceeded);
        if (readSucceeded)
        {
            admission.cursorShowing = Observe(showing);
            if (showing)
                admission.RejectFirst(AdmissionRejection::CursorShowing);
        }
    }

    inline void CapturePose(AdmissionTrace& admission, bool fresh) noexcept
    {
        admission.poseFresh = Observe(fresh);
        if (!fresh)
            admission.RejectFirst(AdmissionRejection::PoseStale);
    }

    inline const char* AdmissionRejectionCode(AdmissionRejection value) noexcept
    {
        switch (value)
        {
        case AdmissionRejection::Hidden: return "hidden";
        case AdmissionRejection::FocusLost: return "focus";
        case AdmissionRejection::HudHookInactive: return "hud-hook";
        case AdmissionRejection::HudSnapshotMissing: return "hud-missing";
        case AdmissionRejection::HudScreenMismatch: return "hud-screen";
        case AdmissionRejection::HudTimestampFuture: return "hud-future";
        case AdmissionRejection::HudStale: return "hud-stale";
        case AdmissionRejection::HudRejected: return "hud-rejected";
        case AdmissionRejection::CursorShowing: return "cursor";
        case AdmissionRejection::PoseStale: return "pose-stale";
        default: return "none";
        }
    }

    inline char Code(Observation value) noexcept
    {
        switch (value)
        {
        case Observation::No: return 'n';
        case Observation::Yes: return 'y';
        case Observation::Error: return 'e';
        default: return '-';
        }
    }

    inline const char* RejectionName(Rejection value) noexcept
    {
        switch (value)
        {
        case Rejection::PresentInfoNull: return "present-info-null";
        case Rejection::PresentInfoUnreadable: return "present-info-unreadable";
        case Rejection::PresentTargetsInvalid: return "present-targets-invalid";
        case Rejection::RuntimeInactive: return "runtime-inactive";
        case Rejection::QueueNull: return "queue-null";
        case Rejection::SwapchainZero: return "swapchain-zero";
        case Rejection::WaitCountTooHigh: return "wait-count-high";
        case Rejection::SnapshotUnknown: return "snapshot-unknown";
        case Rejection::CreateProvenanceMissing: return "create-provenance-missing";
        case Rejection::DeviceMismatch: return "device-mismatch";
        case Rejection::NoEligibleContent: return "no-eligible-content";
        case Rejection::InvalidWaitSemaphores: return "wait-semaphores-invalid";
        case Rejection::DeactivatedAfterLock: return "deactivated-after-lock";
        case Rejection::RendererBuildFailed: return "renderer-build-failed";
        case Rejection::RendererNotReady: return "renderer-not-ready";
        case Rejection::ImageIndexOutOfRange: return "image-index-out-of-range";
        case Rejection::FenceBusy: return "fence-busy";
        case Rejection::FenceStatusFailed: return "fence-status-failed";
        case Rejection::CommandResetFailed: return "command-reset-failed";
        case Rejection::CommandBeginFailed: return "command-begin-failed";
        case Rejection::CommandEndFailed: return "command-end-failed";
        case Rejection::RecordingAccessViolation: return "record-access-violation";
        case Rejection::FenceResetFailed: return "fence-reset-failed";
        case Rejection::QueueSubmitFailed: return "queue-submit-failed";
        default: return "none";
        }
    }

    inline const char* BuildStageName(BuildStage value) noexcept
    {
        switch (value)
        {
        case BuildStage::Reused: return "reuse";
        case BuildStage::Snapshot: return "snapshot";
        case BuildStage::DeviceMatch: return "device-match";
        case BuildStage::DeviceFunctions: return "device-functions";
        case BuildStage::SwapchainImages: return "swapchain-images";
        case BuildStage::ImageCount: return "image-count";
        case BuildStage::CommandPool: return "command-pool";
        case BuildStage::RenderPass: return "render-pass";
        case BuildStage::TexturedFrame: return "textured-frame";
        case BuildStage::ImageView: return "image-view";
        case BuildStage::Framebuffer: return "framebuffer";
        case BuildStage::CommandBuffers: return "command-buffers";
        case BuildStage::Semaphore: return "semaphore";
        case BuildStage::Fence: return "fence";
        case BuildStage::Ready: return "ready";
        default: return "nr";
        }
    }

    inline const char* RecordStageName(RecordStage value) noexcept
    {
        switch (value)
        {
        case RecordStage::Reset: return "reset";
        case RecordStage::Begin: return "begin";
        case RecordStage::End: return "end";
        case RecordStage::AccessViolation: return "access-violation";
        case RecordStage::Complete: return "complete";
        default: return "nr";
        }
    }

    inline int WriteLine(char* buffer, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(buffer, kSampleLineStorage, format, args);
        va_end(args);
        return length;
    }

    inline bool FormatSample(const Trace& trace, FormattedSample& formatted) noexcept
    {
        formatted = {};
        const int first = WriteLine(formatted.lines[0],
            "[Minimap] pd1 legend: -=not-reached,n=no,y=yes,e=error | reject=%s | adjusted=%c | present=%c:%d",
            RejectionName(trace.firstRejection), Code(trace.adjustedUsed),
            Code(trace.presentResultObserved), trace.presentResult);
        const int second = WriteLine(formatted.lines[1],
            "[Minimap] pd1 input pinfo=%c active=%c queue=%c target=%c waitcnt=%c snapshot=%c provenance=%c device=%c | waits=%u swaps=%u image=%u extent=%ux%u fmt=%u images=%u/%u | renderer ready=%c subfail=%c stage=%s result=%c:%d textured=%c",
            Code(trace.presentInfo), Code(trace.runtimeActive), Code(trace.queueValid), Code(trace.presentTargets),
            Code(trace.waitCountValid), Code(trace.snapshotKnown), Code(trace.createProvenance),
            Code(trace.deviceMatch), trace.waitSemaphoreCount, trace.swapchainCount,
            trace.imageIndex, trace.snapshotWidth, trace.snapshotHeight, trace.snapshotFormat,
            trace.snapshotImageCount, trace.snapshotCachedImageCount, Code(trace.rendererReady),
            Code(trace.rendererSubmissionFailed), BuildStageName(trace.buildStage),
            Code(trace.buildResultObserved), trace.buildResult, Code(trace.rendererBuildTextured));
        const int third = WriteLine(formatted.lines[2],
            "[Minimap] pd1 admission fallback{%c%c%c%c%c%c%c%c%c%c%c%c%c age=%llu screen=%u reject=%s} notice{%c%c%c%c%c%c%c%c%c%c age=%llu screen=%u reject=%s}",
            Code(trace.fallback.focus), Code(trace.fallback.visible), Code(trace.fallback.hudHook),
            Code(trace.fallback.hudSnapshot), Code(trace.fallback.hudAge), Code(trace.fallback.hudAllowed),
            Code(trace.fallback.cursorRead), Code(trace.fallback.cursorShowing), Code(trace.fallback.poseFresh),
            Code(trace.fallback.admitted), Code(trace.fallbackInitialPose), Code(trace.fallbackEnabled),
            Code(trace.fallbackCaptureAttempted),
            static_cast<unsigned long long>(trace.fallback.ageMs), trace.fallback.screen,
            AdmissionRejectionCode(trace.fallback.firstRejection),
            Code(trace.notice.focus), Code(trace.notice.visible), Code(trace.notice.hudHook),
            Code(trace.notice.hudSnapshot), Code(trace.notice.hudAge), Code(trace.notice.hudAllowed),
            Code(trace.notice.cursorRead), Code(trace.notice.cursorShowing), Code(trace.notice.poseFresh),
            Code(trace.notice.admitted),
            static_cast<unsigned long long>(trace.notice.ageMs), trace.notice.screen,
            AdmissionRejectionCode(trace.notice.firstRejection));
        const int fourth = WriteLine(formatted.lines[3],
            "[Minimap] pd1 draw{%c%c%c%c%c%c%c%c%c%c age=%llu screen=%u reject=%s} record{%c%c%c%c%c%c%c%c%c%c age=%llu screen=%u reject=%s} index=%c%c%c%c fence=%c:%d record=%s/%c:%d reset=%c:%d submit=%c:%d prepared=%c content=%c notice=%c/%c adjusted=%c present=%c:%d",
            Code(trace.draw.focus), Code(trace.draw.visible), Code(trace.draw.hudHook),
            Code(trace.draw.hudSnapshot), Code(trace.draw.hudAge), Code(trace.draw.hudAllowed),
            Code(trace.draw.cursorRead), Code(trace.draw.cursorShowing), Code(trace.draw.poseFresh),
            Code(trace.draw.admitted), static_cast<unsigned long long>(trace.draw.ageMs), trace.draw.screen,
            AdmissionRejectionCode(trace.draw.firstRejection),
            Code(trace.recordNotice.focus), Code(trace.recordNotice.visible), Code(trace.recordNotice.hudHook),
            Code(trace.recordNotice.hudSnapshot), Code(trace.recordNotice.hudAge), Code(trace.recordNotice.hudAllowed),
            Code(trace.recordNotice.cursorRead), Code(trace.recordNotice.cursorShowing),
            Code(trace.recordNotice.poseFresh), Code(trace.recordNotice.admitted),
            static_cast<unsigned long long>(trace.recordNotice.ageMs), trace.recordNotice.screen,
            AdmissionRejectionCode(trace.recordNotice.firstRejection),
            Code(trace.commandIndexValid), Code(trace.framebufferIndexValid),
            Code(trace.completionSemaphoreIndexValid), Code(trace.fenceIndexValid),
            Code(trace.fenceStatusObserved), trace.fenceStatus,
            RecordStageName(trace.recordStage), Code(trace.recordResultObserved), trace.recordResult,
            Code(trace.fenceResetResultObserved), trace.fenceResetResult,
            Code(trace.submitResultObserved), trace.submitResult, Code(trace.adjustedPrepared),
            Code(trace.contentEligible), Code(trace.noticeVisible), Code(trace.noticeOn),
            Code(trace.adjustedUsed), Code(trace.presentResultObserved), trace.presentResult);
        const int lengths[kSampleLineCount] = { first, second, third, fourth };
        for (std::size_t index = 0; index < kSampleLineCount; ++index)
        {
            if (lengths[index] < 0 || static_cast<std::size_t>(lengths[index]) > kLoggerPayloadLimit)
                return false;
        }
        formatted.count = kSampleLineCount;
        return true;
    }
}

#define MINIMAP_PD_TRACE_PARAM(type, name) , type name = nullptr
#define MINIMAP_PD_TRACE_ARG(name) , name
#define MINIMAP_PD_TRACE_ONLY_PARAM(type, name) type name = nullptr
#define MINIMAP_PD_TRACE_ONLY_ARG(name) name
#define MINIMAP_PD_TRACE_IF(trace, expression) do { if ((trace) != nullptr) { expression; } } while (false)
#define MINIMAP_PD_GATE(trace, rejection, expression) \
    (((trace) != nullptr) ? \
        ::MinimapPresentDecision::EvaluateGate((trace), (rejection), [&]() { return (expression); }) : \
        (expression))
#define MINIMAP_PD_TRACE_STORAGE(name) std::optional<::MinimapPresentDecision::Trace> name
#else
#define MINIMAP_PD_TRACE_PARAM(type, name)
#define MINIMAP_PD_TRACE_ARG(name)
#define MINIMAP_PD_TRACE_ONLY_PARAM(type, name)
#define MINIMAP_PD_TRACE_ONLY_ARG(name)
#define MINIMAP_PD_TRACE_IF(trace, expression) ((void)0)
#define MINIMAP_PD_GATE(trace, rejection, expression) (expression)
#define MINIMAP_PD_TRACE_STORAGE(name)
#endif
