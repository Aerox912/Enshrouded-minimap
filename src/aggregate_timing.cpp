#include "aggregate_timing.h"
// MM_BG_BEGIN
#include "background_diagnostic.h"
// MM_BG_END

#include <exception>
#include <limits>

namespace MinimapAggregateTiming
{
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
    // Regression-build-only barrier supplied by the test translation unit.
    // Neither this declaration nor any call below exists in shipped builds.
    void AggregateTimingTestPause(const Collector*, Metric, unsigned point) noexcept;
#endif
#if defined(MINIMAP_AGGREGATE_TIMING_LOSS_TEST_SEAMS)
    // Test-only scheduling/observation of the real CAS; neither seam changes
    // its arguments, atomic state, or return value. Absent in production.
    void AggregateTimingLossTestBeforeCas(const std::atomic<std::uint64_t>*,
        std::uint64_t expected, std::uint64_t desired, unsigned attempt) noexcept;
    void AggregateTimingLossTestAfterCas(const std::atomic<std::uint64_t>*,
        unsigned attempt, bool exchanged) noexcept;
#endif
    namespace
    {
        constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
        constexpr unsigned kAtomicAttempts = 8;
        constexpr std::uint32_t kOpenBucket = (std::numeric_limits<std::uint32_t>::max)();
        constexpr std::array<std::uint32_t, kDurationBucketCount - 1> kUpperUs{
            1, 2, 5, 10, 20, 50, 100, 250, 500, 1000, 5000
        };
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
            "Diagnostic hot paths require lock-free 64-bit atomics");

        enum class AddResult : std::uint8_t { Applied, Saturated, RetryExhausted };

        CollectorLossKind Cause(AddResult result) noexcept
        {
            return result == AddResult::Saturated
                ? CollectorLossKind::Saturation : CollectorLossKind::RetryExhaustion;
        }

        std::uint64_t CounterBit(LossCounter counter) noexcept
        { return std::uint64_t{ 1 } << static_cast<unsigned>(counter); }

        std::uint64_t PackLoss(CollectorLossKind kind, LossCounter counter,
            Metric metric, std::uint8_t member = 0) noexcept
        {
            const auto metricId = metric == Metric::Count ? 255u : static_cast<unsigned>(metric);
            return (std::uint64_t{ 1 } << 63) | static_cast<std::uint64_t>(kind) |
                (static_cast<std::uint64_t>(counter) << 3) |
                (static_cast<std::uint64_t>(metricId) << 9) |
                (static_cast<std::uint64_t>(member) << 17);
        }

        // No retry loop in this collector is unbounded. Failed CAS admission is
        // telemetry loss, never a reason to delay the original game operation.
        AddResult TryAdd(std::atomic<std::uint64_t>& counter, std::uint64_t amount,
            std::uint64_t* previous = nullptr) noexcept
        {
            auto old = counter.load(std::memory_order_seq_cst);
            for (unsigned attempt = 0; attempt < kAtomicAttempts; ++attempt)
            {
                const bool overflow = amount > kMaximum - old;
                const auto next = overflow ? kMaximum : old + amount;
#if defined(MINIMAP_AGGREGATE_TIMING_LOSS_TEST_SEAMS)
                AggregateTimingLossTestBeforeCas(&counter, old, next, attempt);
#endif
                const bool exchanged = counter.compare_exchange_weak(old, next, std::memory_order_seq_cst);
#if defined(MINIMAP_AGGREGATE_TIMING_LOSS_TEST_SEAMS)
                AggregateTimingLossTestAfterCas(&counter, attempt, exchanged);
#endif
                if (exchanged)
                {
                    if (previous != nullptr) *previous = old;
                    return overflow ? AddResult::Saturated : AddResult::Applied;
                }
            }
            return AddResult::RetryExhausted;
        }

        AddResult TryGaugeEntry(std::atomic<std::uint64_t>& gauge,
            std::uint64_t& previous) noexcept
        {
            auto old = gauge.load(std::memory_order_seq_cst);
            for (unsigned attempt = 0; attempt < kAtomicAttempts; ++attempt)
            {
                if (old == kMaximum) return AddResult::Saturated;
                if (gauge.compare_exchange_weak(old, old + 1, std::memory_order_seq_cst))
                {
                    previous = old;
                    return AddResult::Applied;
                }
            }
            return AddResult::RetryExhausted;
        }

        std::size_t Bucket(std::uint64_t ticks, std::uint64_t frequency) noexcept
        {
            // floor(frequency * upper_us / 1e6), without overflowing a product.
            for (std::size_t i = 0; i < kUpperUs.size(); ++i)
            {
                const auto bound = (frequency / 1000000) * kUpperUs[i] +
                    ((frequency % 1000000) * kUpperUs[i]) / 1000000;
                if (ticks <= bound) return i;
            }
            return kDurationBucketCount - 1;
        }

        struct ThreadDepth
        {
            const Collector* collector = nullptr;
            Metric metric = Metric::Count;
            std::uint64_t depth = 0;
        };
        // Independent collectors on the same thread never share recursion depth.
        thread_local std::array<ThreadDepth, 64> threadDepths{};

        ThreadDepth* FindDepth(const Collector* collector, Metric metric) noexcept
        {
            ThreadDepth* empty = nullptr;
            for (auto& slot : threadDepths)
            {
                if (slot.collector == collector && slot.metric == metric) return &slot;
                if (slot.collector == nullptr && empty == nullptr) empty = &slot;
            }
            return empty;
        }

        std::uint64_t Pack(const StateContext& state) noexcept
        {
            return static_cast<std::uint64_t>(state.visibility) |
                (static_cast<std::uint64_t>(state.noticeActive) << 2) |
                (static_cast<std::uint64_t>(state.menu) << 3) |
                (static_cast<std::uint64_t>(state.transition) << 5);
        }

        void IncludeContext(Snapshot& snapshot, std::uint64_t packed) noexcept
        {
            snapshot.visibilityMask |= static_cast<std::uint8_t>(1u << (packed & 3u));
            snapshot.noticeMask |= static_cast<std::uint8_t>(1u << ((packed >> 2) & 1u));
            snapshot.menuMask |= static_cast<std::uint8_t>(1u << ((packed >> 3) & 3u));
            snapshot.transitionMask |= static_cast<std::uint8_t>(1u << ((packed >> 5) & 1u));
        }

        bool Multiple(std::uint8_t mask) noexcept
        { return mask != 0 && (mask & (mask - 1)) != 0; }

        std::uint64_t Difference(std::uint64_t current, std::uint64_t& previous) noexcept
        {
            const auto result = current >= previous ? current - previous : 0;
            previous = current;
            return result;
        }

