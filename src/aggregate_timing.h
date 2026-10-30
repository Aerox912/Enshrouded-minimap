#pragma once

#ifndef ESM_AGGREGATE_TIMING_DIAGNOSTICS
#define ESM_AGGREGATE_TIMING_DIAGNOSTICS 0
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace MinimapAggregateTiming
{
    // The first duration bucket includes valid zero-tick samples: [0, 1] us.
    inline constexpr std::size_t kDurationBucketCount = 12;
    inline constexpr std::size_t kCallbackGapBucketCount = 8;
    inline constexpr std::size_t kOwnershipOutcomeCount = 6;
    inline constexpr std::size_t kMaxFormattedLine = 65536;
    inline constexpr std::uint64_t kMinimumSettledStateSeconds = 2;

    enum class Metric : std::uint8_t
    {
        MenuCallback, OwnershipPreProof, OwnershipOriginalForward, OwnershipPostProof,
        CreativeProofCallbackPre, CreativeProofCallbackPost,
        CreativeProofAdmissionUpdate, CreativeProofOther,
        PresentCallback, PresentCallbackMutexAcquire, PresentLocalWork, OriginalPresent,
        SubmissionGate, CachedCameraRefresh, CameraFallbackCapture,
        FileCacheMutexAcquire, FileCacheMutexHold, RendererMutexAcquire,
        CameraCapture, MarkerCapture, FogCapture, ConfigPoll, LogPoll,
        OverlayRecord, OverlayWidget, OverlayNotice, OverlaySubmit,
        ProofCacheWarmHit, ProofCacheColdAttempt, ProofColdSuccess, ProofColdFailure,
        BackingFileOpenAttempt, BackingFileOpenSuccess, BackingFileOpenFailure,
        BackingFileRead, NullOriginalExit, ForwardAttempt, ForwardReturn, AdjustedPresent,
        InvalidateObservationClear, InvalidateRawAdmission, InvalidateModuleAcquireFailure,
        GateNoRuntime, GateNoVisibleContent, GateNoQueueWaits, GateNoSwapchain,
        GateRendererBuildFailure, QueueSubmitSuccess, QueueSubmitFailure,
        AggregateDrain, AggregateFormat, LoggerSubmission,
        Count
    };

    enum class Visibility : std::uint8_t { Visible, Hidden, Unknown };
    enum class MenuState : std::uint8_t { Gameplay, Menu, Unknown };
    enum class OwnershipStage : std::uint8_t { Pre, Post };
    enum class OwnershipResult : std::uint8_t { Raw, PublicCreative, Rejected };
    enum class StateDisposition : std::uint8_t { Unqualified, VisibleSettled, HiddenSettled };
    enum class ReportLossReason : std::uint8_t { FormatCapacity, InvalidSnapshot };

    // Additive typed-v1 diagnostic schema. Existing overflowTotal remains the
    // lifetime count of any collector loss, with its existing exclusion rule.
    enum class CollectorLossKind : std::uint8_t
    { Saturation, RetryExhaustion, Capacity, InvalidUsage, AccountingFailure, Count };

    enum class LossCounter : std::uint8_t
    {
        Unknown, ProducerVersion, PublicationCarry, CallbackEntries,
        OwnershipPreRaw, OwnershipPrePublic, OwnershipPreRejected,
        OwnershipPostRaw, OwnershipPostPublic, OwnershipPostRejected,
        VisibilityVisible, VisibilityHidden, VisibilityUnknown,
        NoticeInactive, NoticeActive, MenuGameplay, MenuMenu, MenuUnknown,
        TransitionInactive, TransitionActive, StateVersion, StateDrops,
        AttemptedSequence, DrainGeneration, ReportLossTotal, LastReportLossSequence,
        InitializationViolationTotal, ClockFailureTotal, DrainOverlapTotal,
        LossAccountingTotal, MetricDroppedAccounting,
        MetricEntries, MetricCompleted, MetricTimedSamples, MetricTotalTicks,
        MetricTotalUnits, MetricTimingFailures, MetricIncomplete,
        MetricCrossWindow, MetricCrossState, MetricRecursive, MetricOverlap,
        MetricMaxDepth, MetricActive, MetricDurationBucket,
        ScopeTokenCapacity, ThreadDepthCapacity, ThreadDepthOverflow,
        ThreadDepthMismatch, InvalidMetric, InvalidOwnership, InvalidReportLossReason,
        CallbackGapHistogram, TypedLossTotal,
        Count
    };

    inline constexpr std::size_t kCollectorLossKindCount =
        static_cast<std::size_t>(CollectorLossKind::Count);
    static_assert(static_cast<unsigned>(LossCounter::Count) <= 64,
        "Typed loss counter masks must remain fixed-width");

    struct CollectorLossSnapshot
    {
        std::array<std::uint64_t, kCollectorLossKindCount> totals{};
        std::array<std::uint64_t, kCollectorLossKindCount> counterMasks{};
        // One atomic identity of the latest primary fault publication, not a coherent cut
        // with totals/masks: valid bit 63; kind bits 0..2; counter bits 3..8;
        // metric bits 9..16 (255 means no metric); member bits 17..24.
        // Nonrecursive accounting fallback does not replace the primary fault.
        std::uint64_t lastFailureIdentity = 0;
        bool accountingUncertain = false;
        // Accounting failures have their own total/category. These cause bits
        // additionally distinguish saturation from retry exhaustion within
        // the accounting layer without recursively recording primary faults.
        std::uint64_t accountingFailureCauseMask = 0;
        std::uint64_t lastAccountingFailureIdentity = 0;
    };

    enum class StateReason : std::uint32_t
    {
        None = 0,
        UnknownState = 1u << 0,
        MixedWindow = 1u << 1,
        NoticeActive = 1u << 2,
        MenuOrUnknownMenu = 1u << 3,
        Transition = 1u << 4,
        Settling = 1u << 5,
        StateUpdateAtCut = 1u << 6,
        ActiveScopes = 1u << 7,
        CrossStateWork = 1u << 8,
        ReportLoss = 1u << 9,
        StateDrops = 1u << 10,
        ClockFailure = 1u << 11,
        InitializationFailure = 1u << 12
    };

    constexpr std::uint32_t StateReasonBit(StateReason reason) noexcept
    { return static_cast<std::uint32_t>(reason); }

    struct StateContext
    {
        Visibility visibility = Visibility::Unknown;
        bool noticeActive = false;
        MenuState menu = MenuState::Unknown;
        bool transition = false;
    };

    struct MetricSnapshot
    {
        std::uint64_t entries = 0, completed = 0, timedSamples = 0;
        std::uint64_t totalTicks = 0, totalUnits = 0, dropped = 0;
        std::uint64_t timingFailures = 0, incomplete = 0;
        std::uint64_t crossWindowSamples = 0, crossStateSamples = 0;
        std::uint64_t recursiveCalls = 0, overlapEntries = 0;
        // maxDepth is a lifetime high-water mark; activeAtSnapshot is an instantaneous gauge.
        std::uint64_t maxDepth = 0, activeAtSnapshot = 0;
        // Bucket upper bound only; this is not a measured maximum duration.
        std::uint32_t maxBucketUpperUs = 0;
        std::array<std::uint64_t, kDurationBucketCount> durationBuckets{};
    };

    struct Snapshot
    {
        std::uint64_t sequence = 0, previousSequence = 0;
        std::uint64_t windowStartTicks = 0, windowEndTicks = 0, frequency = 0;
        std::uint64_t stateDrops = 0, activeScopesAtSnapshot = 0;
        std::uint64_t totalTimedSamples = 0, crossStateSamples = 0;
        std::uint64_t reportLossTotal = 0, lastReportLossSequence = 0;
        std::uint64_t overflowTotal = 0, drainOverlapTotal = 0;
        std::uint64_t initializationViolationTotal = 0, clockFailureTotal = 0;
        std::uint32_t reportLossReasonMask = 0, stateReasonMask = 0;
        StateDisposition stateDisposition = StateDisposition::Unqualified;
        bool initialized = false, stateUpdateInProgressAtCut = false;
        // Independent atomic loads form a nearby-instant cut, not a coherent snapshot.
        bool counterCutFuzzy = true, ratesApproximate = true;
        bool spanDurationsInclusive = true, loggerSubmissionOnly = true;
        std::array<std::uint64_t, kCallbackGapBucketCount> callbackGapHistogram{};
        // Indexed stage-major, then result-major: stage * 3 + result.
        std::array<std::uint64_t, kOwnershipOutcomeCount> ownershipOutcomes{};
        std::uint8_t visibilityMask = 0, noticeMask = 0, menuMask = 0, transitionMask = 0;
        std::array<MetricSnapshot, static_cast<std::size_t>(Metric::Count)> metrics{};
        CollectorLossSnapshot collectorLoss{};
    };

    using ClockReader = bool (*)(void* context, std::uint64_t* ticks) noexcept;

    struct MetricState
    {
        std::atomic<std::uint64_t> entries{ 0 }, completed{ 0 }, timedSamples{ 0 };
        std::atomic<std::uint64_t> totalTicks{ 0 }, totalUnits{ 0 }, dropped{ 0 };
        std::atomic<std::uint64_t> timingFailures{ 0 }, incomplete{ 0 };
        std::atomic<std::uint64_t> crossWindowSamples{ 0 }, crossStateSamples{ 0 };
        std::atomic<std::uint64_t> recursiveCalls{ 0 }, overlapEntries{ 0 };
        std::atomic<std::uint64_t> maxDepth{ 0 }, active{ 0 };
        std::array<std::atomic<std::uint64_t>, kDurationBucketCount> durationBuckets{};
    };

    struct MetricTotals
    {
        std::uint64_t entries = 0, completed = 0, timedSamples = 0;
        std::uint64_t totalTicks = 0, totalUnits = 0, dropped = 0;
        std::uint64_t timingFailures = 0, incomplete = 0;
        std::uint64_t crossWindowSamples = 0, crossStateSamples = 0;
        std::uint64_t recursiveCalls = 0, overlapEntries = 0;
        std::array<std::uint64_t, kDurationBucketCount> durationBuckets{};
    };

    class Collector final
    {
    public:
        Collector() noexcept = default;
        Collector(const Collector&) = delete;
        Collector& operator=(const Collector&) = delete;

        // The first call fixes clock, frequency, and initial window for this lifetime.
        // Later calls never reset counters and increment initializationViolationTotal.
        void Initialize(ClockReader clock, void* clockContext,
            std::uint64_t frequency, std::uint64_t windowStartTicks) noexcept;

        // Count is an instantaneous completed event with no duration sample.
        bool Count(Metric metric) noexcept;
        // Record is a completed caller-measured duration in raw clock ticks and units.
        bool Record(Metric metric, std::uint64_t elapsedTicks,
            std::uint64_t units = 0) noexcept;
        bool RecordTimingFailure(Metric metric, bool incomplete = false) noexcept;
        bool RecordOwnershipOutcome(OwnershipStage stage,
            OwnershipResult result) noexcept;
        bool Observe(const StateContext& context, std::uint64_t nowTicks) noexcept;
        void MarkPresentCallbackMarker() noexcept;
        // Drain is single-owner and nonblocking. Sequence numbers count attempts;
        // a false return or later formatting loss leaves a visible gap.
        bool Drain(Snapshot& output) noexcept;
        // Call after a failed bounded format. Loss totals persist into later snapshots.
        bool RecordReportLoss(std::uint64_t sequence, ReportLossReason reason) noexcept;

    private:
        friend class ScopedTimer;
        friend class MenuWrapperPartition;
#if defined(MINIMAP_AGGREGATE_TIMING_LOSS_TEST_SEAMS)
        friend struct LossClassificationTestAccess;
#endif

        // Private fixed-capacity scope publication. 0 is empty; 1 is an
        // unknown/reserved entry; other values encode the entry state version.
        static constexpr std::size_t kActiveTokenCount = 256;
        struct ScopeToken
        {
            std::uint64_t ticks = 0, generation = 0, stateVersion = 0, depth = 0;
            std::size_t slot = kActiveTokenCount;
            bool clockValid = false, knownState = false;
        };
        struct WriterScope;

        bool IsValid(Metric metric) const noexcept;
        bool ReadClock(std::uint64_t& ticks) const noexcept;
        void Add(std::atomic<std::uint64_t>& counter, LossCounter identity,
            std::uint64_t amount = 1, MetricState* metric = nullptr,
            std::uint8_t member = 0) noexcept;
        void Lost(CollectorLossKind kind, LossCounter counter,
            MetricState* metric = nullptr, std::uint8_t member = 0) noexcept;
        void AccountingFailure(LossCounter counter, CollectorLossKind cause,
            Metric metric = Metric::Count, std::uint8_t member = 0) noexcept;
        void Begin(Metric metric, ScopeToken& token, bool readClock = true,
            std::uint64_t suppliedTicks = 0, bool suppliedValid = false) noexcept;
        void Complete(Metric metric, ScopeToken& token, bool incomplete,
            std::uint64_t endTicks, bool endClockValid,
            std::uint64_t units = 0) noexcept;
        void CountInternal(Metric metric) noexcept;
        void RecordInternal(Metric metric, std::uint64_t elapsedTicks,
            std::uint64_t units, bool incomplete,
            std::uint64_t startGeneration = 0) noexcept;
        void RecordFailureInternal(Metric metric, bool incomplete,
            std::uint64_t startGeneration = 0) noexcept;

        ClockReader clock_ = nullptr;
        void* clockContext_ = nullptr;
        std::uint64_t frequency_ = 0, windowStartTicks_ = 0;
        std::atomic<bool> initializationAttempted_{ false }, initialized_{ false };
        std::atomic<bool> drainActive_{ false };
        std::atomic<std::uint64_t> attemptedSequence_{ 0 };
        std::uint64_t previousDrainSequence_ = 0;
        std::array<MetricState, static_cast<std::size_t>(Metric::Count)> metrics_{};
        std::array<MetricTotals, static_cast<std::size_t>(Metric::Count)> previous_{};
        std::array<std::atomic<std::uint64_t>, kOwnershipOutcomeCount> ownershipOutcomes_{};
        std::array<std::uint64_t, kOwnershipOutcomeCount> previousOwnershipOutcomes_{};
        std::array<std::atomic<std::uint64_t>, kCallbackGapBucketCount> callbackGapHistogram_{};
        std::array<std::uint64_t, kCallbackGapBucketCount> previousCallbackGapHistogram_{};
        std::array<std::atomic<std::uint64_t>, 3> visibilitySeen_{};
        std::array<std::atomic<std::uint64_t>, 2> noticeSeen_{};
        std::array<std::atomic<std::uint64_t>, 3> menuSeen_{};
        std::array<std::atomic<std::uint64_t>, 2> transitionSeen_{};
        std::array<std::uint64_t, 3> previousVisibilitySeen_{};
        std::array<std::uint64_t, 2> previousNoticeSeen_{};
        std::array<std::uint64_t, 3> previousMenuSeen_{};
        std::array<std::uint64_t, 2> previousTransitionSeen_{};
        std::atomic<std::uint64_t> stateDrops_{ 0 }, callbackEntries_{ 0 };
        std::uint64_t lastPresentMarkerCallbacks_ = 0;
        std::atomic<std::uint64_t> lastContextPacked_{ 0 }, lastContextChangeTicks_{ 0 };
        std::atomic<std::uint64_t> firstContextTicks_{ 0 };
        std::atomic<std::uint64_t> generation_{ 0 }, stateVersion_{ 0 };
        std::atomic<std::uint64_t> observationWriters_{ 0 };
        std::atomic<bool> hasPriorContext_{ false };
        std::atomic<std::uint64_t> reportLossTotal_{ 0 }, lastReportLossSequence_{ 0 };
        std::atomic<std::uint32_t> reportLossReasonMask_{ 0 };
        std::atomic<std::uint64_t> overflowTotal_{ 0 }, drainOverlapTotal_{ 0 };
        std::atomic<std::uint64_t> initializationViolationTotal_{ 0 };
        std::atomic<std::uint64_t> clockFailureTotal_{ 0 };
        std::atomic<std::uint64_t> producerWriters_{ 0 }, producerVersion_{ 0 };
        std::atomic<std::uint64_t> publicationCarry_{ 0 };
        std::uint64_t previousPublicationCarry_ = 0;
        // Drain-owner-only debt for fields split across the preceding cut.
        // Inherited debt never refreshes itself; only actual overlap does.
        bool nextDrainPublicationDebt_ = false;
        std::array<std::atomic<std::uint64_t>, kCollectorLossKindCount> lossTotals_{};
        std::array<std::atomic<std::uint64_t>, kCollectorLossKindCount> lossCounterMasks_{};
        std::atomic<std::uint64_t> lastLossFailureIdentity_{ 0 };
        std::atomic<bool> lossAccountingUncertain_{ false };
        std::atomic<std::uint64_t> accountingFailureCauseMask_{ 0 };
        std::atomic<std::uint64_t> lastAccountingFailureIdentity_{ 0 };
        std::array<std::atomic<std::uint64_t>, kActiveTokenCount> activeTokens_{};
        std::atomic<std::uint64_t> tokenCursor_{ 0 };
        std::uint64_t previousWindowContext_ = 0;
        bool previousWindowHadContext_ = false;
    };

    class ScopedTimer final
    {
    public:
        ScopedTimer(Collector& collector, Metric metric) noexcept;
        ~ScopedTimer() noexcept;
        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;
        void Stop() noexcept;

    private:
        Collector* collector_;
        Metric metric_;
        Collector::ScopeToken token_{};
        int uncaughtAtEntry_ = 0;
        bool stopped_ = false;
    };

    class MenuWrapperPartition final
    {
    public:
        explicit MenuWrapperPartition(Collector& collector) noexcept;
        ~MenuWrapperPartition() noexcept;
        MenuWrapperPartition(const MenuWrapperPartition&) = delete;
        MenuWrapperPartition& operator=(const MenuWrapperPartition&) = delete;

        // Call immediately before the original forwarding call and immediately after
        // it returns. A throwing original has an attempt but no return or post sample.
        void ForwardStarted() noexcept;
        void ForwardReturned() noexcept;
        void NullOriginalExit() noexcept;

    private:
        Collector* collector_;
        Collector::ScopeToken callback_{}, pre_{}, original_{}, post_{};
        int uncaughtAtEntry_ = 0;
        bool forwardAttempted_ = false, forwardReturned_ = false;
        bool nullOriginal_ = false, stopped_ = false;
    };

    // Global collector storage is initialized before hooks are published by OnLoad.
    Collector& DiagnosticCollector() noexcept;
    const char* MetricName(Metric metric) noexcept;
    bool FormatSnapshot(const Snapshot& snapshot, char* output,
        std::size_t capacity, std::size_t* written) noexcept;
}