        void Sum(std::uint64_t& target, std::uint64_t value) noexcept
        { target = value > kMaximum - target ? kMaximum : target + value; }
    }

    struct Collector::WriterScope
    {
        Collector& collector;
        std::uint64_t generation = 0;
        bool cutAtEntry = false;
        explicit WriterScope(Collector& owner) noexcept : collector(owner)
        {
            collector.producerWriters_.fetch_add(1, std::memory_order_seq_cst);
            collector.Add(collector.producerVersion_, LossCounter::ProducerVersion);
            generation = collector.generation_.load();
            cutAtEntry = collector.drainActive_.load();
        }
        ~WriterScope() noexcept
        {
            // A cut may precede some of this writer's fields. Carry uncertainty
            // into its eventual completion window, even after it is no longer
            // active; checking only writers at the next cut would miss that.
            if (cutAtEntry || collector.drainActive_.load() ||
                generation != collector.generation_.load())
            {
                collector.Add(collector.publicationCarry_, LossCounter::PublicationCarry);
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
                AggregateTimingTestPause(&collector, Metric::Count, 6);
#endif
            }
            collector.Add(collector.producerVersion_, LossCounter::ProducerVersion);
            collector.producerWriters_.fetch_sub(1, std::memory_order_seq_cst);
        }
    };

    void Collector::AccountingFailure(LossCounter counter, CollectorLossKind cause,
        Metric metric, std::uint8_t member) noexcept
    {
        const auto accounting = static_cast<std::size_t>(CollectorLossKind::AccountingFailure);
        lossAccountingUncertain_.store(true, std::memory_order_seq_cst);
        accountingFailureCauseMask_.fetch_or(std::uint64_t{ 1 } << static_cast<unsigned>(cause));
        lossCounterMasks_[accounting].fetch_or(CounterBit(counter));
        lastAccountingFailureIdentity_.store(PackLoss(cause, counter, metric, member));
        const auto result = TryAdd(lossTotals_[accounting], 1);
        if (result != AddResult::Applied)
        {
            // No recursive Lost/AccountingFailure call. Even the fallback's
            // own total can fail: retain a max/unknown sentinel and sticky
            // cause/counter/identity through non-fallible lock-free operations.
            lossTotals_[accounting].store(kMaximum);
            accountingFailureCauseMask_.fetch_or(std::uint64_t{ 1 } << static_cast<unsigned>(Cause(result)));
            lossCounterMasks_[accounting].fetch_or(CounterBit(LossCounter::TypedLossTotal));
            lastAccountingFailureIdentity_.store(PackLoss(Cause(result), LossCounter::TypedLossTotal,
                Metric::Count, static_cast<std::uint8_t>(accounting)));
        }
    }

    void Collector::Lost(CollectorLossKind kind, LossCounter counter,
        MetricState* metric, std::uint8_t member) noexcept
    {
        const auto metricId = metric != nullptr
            ? static_cast<Metric>(metric - metrics_.data()) : Metric::Count;
        // Keep the original lifetime overflow/drop operations first. Typed
        // metadata never clears or replaces their existing conservative veto.
        const auto legacy = TryAdd(overflowTotal_, 1);
        if (legacy != AddResult::Applied)
        {
            overflowTotal_.store(kMaximum);
            AccountingFailure(LossCounter::LossAccountingTotal, Cause(legacy));
        }
        if (metric != nullptr)
        {
            const auto dropped = TryAdd(metric->dropped, 1);
            if (dropped != AddResult::Applied)
            {
                metric->dropped.store(kMaximum);
                AccountingFailure(LossCounter::MetricDroppedAccounting, Cause(dropped), metricId);
            }
        }
        const auto index = static_cast<std::size_t>(kind);
        lossCounterMasks_[index].fetch_or(CounterBit(counter), std::memory_order_seq_cst);
        const auto total = TryAdd(lossTotals_[index], 1);
        if (total != AddResult::Applied)
        {
            lossTotals_[index].store(kMaximum);
            AccountingFailure(LossCounter::TypedLossTotal, Cause(total), Metric::Count,
                static_cast<std::uint8_t>(index));
        }
        // One atomic tuple; accounting fallback never replaces the primary
        // identity. "Latest" means publication order, not a coherent cut or
        // wall-clock ordering of concurrent fault beginnings.
        lastLossFailureIdentity_.store(PackLoss(kind, counter, metricId, member));
    }

    void Collector::Add(std::atomic<std::uint64_t>& counter, LossCounter identity,
        std::uint64_t amount, MetricState* metric, std::uint8_t member) noexcept
    {
        if (amount == 0) return;
        const auto result = TryAdd(counter, amount);
        if (result != AddResult::Applied) Lost(Cause(result), identity, metric, member);
    }

    void Collector::Initialize(ClockReader clock, void* clockContext,
        std::uint64_t frequency, std::uint64_t windowStartTicks) noexcept
    {
        if (initializationAttempted_.exchange(true, std::memory_order_acq_rel))
        {
            Add(initializationViolationTotal_, LossCounter::InitializationViolationTotal);
            return;
        }
        if (clock == nullptr || frequency == 0)
        {
            Add(initializationViolationTotal_, LossCounter::InitializationViolationTotal);
            return;
        }
        clock_ = clock;
        clockContext_ = clockContext;
        frequency_ = frequency;
        windowStartTicks_ = windowStartTicks;
        initialized_.store(true, std::memory_order_release);
    }

    bool Collector::IsValid(Metric metric) const noexcept
    { return static_cast<std::size_t>(metric) < metrics_.size(); }

    bool Collector::ReadClock(std::uint64_t& ticks) const noexcept
    {
        ticks = 0;
        if (!initialized_.load(std::memory_order_acquire) || !clock_(clockContext_, &ticks))
        {
            const_cast<Collector*>(this)->Add(
                const_cast<Collector*>(this)->clockFailureTotal_, LossCounter::ClockFailureTotal);
            return false;
        }
        return true;
    }

    void Collector::CountInternal(Metric metric) noexcept
    {
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        Add(state.entries, LossCounter::MetricEntries, 1, &state);
        Add(state.completed, LossCounter::MetricCompleted, 1, &state);
        if (metric == Metric::MenuCallback) Add(callbackEntries_, LossCounter::CallbackEntries);
    }

    bool Collector::Count(Metric metric) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        if (!IsValid(metric)) { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidMetric); return false; }
        WriterScope write(*this);
        CountInternal(metric);
        return true;
    }

    void Collector::RecordInternal(Metric metric, std::uint64_t elapsedTicks,
        std::uint64_t units, bool incomplete, std::uint64_t startGeneration) noexcept
    {
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        Add(state.entries, LossCounter::MetricEntries, 1, &state);
        if (metric == Metric::MenuCallback) Add(callbackEntries_, LossCounter::CallbackEntries);
        if (incomplete) { Add(state.incomplete, LossCounter::MetricIncomplete, 1, &state); return; }
        Add(state.completed, LossCounter::MetricCompleted, 1, &state);
        Add(state.totalTicks, LossCounter::MetricTotalTicks, elapsedTicks, &state);
        Add(state.totalUnits, LossCounter::MetricTotalUnits, units, &state);
        const auto bucket = Bucket(elapsedTicks, frequency_);
        Add(state.durationBuckets[bucket], LossCounter::MetricDurationBucket, 1, &state, static_cast<std::uint8_t>(bucket));
        if (startGeneration != generation_.load()) Add(state.crossWindowSamples, LossCounter::MetricCrossWindow, 1, &state);
        Add(state.timedSamples, LossCounter::MetricTimedSamples, 1, &state);
    }

    bool Collector::Record(Metric metric, std::uint64_t elapsedTicks,
        std::uint64_t units) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        if (!IsValid(metric)) { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidMetric); return false; }
        WriterScope write(*this);
        RecordInternal(metric, elapsedTicks, units, false, generation_.load());
        return true;
    }

    void Collector::RecordFailureInternal(Metric metric, bool incomplete,
        std::uint64_t startGeneration) noexcept
    {
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        Add(state.entries, LossCounter::MetricEntries, 1, &state);
        if (metric == Metric::MenuCallback) Add(callbackEntries_, LossCounter::CallbackEntries);
        Add(state.timingFailures, LossCounter::MetricTimingFailures, 1, &state);
        Add(incomplete ? state.incomplete : state.completed,
            incomplete ? LossCounter::MetricIncomplete : LossCounter::MetricCompleted, 1, &state);
        if (startGeneration != generation_.load()) Add(state.crossWindowSamples, LossCounter::MetricCrossWindow, 1, &state);
    }

    bool Collector::RecordTimingFailure(Metric metric, bool incomplete) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        if (!IsValid(metric)) { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidMetric); return false; }
        WriterScope write(*this);
        RecordFailureInternal(metric, incomplete, generation_.load());
        return true;
    }

    bool Collector::RecordOwnershipOutcome(OwnershipStage stage,
        OwnershipResult result) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        const auto s = static_cast<std::size_t>(stage);
        const auto r = static_cast<std::size_t>(result);
        if (s >= 2 || r >= 3) { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidOwnership); return false; }
        WriterScope write(*this);
        Add(ownershipOutcomes_[s * 3 + r], static_cast<LossCounter>(
            static_cast<unsigned>(LossCounter::OwnershipPreRaw) + static_cast<unsigned>(s * 3 + r)));
        return true;
    }

    bool Collector::Observe(const StateContext& context, std::uint64_t nowTicks) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        if (static_cast<unsigned>(context.visibility) >= 3 ||
            static_cast<unsigned>(context.menu) >= 3)
        { Add(stateDrops_, LossCounter::StateDrops); return false; }
        WriterScope write(*this);
        std::uint64_t empty = 0;
        if (!observationWriters_.compare_exchange_strong(empty, 1))
        { Add(stateDrops_, LossCounter::StateDrops); return false; }
        if (hasPriorContext_.load() && nowTicks < lastContextChangeTicks_.load())
        {
            Add(clockFailureTotal_, LossCounter::ClockFailureTotal);
            Add(stateDrops_, LossCounter::StateDrops);
            observationWriters_.store(0);
            return false;
        }
        const auto packed = Pack(context);
        if (!hasPriorContext_.load() || lastContextPacked_.load() != packed)
        {
            Add(stateVersion_, LossCounter::StateVersion);
            if (!hasPriorContext_.load()) firstContextTicks_.store(nowTicks);
            lastContextPacked_.store(packed);
            lastContextChangeTicks_.store(nowTicks);
            hasPriorContext_.store(true);
        }
        Add(visibilitySeen_[static_cast<std::size_t>(context.visibility)], static_cast<LossCounter>(
            static_cast<unsigned>(LossCounter::VisibilityVisible) + static_cast<unsigned>(context.visibility)));
        Add(noticeSeen_[context.noticeActive ? 1 : 0], context.noticeActive ? LossCounter::NoticeActive : LossCounter::NoticeInactive);
        Add(menuSeen_[static_cast<std::size_t>(context.menu)], static_cast<LossCounter>(
            static_cast<unsigned>(LossCounter::MenuGameplay) + static_cast<unsigned>(context.menu)));
        Add(transitionSeen_[context.transition ? 1 : 0], context.transition ? LossCounter::TransitionActive : LossCounter::TransitionInactive);
        observationWriters_.store(0);
        return true;
    }

    void Collector::MarkPresentCallbackMarker() noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return;
        WriterScope write(*this);
        // Caller already holds the existing present serialization. No new lock.
        const auto callbacks = callbackEntries_.load();
        const auto gap = callbacks >= lastPresentMarkerCallbacks_
            ? callbacks - lastPresentMarkerCallbacks_ : 0;
        lastPresentMarkerCallbacks_ = callbacks;
        const std::size_t bucket = gap < 4 ? static_cast<std::size_t>(gap) :
            gap < 8 ? 4 : gap < 16 ? 5 : gap < 32 ? 6 : 7;
        Add(callbackGapHistogram_[bucket], LossCounter::CallbackGapHistogram, 1, nullptr, static_cast<std::uint8_t>(bucket));
    }

    void Collector::Begin(Metric metric, ScopeToken& token, bool readClock,
        std::uint64_t suppliedTicks, bool suppliedValid) noexcept
    {
// MM_BG_BEGIN
        MM_BG_COLLECTOR_SCOPE(metric == Metric::MarkerCapture ? MinimapBackgroundDiagnostic::Metric::CollectorBeginMarker : MinimapBackgroundDiagnostic::Metric::CollectorBeginOther);
// MM_BG_END
        token = ScopeToken{};
        if (!initialized_.load(std::memory_order_acquire)) return;
        if (!IsValid(metric)) { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidMetric); return; }
        WriterScope write(*this);
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        Add(state.entries, LossCounter::MetricEntries, 1, &state);
        if (metric == Metric::MenuCallback) Add(callbackEntries_, LossCounter::CallbackEntries);
        auto* local = FindDepth(this, metric);
        if (local == nullptr || local->depth == kMaximum)
        {
            Lost(CollectorLossKind::Capacity, local == nullptr
                ? LossCounter::ThreadDepthCapacity : LossCounter::ThreadDepthOverflow, &state);
            return;
        }
        const auto cursor = tokenCursor_.fetch_add(1, std::memory_order_relaxed);
        for (std::size_t offset = 0; offset < kActiveTokenCount; ++offset)
        {
            const auto slot = static_cast<std::size_t>((cursor + offset) % kActiveTokenCount);
            std::uint64_t empty = 0;
            if (activeTokens_[slot].compare_exchange_strong(empty, 1))
            { token.slot = slot; break; }
        }
        if (token.slot == kActiveTokenCount) { Lost(CollectorLossKind::Capacity, LossCounter::ScopeTokenCapacity, &state); return; }
        std::uint64_t priorActive = 0;
        const auto admitted = TryGaugeEntry(state.active, priorActive);
        if (admitted != AddResult::Applied)
        {
            activeTokens_[token.slot].store(0);
            token.slot = kActiveTokenCount;
            Lost(Cause(admitted), LossCounter::MetricActive, &state);
            return;
        }
        if (priorActive > local->depth) Add(state.overlapEntries, LossCounter::MetricOverlap, 1, &state);
        local->collector = this;
        local->metric = metric;
        token.depth = ++local->depth;
        if (token.depth > 1) Add(state.recursiveCalls, LossCounter::MetricRecursive, 1, &state);
        auto maximum = state.maxDepth.load();
        unsigned attempt = 0;
        while (maximum < token.depth && attempt++ < kAtomicAttempts)
            if (state.maxDepth.compare_exchange_weak(maximum, token.depth)) break;
        if (maximum < token.depth && state.maxDepth.load() < token.depth)
            Lost(CollectorLossKind::RetryExhaustion, LossCounter::MetricMaxDepth, &state);

        token.generation = generation_.load();
        token.stateVersion = stateVersion_.load();
        token.knownState = hasPriorContext_.load() && observationWriters_.load() == 0 &&
            token.stateVersion == stateVersion_.load() && token.stateVersion < kMaximum - 2;
        token.ticks = suppliedTicks;
        token.clockValid = readClock ? ReadClock(token.ticks) : suppliedValid;
        activeTokens_[token.slot].store(token.knownState ? token.stateVersion + 2 : 1);
    }

    void Collector::Complete(Metric metric, ScopeToken& token, bool incomplete,
        std::uint64_t endTicks, bool endClockValid, std::uint64_t units) noexcept
    {
// MM_BG_BEGIN
        MM_BG_COLLECTOR_SCOPE(metric == Metric::MarkerCapture ? MinimapBackgroundDiagnostic::Metric::CollectorEndMarker : MinimapBackgroundDiagnostic::Metric::CollectorEndOther);
// MM_BG_END
        if (token.depth == 0 || !IsValid(metric)) return;
        WriterScope write(*this);
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        const bool crossedWindow = token.generation != generation_.load();
        if (crossedWindow) Add(state.crossWindowSamples, LossCounter::MetricCrossWindow, 1, &state);
        const bool crossedState = !token.knownState || observationWriters_.load() != 0 ||
            token.stateVersion != stateVersion_.load();
        const bool valid = token.clockValid && endClockValid && endTicks >= token.ticks;
        // Attribution flags count completed/abandoned scopes, including an
        // incomplete or clock-invalid one. They never create a timed sample.
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
        AggregateTimingTestPause(this, metric, 1);
#endif
        if (crossedState) Add(state.crossStateSamples, LossCounter::MetricCrossState, 1, &state);
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
        AggregateTimingTestPause(this, metric, 2);
#endif
        if (incomplete) Add(state.incomplete, LossCounter::MetricIncomplete, 1, &state);
        else Add(state.completed, LossCounter::MetricCompleted, 1, &state);
        if (!valid)
        {
            Add(state.timingFailures, LossCounter::MetricTimingFailures, 1, &state);
            if (token.clockValid && endClockValid && endTicks < token.ticks)
                Add(clockFailureTotal_, LossCounter::ClockFailureTotal);
        }
        if (!incomplete && valid)
        {
            // Publish attribution flags before the duration. Producer fences
            // also veto qualification if Drain cuts this multi-atomic update.
            const auto elapsed = endTicks - token.ticks;
            Add(state.totalTicks, LossCounter::MetricTotalTicks, elapsed, &state);
            Add(state.totalUnits, LossCounter::MetricTotalUnits, units, &state);
            const auto bucket = Bucket(elapsed, frequency_);
            Add(state.durationBuckets[bucket], LossCounter::MetricDurationBucket, 1, &state, static_cast<std::uint8_t>(bucket));
            Add(state.timedSamples, LossCounter::MetricTimedSamples, 1, &state);
        }
        // Recheck after publishing all duration fields. An earlier attribution
        // read cannot be used as a coherent cut with later atomic writes.
        if (!crossedWindow && token.generation != generation_.load())
            Add(state.crossWindowSamples, LossCounter::MetricCrossWindow, 1, &state);
        if (!crossedState && (observationWriters_.load() != 0 ||
            token.stateVersion != stateVersion_.load()))
            Add(state.crossStateSamples, LossCounter::MetricCrossState, 1, &state);
        auto* local = FindDepth(this, metric);
        if (local != nullptr && local->collector == this && local->depth != 0)
        {
            if (--local->depth == 0) *local = ThreadDepth{};
        }
        else Lost(CollectorLossKind::InvalidUsage, LossCounter::ThreadDepthMismatch, &state);
        state.active.fetch_sub(1);
        activeTokens_[token.slot].store(0);
        token.depth = 0;
        token.slot = kActiveTokenCount;
    }

    bool Collector::RecordReportLoss(std::uint64_t sequence, ReportLossReason reason) noexcept
    {
        if (!initialized_.load(std::memory_order_acquire)) return false;
        const auto bit = static_cast<unsigned>(reason);
        if (bit > static_cast<unsigned>(ReportLossReason::InvalidSnapshot))
        { Lost(CollectorLossKind::InvalidUsage, LossCounter::InvalidReportLossReason); return false; }
        WriterScope write(*this);
        Add(reportLossTotal_, LossCounter::ReportLossTotal);
        reportLossReasonMask_.fetch_or(1u << bit);
        auto previous = lastReportLossSequence_.load();
        unsigned attempt = 0;
        while (previous < sequence && attempt++ < kAtomicAttempts)
            if (lastReportLossSequence_.compare_exchange_weak(previous, sequence)) break;
        if (previous < sequence && lastReportLossSequence_.load() < sequence)
            Lost(CollectorLossKind::RetryExhaustion, LossCounter::LastReportLossSequence);
        return true;
    }

    bool Collector::Drain(Snapshot& output) noexcept
    {
        output = Snapshot{};
        std::uint64_t previousAttempt = 0;
        const auto reserved = TryAdd(attemptedSequence_, 1, &previousAttempt);
        if (reserved != AddResult::Applied)
        {
            Lost(Cause(reserved), LossCounter::AttemptedSequence);
            RecordReportLoss(attemptedSequence_.load(), ReportLossReason::InvalidSnapshot);
            return false;
        }
        const auto sequence = previousAttempt + 1;
        output.sequence = sequence;
        if (drainActive_.exchange(true, std::memory_order_acquire))
        { Add(drainOverlapTotal_, LossCounter::DrainOverlapTotal); RecordReportLoss(sequence, ReportLossReason::InvalidSnapshot); return false; }
        if (!initialized_.load(std::memory_order_acquire))
        { drainActive_.store(false, std::memory_order_release); return false; }
        std::uint64_t now = 0;
        Add(generation_, LossCounter::DrainGeneration);
        const bool clockValid = ReadClock(now);
        if (!clockValid || now < windowStartTicks_)
        {
            if (clockValid && now < windowStartTicks_) Add(clockFailureTotal_, LossCounter::ClockFailureTotal);
            RecordReportLoss(sequence, ReportLossReason::InvalidSnapshot);
            drainActive_.store(false, std::memory_order_release);
            return false;
        }
        output.initialized = true;
        output.previousSequence = previousDrainSequence_;
        output.windowStartTicks = windowStartTicks_;
        output.windowEndTicks = now;
        output.frequency = frequency_;
        const auto writersBefore = producerWriters_.load();
        const auto producerBefore = producerVersion_.load();
        const auto stateBefore = stateVersion_.load();
        const auto stateWriterBefore = observationWriters_.load();

        for (std::size_t i = 0; i < metrics_.size(); ++i)
        {
            auto& result = output.metrics[i];
            const auto& state = metrics_[i];
            auto& prior = previous_[i];
#define MM_DELTA(field) result.field = Difference(state.field.load(), prior.field)
            MM_DELTA(entries); MM_DELTA(completed); MM_DELTA(timedSamples);
            MM_DELTA(totalTicks);
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
            AggregateTimingTestPause(this, static_cast<Metric>(i), 3);
#endif
            MM_DELTA(totalUnits); MM_DELTA(dropped);
            MM_DELTA(timingFailures); MM_DELTA(incomplete);
            MM_DELTA(crossWindowSamples); MM_DELTA(crossStateSamples);
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
            AggregateTimingTestPause(this, static_cast<Metric>(i), 4);
#endif
            MM_DELTA(recursiveCalls); MM_DELTA(overlapEntries);
#undef MM_DELTA
            result.maxDepth = state.maxDepth.load();
            result.activeAtSnapshot = state.active.load();
            for (std::size_t b = 0; b < kDurationBucketCount; ++b)
            {
                result.durationBuckets[b] = Difference(state.durationBuckets[b].load(), prior.durationBuckets[b]);
                if (result.durationBuckets[b] != 0)
                    result.maxBucketUpperUs = b < kUpperUs.size() ? kUpperUs[b] : kOpenBucket;
            }
            Sum(output.totalTimedSamples, result.timedSamples);
            Sum(output.crossStateSamples, result.crossStateSamples);
            Sum(output.activeScopesAtSnapshot, result.activeAtSnapshot);
        }
        for (std::size_t i = 0; i < kOwnershipOutcomeCount; ++i)
            output.ownershipOutcomes[i] = Difference(ownershipOutcomes_[i].load(), previousOwnershipOutcomes_[i]);
        for (std::size_t i = 0; i < kCallbackGapBucketCount; ++i)
            output.callbackGapHistogram[i] = Difference(callbackGapHistogram_[i].load(), previousCallbackGapHistogram_[i]);
        for (std::size_t i = 0; i < visibilitySeen_.size(); ++i)
            if (Difference(visibilitySeen_[i].load(), previousVisibilitySeen_[i]) != 0)
                output.visibilityMask |= static_cast<std::uint8_t>(1u << i);
        for (std::size_t i = 0; i < noticeSeen_.size(); ++i)
            if (Difference(noticeSeen_[i].load(), previousNoticeSeen_[i]) != 0)
                output.noticeMask |= static_cast<std::uint8_t>(1u << i);
        for (std::size_t i = 0; i < menuSeen_.size(); ++i)
            if (Difference(menuSeen_[i].load(), previousMenuSeen_[i]) != 0)
                output.menuMask |= static_cast<std::uint8_t>(1u << i);
        for (std::size_t i = 0; i < transitionSeen_.size(); ++i)
            if (Difference(transitionSeen_[i].load(), previousTransitionSeen_[i]) != 0)
                output.transitionMask |= static_cast<std::uint8_t>(1u << i);

        const bool hasContext = hasPriorContext_.load();
        const auto context = lastContextPacked_.load();
        const auto changeTicks = lastContextChangeTicks_.load();
        const auto stateAtTokens = stateVersion_.load();
        bool unknownActive = false, crossStateActive = false;
        for (const auto& token : activeTokens_)
        {
            const auto entry = token.load();
            if (entry == 1) unknownActive = true;
            else if (entry > 1 && (stateAtTokens >= kMaximum - 2 || entry - 2 != stateAtTokens))
                crossStateActive = true;
        }
        if (previousWindowHadContext_) IncludeContext(output, previousWindowContext_);
        if (hasContext) IncludeContext(output, context);
        output.stateDrops = stateDrops_.load();
        output.reportLossTotal = reportLossTotal_.load();
        output.lastReportLossSequence = lastReportLossSequence_.load();
        output.reportLossReasonMask = reportLossReasonMask_.load();
        bool typedLossObserved = false;
        for (std::size_t kind = 0; kind < kCollectorLossKindCount; ++kind)
        {
            output.collectorLoss.totals[kind] = lossTotals_[kind].load(std::memory_order_seq_cst);
            output.collectorLoss.counterMasks[kind] = lossCounterMasks_[kind].load(std::memory_order_seq_cst);
            typedLossObserved = typedLossObserved || output.collectorLoss.totals[kind] != 0 ||
                output.collectorLoss.counterMasks[kind] != 0;
        }
        output.collectorLoss.lastFailureIdentity = lastLossFailureIdentity_.load();
        output.collectorLoss.accountingUncertain = lossAccountingUncertain_.load();
        output.collectorLoss.accountingFailureCauseMask = accountingFailureCauseMask_.load();
        output.collectorLoss.lastAccountingFailureIdentity = lastAccountingFailureIdentity_.load();
        typedLossObserved = typedLossObserved || output.collectorLoss.lastFailureIdentity != 0 ||
            output.collectorLoss.accountingUncertain || output.collectorLoss.accountingFailureCauseMask != 0 ||
            output.collectorLoss.lastAccountingFailureIdentity != 0;
        output.overflowTotal = overflowTotal_.load();
        output.drainOverlapTotal = drainOverlapTotal_.load();
        output.initializationViolationTotal = initializationViolationTotal_.load();
        output.clockFailureTotal = clockFailureTotal_.load();
        const auto stateAfter = stateVersion_.load();
#if defined(MINIMAP_AGGREGATE_TIMING_TEST_SEAMS)
        AggregateTimingTestPause(this, Metric::Count, 5);
#endif
        const bool publicationCarried = Difference(publicationCarry_.load(), previousPublicationCarry_) != 0;
        const bool actualPublicationOverlap = publicationCarried || stateWriterBefore != 0 || observationWriters_.load() != 0 ||
            stateBefore != stateAfter || stateAtTokens != stateAfter || writersBefore != 0 ||
            producerWriters_.load() != 0 || producerBefore != producerVersion_.load();
        // A cut can consume attribution/carry after having missed duration
        // fields. Keep the following drain uncertain even if that writer has
        // finished and its carry was consumed here. Inherited debt affects
        // this output only: it must not recursively keep idle windows impure.
        output.stateUpdateInProgressAtCut = nextDrainPublicationDebt_ || actualPublicationOverlap;
        nextDrainPublicationDebt_ = actualPublicationOverlap;

        auto reason = [&output](StateReason value) noexcept { output.stateReasonMask |= StateReasonBit(value); };
        if (!hasContext || output.visibilityMask == 0 || (output.visibilityMask & 4u) != 0 || unknownActive ||
            (!previousWindowHadContext_ && firstContextTicks_.load() > windowStartTicks_))
            reason(StateReason::UnknownState);
        if (Multiple(output.visibilityMask) || Multiple(output.noticeMask) ||
            Multiple(output.menuMask) || Multiple(output.transitionMask)) reason(StateReason::MixedWindow);
        if ((output.noticeMask & 2u) != 0) reason(StateReason::NoticeActive);
        if (output.menuMask != 1) reason(StateReason::MenuOrUnknownMenu);
        if ((output.transitionMask & 2u) != 0) reason(StateReason::Transition);
        if (!hasContext || now < changeTicks || (now - changeTicks) / frequency_ < kMinimumSettledStateSeconds)
            reason(StateReason::Settling);
        if (output.stateUpdateInProgressAtCut) reason(StateReason::StateUpdateAtCut);
        if (output.activeScopesAtSnapshot != 0) reason(StateReason::ActiveScopes);
        if (output.crossStateSamples != 0 || crossStateActive) reason(StateReason::CrossStateWork);
        if (output.reportLossTotal != 0 || output.overflowTotal != 0 || output.drainOverlapTotal != 0 || typedLossObserved)
            reason(StateReason::ReportLoss);
        if (output.stateDrops != 0) reason(StateReason::StateDrops);
        if (output.clockFailureTotal != 0) reason(StateReason::ClockFailure);
        if (output.initializationViolationTotal != 0) reason(StateReason::InitializationFailure);
        // Parent's narrowed meaning: this labels the observed state, not pure
        // total cost. Same-state active scopes remain an explicit limitation.
        const auto disqualifying = output.stateReasonMask & ~StateReasonBit(StateReason::ActiveScopes);
        if (disqualifying == 0)
            output.stateDisposition = output.visibilityMask == 1
                ? StateDisposition::VisibleSettled : StateDisposition::HiddenSettled;

        previousWindowContext_ = context;
        previousWindowHadContext_ = hasContext;
        previousDrainSequence_ = sequence;
        windowStartTicks_ = now;
        drainActive_.store(false, std::memory_order_release);
        return true;
    }

    ScopedTimer::ScopedTimer(Collector& collector, Metric metric) noexcept
        : collector_(&collector), metric_(metric), uncaughtAtEntry_(std::uncaught_exceptions())
    { collector_->Begin(metric_, token_); }

    ScopedTimer::~ScopedTimer() noexcept { Stop(); }

    void ScopedTimer::Stop() noexcept
    {
        if (stopped_) return;
        stopped_ = true;
        if (token_.depth == 0) return;
        std::uint64_t end = 0;
        const bool valid = collector_->ReadClock(end);
        collector_->Complete(metric_, token_, std::uncaught_exceptions() > uncaughtAtEntry_, end, valid);
    }

    MenuWrapperPartition::MenuWrapperPartition(Collector& collector) noexcept
        : collector_(&collector), uncaughtAtEntry_(std::uncaught_exceptions())
    {
        collector_->Begin(Metric::MenuCallback, callback_);
        collector_->Begin(Metric::OwnershipPreProof, pre_, false, callback_.ticks, callback_.clockValid);
    }

    void MenuWrapperPartition::ForwardStarted() noexcept
    {
        if (stopped_ || forwardAttempted_ || nullOriginal_) return;
        forwardAttempted_ = true;
        std::uint64_t ticks = 0;
        const bool valid = collector_->ReadClock(ticks);
        collector_->Complete(Metric::OwnershipPreProof, pre_, false, ticks, valid);
        collector_->Count(Metric::ForwardAttempt);
        collector_->Begin(Metric::OwnershipOriginalForward, original_, false, ticks, valid);
    }

    void MenuWrapperPartition::ForwardReturned() noexcept
    {
        if (stopped_ || !forwardAttempted_ || forwardReturned_) return;
        forwardReturned_ = true;
        std::uint64_t ticks = 0;
        const bool valid = collector_->ReadClock(ticks);
        collector_->Complete(Metric::OwnershipOriginalForward, original_, false, ticks, valid);
        collector_->Count(Metric::ForwardReturn);
        collector_->Begin(Metric::OwnershipPostProof, post_, false, ticks, valid);
    }

    void MenuWrapperPartition::NullOriginalExit() noexcept
    {
        if (stopped_ || nullOriginal_ || forwardAttempted_) return;
        nullOriginal_ = true;
        collector_->Count(Metric::NullOriginalExit);
    }

    MenuWrapperPartition::~MenuWrapperPartition() noexcept
    {
        if (stopped_) return;
        stopped_ = true;
        std::uint64_t ticks = 0;
        const bool valid = collector_->ReadClock(ticks);
        const bool unwinding = std::uncaught_exceptions() > uncaughtAtEntry_;
        if (forwardReturned_)
            collector_->Complete(Metric::OwnershipPostProof, post_, unwinding, ticks, valid);
        else if (forwardAttempted_)
            collector_->Complete(Metric::OwnershipOriginalForward, original_, true, ticks, valid);
        else collector_->Complete(Metric::OwnershipPreProof, pre_, unwinding, ticks, valid);
        collector_->Complete(Metric::MenuCallback, callback_,
            unwinding || (forwardAttempted_ && !forwardReturned_), ticks, valid);
    }

    Collector& DiagnosticCollector() noexcept
    {
        static Collector collector;
        return collector;
    }

    const char* MetricName(Metric metric) noexcept
    {
        static constexpr std::array<const char*, static_cast<std::size_t>(Metric::Count)> names{
            "menu-callback", "ownership-pre", "ownership-original", "ownership-post",
            "proof-callback-pre", "proof-callback-post", "proof-admission-update", "proof-other",
            "present-callback", "present-mutex-acquire", "present-local", "original-present",
            "submission-gate", "cached-camera-refresh", "camera-fallback",
            "file-cache-mutex-acquire", "file-cache-mutex-hold", "renderer-mutex-acquire",
            "camera", "marker", "fog", "config-poll", "log-poll",
            "overlay-record", "overlay-widget", "overlay-notice", "overlay-submit",
            "proof-cache-warm", "proof-cache-cold", "proof-cold-success", "proof-cold-failure",
            "backing-open-attempt", "backing-open-success", "backing-open-failure", "backing-read",
            "null-original", "forward-attempt", "forward-return", "adjusted-present",
            "invalidate-clear", "invalidate-raw", "invalidate-module",
            "gate-no-runtime", "gate-no-visible", "gate-no-queue-waits", "gate-no-swapchain",
            "gate-renderer-build-failure", "submit-success", "submit-failure",
            "aggregate-drain", "aggregate-format", "logger-submission"
        };
        const auto index = static_cast<std::size_t>(metric);
        return index < names.size() ? names[index] : "invalid";
    }

    namespace
    {
        const char* LossKindName(unsigned kind) noexcept
        {
            static constexpr std::array<const char*, kCollectorLossKindCount> names{
                "saturation", "retry-exhaustion", "capacity", "invalid-usage", "accounting-failure"
            };
            return kind < names.size() ? names[kind] : "unknown-kind";
        }

        const char* LossCounterName(unsigned counter) noexcept
        {
            static constexpr std::array<const char*, static_cast<std::size_t>(LossCounter::Count)> names{
                "unknown", "producer-version", "publication-carry", "callback-entries",
                "ownership-pre-raw", "ownership-pre-public", "ownership-pre-rejected",
                "ownership-post-raw", "ownership-post-public", "ownership-post-rejected",
                "visibility-visible", "visibility-hidden", "visibility-unknown",
                "notice-inactive", "notice-active", "menu-gameplay", "menu-menu", "menu-unknown",
                "transition-inactive", "transition-active", "state-version", "state-drops",
                "attempted-sequence", "drain-generation", "report-loss-total", "last-report-loss-sequence",
                "initialization-violation-total", "clock-failure-total", "drain-overlap-total",
                "loss-accounting-total", "metric-dropped-accounting",
                "metric-entries", "metric-completed", "metric-timed-samples", "metric-total-ticks",
                "metric-total-units", "metric-timing-failures", "metric-incomplete",
                "metric-cross-window", "metric-cross-state", "metric-recursive", "metric-overlap",
                "metric-max-depth", "metric-active", "metric-duration-bucket",
                "scope-token-capacity", "thread-depth-capacity", "thread-depth-overflow",
                "thread-depth-mismatch", "invalid-metric", "invalid-ownership", "invalid-report-loss-reason",
                "callback-gap-histogram", "typed-loss-total"
            };
            return counter < names.size() ? names[counter] : "unknown-counter";
        }

        class LineWriter final
        {
        public:
            LineWriter(char* output, std::size_t capacity) noexcept
                : output_(output), capacity_(capacity) {}

            void Text(const char* value) noexcept
            {
                // All input strings are collector-owned fixed literals/names.
                for (std::size_t i = 0; value[i] != '\0'; ++i) Character(value[i]);
            }

            void Character(char value) noexcept
            {
                if (failed_) return;
                if (position_ >= capacity_ - 1) { failed_ = true; return; }
                output_[position_++] = value;
            }

            void Number(std::uint64_t value) noexcept
            {
                char digits[20];
                std::size_t count = 0;
                do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; }
                while (value != 0);
                while (count != 0) Character(digits[--count]);
            }

            void Field(const char* name, std::uint64_t value) noexcept
            { Text(name); Character('='); Number(value); Character(' '); }

            template <std::size_t N>
            void Values(const std::array<std::uint64_t, N>& values) noexcept
            {
                Character('[');
                for (std::size_t i = 0; i < N; ++i)
                { if (i != 0) Character(','); Number(values[i]); }
                Character(']');
            }

            bool Finish(std::size_t* written) noexcept
            {
                if (failed_) { output_[0] = '\0'; return false; }
                output_[position_] = '\0';
                if (written != nullptr) *written = position_;
                return true;
            }

        private:
            char* output_;
            std::size_t capacity_, position_ = 0;
            bool failed_ = false;
        };

        void IdentityFields(LineWriter& writer, const char* prefix, std::uint64_t identity) noexcept
        {
            const bool valid = (identity & (std::uint64_t{ 1 } << 63)) != 0;
            writer.Text(prefix); writer.Text("-kind=");
            writer.Text(valid ? LossKindName(static_cast<unsigned>(identity & 7u)) : "none");
            writer.Character(' ');
            writer.Text(prefix); writer.Text("-counter=");
            writer.Text(valid ? LossCounterName(static_cast<unsigned>((identity >> 3) & 63u)) : "none");
            writer.Character(' ');
            const auto metric = static_cast<unsigned>((identity >> 9) & 255u);
            writer.Text(prefix); writer.Text("-metric=");
            writer.Text(valid && metric != 255 ? MetricName(static_cast<Metric>(metric)) : "none");
            writer.Character(' ');
            writer.Text(prefix); writer.Text("-member=");
            writer.Number(valid ? (identity >> 17) & 255u : 0);
            writer.Character(' ');
        }

        bool Nonzero(const MetricSnapshot& metric) noexcept
        {
            if (metric.entries || metric.completed || metric.timedSamples || metric.totalTicks ||
                metric.totalUnits || metric.dropped || metric.timingFailures || metric.incomplete ||
                metric.crossWindowSamples || metric.crossStateSamples || metric.recursiveCalls ||
                metric.overlapEntries || metric.maxDepth || metric.activeAtSnapshot || metric.maxBucketUpperUs)
                return true;
            for (const auto value : metric.durationBuckets) if (value != 0) return true;
            return false;
        }

        void Reasons(LineWriter& writer, std::uint32_t mask) noexcept
        {
            static constexpr std::array<const char*, 13> names{
                "unknown", "mixed", "notice", "menu-or-unknown", "transition", "settling",
                "publication-at-cut", "active-scopes", "cross-state-work", "report-loss",
                "state-drops", "clock-failure", "initialization-failure"
            };
            writer.Text("reasons=[");
            bool first = true;
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if ((mask & (1u << i)) == 0) continue;
                if (!first) writer.Character(',');
                writer.Text(names[i]);
                first = false;
            }
            writer.Text("] ");
        }

        void ApproximateRate(LineWriter& writer, std::uint64_t callbacks,
            std::uint64_t elapsed, std::uint64_t frequency) noexcept
        {
            writer.Text("callback-rate=approx:");
            if (elapsed == 0) { writer.Text("unavailable "); return; }
            const long double rate = static_cast<long double>(callbacks) *
                static_cast<long double>(frequency) / static_cast<long double>(elapsed);
            if (rate >= static_cast<long double>(kMaximum)) writer.Text(">=UINT64_MAX");
            else
            {
                const auto whole = static_cast<std::uint64_t>(rate);
                writer.Number(whole);
                writer.Character('.');
                const auto fraction = static_cast<unsigned>((rate - static_cast<long double>(whole)) * 1000);
                writer.Character(static_cast<char>('0' + (fraction / 100) % 10));
                writer.Character(static_cast<char>('0' + (fraction / 10) % 10));
                writer.Character(static_cast<char>('0' + fraction % 10));
            }
            writer.Text("/s ");
        }
    }

    bool FormatSnapshot(const Snapshot& snapshot, char* output,
        std::size_t capacity, std::size_t* written) noexcept
    {
        if (written != nullptr) *written = 0;
        if (output == nullptr || capacity == 0) return false;
        output[0] = '\0';
        if (!snapshot.initialized || snapshot.frequency == 0 ||
            snapshot.windowEndTicks < snapshot.windowStartTicks ||
            !snapshot.counterCutFuzzy || !snapshot.ratesApproximate ||
            !snapshot.spanDurationsInclusive || !snapshot.loggerSubmissionOnly ||
            static_cast<unsigned>(snapshot.stateDisposition) >
                static_cast<unsigned>(StateDisposition::HiddenSettled)) return false;

        LineWriter writer(output, capacity);
        writer.Text("Shroudweave-Minimap timing-schema=aggregate-v1 ");
        writer.Field("seq", snapshot.sequence);
        writer.Field("prev-seq", snapshot.previousSequence);
        const auto gap = snapshot.sequence > snapshot.previousSequence
            ? snapshot.sequence - snapshot.previousSequence - 1 : 0;
        writer.Field("attempt-gap", gap);
        writer.Field("window-start-ticks", snapshot.windowStartTicks);
        writer.Field("window-end-ticks", snapshot.windowEndTicks);
        const auto elapsed = snapshot.windowEndTicks - snapshot.windowStartTicks;
        writer.Field("window-ticks", elapsed);
        writer.Field("frequency-Hz", snapshot.frequency);
        writer.Text("units=raw-clock-ticks-and-operation-units counter-cut=nearby-instant-fuzzy ");
        writer.Text("duration=inclusive attribution=whole-completion-window ");
        writer.Text("wrapper-spans=include-diagnostic-bookkeeping ");
        writer.Text("external-record-state=completion-only logger=submit-call-only ");
        writer.Text("state-label=observed-state-only active-same-state=timing-limitation ");
        writer.Text("active-or-cross-window-spans=not-pure-state-total-cost ");
        writer.Text("depth=lifetime-high-water active=instantaneous ");
        writer.Text("duration-us-buckets=[0..1,>1..2,>2..5,>5..10,>10..20,>20..50,>50..100,>100..250,>250..500,>500..1000,>1000..5000,>5000] >5000us=open-ended ");
        writer.Text("bucket-bound=not-measured-maximum loss-counters=saturating-UINT64_MAX-means-at-least-or-unknown ");
        writer.Field("present-count", snapshot.metrics[static_cast<std::size_t>(Metric::PresentCallback)].entries);
        const auto callbacks = snapshot.metrics[static_cast<std::size_t>(Metric::MenuCallback)].entries;
        writer.Field("callback-count", callbacks);
        ApproximateRate(writer, callbacks, elapsed, snapshot.frequency);
        writer.Text("callback-rate-is-not-displayed-FPS ");
        writer.Text("state=");
        writer.Text(snapshot.stateDisposition == StateDisposition::VisibleSettled ? "visible-settled " :
            snapshot.stateDisposition == StateDisposition::HiddenSettled ? "hidden-settled " : "unqualified ");
        writer.Field("state-reason-mask", snapshot.stateReasonMask);
        Reasons(writer, snapshot.stateReasonMask);
        writer.Field("visibility-mask", snapshot.visibilityMask);
        writer.Field("notice-mask", snapshot.noticeMask);
        writer.Field("menu-mask", snapshot.menuMask);
        writer.Field("transition-mask", snapshot.transitionMask);
        writer.Field("publication-at-cut", snapshot.stateUpdateInProgressAtCut ? 1 : 0);
        writer.Field("active-scopes", snapshot.activeScopesAtSnapshot);
        writer.Field("timed-samples", snapshot.totalTimedSamples);
        writer.Field("cross-state-completions", snapshot.crossStateSamples);
        writer.Field("state-drops", snapshot.stateDrops);
        writer.Field("report-loss", snapshot.reportLossTotal);
        writer.Field("lost-seq", snapshot.lastReportLossSequence);
        writer.Field("report-loss-reason-mask", snapshot.reportLossReasonMask);
        writer.Field("overflow-total", snapshot.overflowTotal);
        writer.Text("collector-loss-schema=typed-v1 overflow-meaning=legacy-any-collector-loss ");
        writer.Text("loss-cut=nearby-instant-fuzzy loss-totals=lifetime-primary-plus-accounting-faults ");
        writer.Text("loss-identity=latest-primary-publication ");
        static constexpr std::array<const char*, kCollectorLossKindCount> totalNames{
            "loss-saturation-total", "loss-retry-total", "loss-capacity-total", "loss-invalid-total", "loss-accounting-total"
        };
        static constexpr std::array<const char*, kCollectorLossKindCount> maskNames{
            "loss-saturation-counters", "loss-retry-counters", "loss-capacity-counters", "loss-invalid-counters", "loss-accounting-counters"
        };
        for (std::size_t kind = 0; kind < kCollectorLossKindCount; ++kind)
        {
            writer.Field(totalNames[kind], snapshot.collectorLoss.totals[kind]);
            writer.Field(maskNames[kind], snapshot.collectorLoss.counterMasks[kind]);
        }
        writer.Field("loss-last-id", snapshot.collectorLoss.lastFailureIdentity);
        IdentityFields(writer, "loss-last", snapshot.collectorLoss.lastFailureIdentity);
        writer.Field("loss-accounting-uncertain", snapshot.collectorLoss.accountingUncertain ? 1 : 0);
        writer.Field("loss-accounting-causes", snapshot.collectorLoss.accountingFailureCauseMask);
        writer.Field("loss-accounting-last-id", snapshot.collectorLoss.lastAccountingFailureIdentity);
        IdentityFields(writer, "loss-accounting-last", snapshot.collectorLoss.lastAccountingFailureIdentity);
        writer.Field("drain-overlap-total", snapshot.drainOverlapTotal);
        writer.Field("initialization-violations", snapshot.initializationViolationTotal);
        writer.Field("clock-failures", snapshot.clockFailureTotal);
        writer.Text("callback-gap-bins=[0,1,2,3,4..7,8..15,16..31,32+] callback-gaps=");
        writer.Values(snapshot.callbackGapHistogram);
        writer.Text(" ownership-pre-post-raw-public-rejected=");
        writer.Values(snapshot.ownershipOutcomes);
        writer.Character(' ');

        for (std::size_t i = 0; i < snapshot.metrics.size(); ++i)
        {
            const auto& metric = snapshot.metrics[i];
            if (!Nonzero(metric)) continue;
            writer.Text(MetricName(static_cast<Metric>(i)));
            writer.Character('{');
            writer.Field("entries", metric.entries);
            writer.Field("completed", metric.completed);
            writer.Field("samples", metric.timedSamples);
            writer.Field("ticks", metric.totalTicks);
            writer.Field("units", metric.totalUnits);
            writer.Field("dropped", metric.dropped);
            writer.Field("timing-failures", metric.timingFailures);
            writer.Field("incomplete", metric.incomplete);
            writer.Field("cross-window-completions", metric.crossWindowSamples);
            writer.Field("cross-state-completions", metric.crossStateSamples);
            writer.Field("recursive", metric.recursiveCalls);
            writer.Field("overlap", metric.overlapEntries);
            writer.Field("lifetime-depth", metric.maxDepth);
            writer.Field("active-gauge", metric.activeAtSnapshot);
            writer.Text("bucket-upper-us=");
            if (metric.maxBucketUpperUs == kOpenBucket) writer.Text("open-ended>5000");
            else writer.Number(metric.maxBucketUpperUs);
            writer.Text(" histogram=");
            writer.Values(metric.durationBuckets);
            writer.Text("} ");
        }
        return writer.Finish(written);
    }
}
