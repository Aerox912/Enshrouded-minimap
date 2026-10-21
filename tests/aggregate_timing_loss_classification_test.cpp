#include "../src/aggregate_timing.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <thread>

using namespace MinimapAggregateTiming;

#if !defined(MINIMAP_AGGREGATE_TIMING_LOSS_TEST_SEAMS)
#error This focused regression requires the compiled-out loss test seams.
#endif

namespace MinimapAggregateTiming
{
    struct LossClassificationTestAccess
    {
        static const std::atomic<std::uint64_t>* ProducerVersion(const Collector& collector) noexcept
        { return &collector.producerVersion_; }

        static void SeedLegacyLossAtMaximum(Collector& collector) noexcept
        { collector.overflowTotal_.store((std::numeric_limits<std::uint64_t>::max)()); }

        static void SeedCallbackEntriesAtMaximum(Collector& collector) noexcept
        { collector.callbackEntries_.store((std::numeric_limits<std::uint64_t>::max)()); }

        static constexpr std::size_t TokenCapacity() noexcept
        { return Collector::kActiveTokenCount; }

        static const std::atomic<std::uint64_t>* TypedTotal(const Collector& collector,
            CollectorLossKind kind) noexcept
        { return &collector.lossTotals_[static_cast<std::size_t>(kind)]; }

        static void SeedTypedTotalMaximum(Collector& collector, CollectorLossKind kind) noexcept
        { collector.lossTotals_[static_cast<std::size_t>(kind)].store((std::numeric_limits<std::uint64_t>::max)()); }

        static void SeedTypedEvidenceOnly(Collector& collector) noexcept
        {
            const auto kind = static_cast<std::size_t>(CollectorLossKind::Saturation);
            collector.lossTotals_[kind].store(1);
            collector.lossCounterMasks_[kind].store(
                std::uint64_t{ 1 } << static_cast<unsigned>(LossCounter::MetricTotalTicks));
        }

        static void SeedGapMaximum(Collector& collector) noexcept
        {
            collector.callbackEntries_.store(32);
            collector.callbackGapHistogram_[7].store((std::numeric_limits<std::uint64_t>::max)());
        }
    };
}

namespace
{
    using namespace std::chrono_literals;
    constexpr auto kWaitLimit = 5s;
    constexpr auto kMaximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr unsigned kAttemptCount = 8;
    int behaviorFailures = 0, harnessFailures = 0;

    void Check(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        if (!condition) ++behaviorFailures;
    }

    void Require(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "HARNESS-FAIL ") << name << '\n';
        if (!condition) ++harnessFailures;
    }

    struct FakeClock { std::atomic<std::uint64_t> ticks{ 0 }; };

    bool ReadFakeClock(void* context, std::uint64_t* ticks) noexcept
    {
        if (context == nullptr || ticks == nullptr) return false;
        *ticks = static_cast<FakeClock*>(context)->ticks.load(std::memory_order_relaxed);
        return true;
    }

    void InitializeHidden(Collector& collector, FakeClock& clock)
    {
        collector.Initialize(ReadFakeClock, &clock, 1000, 0);
        StateContext hidden{};
        hidden.visibility = Visibility::Hidden;
        hidden.menu = MenuState::Gameplay;
        Require(collector.Observe(hidden, 0), "known hidden gameplay observation is accepted");
        clock.ticks.store(3000, std::memory_order_relaxed);
    }

    constexpr std::size_t Kind(CollectorLossKind kind) noexcept
    { return static_cast<std::size_t>(kind); }

    constexpr std::uint64_t Mask(LossCounter counter) noexcept
    { return std::uint64_t{ 1 } << static_cast<unsigned>(counter); }

    constexpr std::uint64_t Identity(CollectorLossKind kind, LossCounter counter,
        Metric metric = Metric::Count, unsigned member = 0) noexcept
    {
        const auto metricId = metric == Metric::Count ? 255u : static_cast<unsigned>(metric);
        return (std::uint64_t{ 1 } << 63) | static_cast<std::uint64_t>(kind) |
            (static_cast<std::uint64_t>(counter) << 3) |
            (static_cast<std::uint64_t>(metricId) << 9) |
            (static_cast<std::uint64_t>(member) << 17);
    }

    bool LossExcluded(const Snapshot& snapshot) noexcept
    {
        return snapshot.stateDisposition == StateDisposition::Unqualified &&
            (snapshot.stateReasonMask & StateReasonBit(StateReason::ReportLoss)) != 0;
    }

    class ContentionGate final
    {
    public:
        explicit ContentionGate(const std::atomic<std::uint64_t>* target) noexcept : target_(target) {}

        void SetVictim()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            victim_ = std::this_thread::get_id();
        }

        void Before(const std::atomic<std::uint64_t>* counter, std::uint64_t expected,
            std::uint64_t desired, unsigned attempt) noexcept
        {
            if (counter != target_) return;
            try
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!armed_ || std::this_thread::get_id() != victim_ || attempt >= kAttemptCount) return;
                expected_[attempt] = expected;
                incrementShape_[attempt] = expected != kMaximum && desired == expected + 1;
                requested_ = attempt + 1;
                changed_.notify_all();
                if (!changed_.wait_for(lock, kWaitLimit, [this, attempt] {
                    return released_ > attempt || canceled_;
                }))
                {
                    timedOut_ = true;
                    canceled_ = true;
                    armed_ = false;
                    changed_.notify_all();
                }
            }
            catch (...) { timedOut_.store(true, std::memory_order_relaxed); }
        }

        void After(const std::atomic<std::uint64_t>* counter, unsigned attempt, bool exchanged) noexcept
        {
            if (counter != target_) return;
            try
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!armed_ || std::this_thread::get_id() != victim_ || attempt >= kAttemptCount) return;
                failed_[attempt] = !exchanged;
                changedExpected_[attempt] = counter->load(std::memory_order_seq_cst) != expected_[attempt];
                if (attempt + 1 == kAttemptCount) armed_ = false;
            }
            catch (...) { timedOut_.store(true, std::memory_order_relaxed); }
        }

        bool AwaitRequest(unsigned request)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool ready = changed_.wait_for(lock, kWaitLimit, [this, request] {
                return requested_ >= request || finished_ || canceled_;
            });
            if (!ready) timedOut_ = true;
            return ready && requested_ >= request && !canceled_;
        }

        void Release(unsigned request)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = request;
            changed_.notify_all();
        }

        void Finish()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
            changed_.notify_all();
        }

        bool AwaitFinish()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool ready = changed_.wait_for(lock, kWaitLimit, [this] { return finished_; });
            if (!ready) timedOut_ = true;
            return ready;
        }

        void ReleaseAll()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            canceled_ = true;
            armed_ = false;
            released_ = kAttemptCount;
            changed_.notify_all();
        }

        bool ProvedEightRealConflicts() const noexcept
        {
            for (unsigned i = 0; i < kAttemptCount; ++i)
                if (!failed_[i] || !changedExpected_[i] || !incrementShape_[i]) return false;
            return true;
        }

        bool TimedOut() const noexcept { return timedOut_.load(std::memory_order_relaxed); }

    private:
        const std::atomic<std::uint64_t>* target_;
        std::mutex mutex_;
        std::condition_variable changed_;
        std::thread::id victim_{};
        std::array<std::uint64_t, kAttemptCount> expected_{};
        std::array<bool, kAttemptCount> incrementShape_{}, failed_{}, changedExpected_{};
        unsigned requested_ = 0, released_ = 0;
        bool armed_ = true, canceled_ = false, finished_ = false;
        std::atomic<bool> timedOut_{ false };
    };

    std::atomic<ContentionGate*> activeGate{ nullptr };

    void TestRealProducerVersionContention()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        ContentionGate gate(LossClassificationTestAccess::ProducerVersion(collector));
        activeGate.store(&gate, std::memory_order_release);
        bool victimAccepted = false;
        std::thread victim([&] {
            gate.SetVictim();
            victimAccepted = collector.Count(Metric::ConfigPoll);
            gate.Finish();
        });

        unsigned contenders = 0;
        bool scheduleCompleted = true;
        for (unsigned request = 1; request <= kAttemptCount; ++request)
        {
            if (!gate.AwaitRequest(request)) { scheduleCompleted = false; break; }
            // Real competing collector calls change the same shared version.
            // No atomic value, CAS result, or collector return is substituted.
            if (!collector.Count(Metric::PresentCallback)) { scheduleCompleted = false; break; }
            ++contenders;
            gate.Release(request);
        }
        const bool finished = gate.AwaitFinish();
        gate.ReleaseAll();
        victim.join();
        activeGate.store(nullptr, std::memory_order_release);
        Require(scheduleCompleted && finished && !gate.TimedOut(),
            "real producer-version contention completes every bounded barrier");
        Require(gate.ProvedEightRealConflicts(),
            "eight actual CAS failures follow eight real expected-value changes");
        Require(victimAccepted && contenders == kAttemptCount,
            "victim and eight competing Count calls return normally");

        Snapshot snapshot{};
        Require(collector.Drain(snapshot), "contention snapshot drains normally");
        const auto& victimMetric = snapshot.metrics[static_cast<std::size_t>(Metric::ConfigPoll)];
        const auto& contenderMetric = snapshot.metrics[static_cast<std::size_t>(Metric::PresentCallback)];
        Require(victimMetric.entries == 1 && victimMetric.completed == 1 && victimMetric.dropped == 0 &&
            contenderMetric.entries == kAttemptCount && contenderMetric.completed == kAttemptCount &&
            contenderMetric.dropped == 0 && snapshot.overflowTotal == 1,
            "reproduced shared bookkeeping loss preserves metric events and exposes one legacy loss");
        Check(snapshot.collectorLoss.totals[Kind(CollectorLossKind::RetryExhaustion)] == 1 &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 0,
            "loss classification distinguishes real eight-CAS exhaustion from saturation");
        Check(snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::RetryExhaustion)] == Mask(LossCounter::ProducerVersion),
            "loss classification names the actual producer-version bookkeeping counter");
        Check(snapshot.collectorLoss.lastFailureIdentity ==
            Identity(CollectorLossKind::RetryExhaustion, LossCounter::ProducerVersion),
            "loss identity atomically encodes retry kind counter and no-metric sentinel");
        Check(!snapshot.collectorLoss.accountingUncertain && LossExcluded(snapshot),
            "retry loss keeps existing lifetime exclusion without inventing accounting loss");

        clock.ticks.store(6000, std::memory_order_relaxed);
        Snapshot idle{};
        Require(collector.Drain(idle), "idle post-contention snapshot drains");
        Check(idle.overflowTotal == 1 && LossExcluded(idle) &&
            idle.collectorLoss.totals[Kind(CollectorLossKind::RetryExhaustion)] == 1 &&
            idle.collectorLoss.counterMasks[Kind(CollectorLossKind::RetryExhaustion)] == Mask(LossCounter::ProducerVersion) &&
            idle.collectorLoss.lastFailureIdentity == snapshot.collectorLoss.lastFailureIdentity,
            "typed loss count counter mask and identity persist without qualifying a later idle window");
    }

    void TestTrueSaturationAndFormatLoss()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        Require(collector.Record(Metric::BackingFileRead, kMaximum, kMaximum) &&
            collector.Record(Metric::BackingFileRead, 1, 1), "raw saturation records return normally");
        Snapshot snapshot{};
        Require(collector.Drain(snapshot), "saturation snapshot drains");
        const auto& metric = snapshot.metrics[static_cast<std::size_t>(Metric::BackingFileRead)];
        Require(metric.totalTicks == kMaximum && metric.totalUnits == kMaximum &&
            metric.entries == 2 && metric.completed == 2 && metric.timedSamples == 2 &&
            metric.dropped == 2 && snapshot.overflowTotal == 2,
            "real arithmetic saturation clamps raw totals and preserves legacy dropped evidence");
        Check(snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 2 &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::RetryExhaustion)] == 0,
            "loss classification reports two arithmetic saturations and no retry exhaustion");
        Check(snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::Saturation)] ==
            (Mask(LossCounter::MetricTotalTicks) | Mask(LossCounter::MetricTotalUnits)),
            "saturation counter mask distinguishes duration ticks from operation units");
        Check(snapshot.collectorLoss.lastFailureIdentity ==
            Identity(CollectorLossKind::Saturation, LossCounter::MetricTotalUnits, Metric::BackingFileRead),
            "packed saturation identity retains the backing-read metric");
        Check(LossExcluded(snapshot) && !snapshot.collectorLoss.accountingUncertain,
            "true saturation preserves conservative exclusion with reliable loss accounting");

        std::array<char, 64> small{};
        small.fill('x');
        std::size_t written = 123;
        Require(!FormatSnapshot(snapshot, small.data(), small.size(), &written) &&
            written == 0 && small[0] == '\0', "small capacity emits no partial classification report");
        Require(collector.RecordReportLoss(snapshot.sequence, ReportLossReason::FormatCapacity),
            "failed classification report records existing format-capacity loss");
        clock.ticks.store(6000, std::memory_order_relaxed);
        Snapshot later{};
        Require(collector.Drain(later) && later.reportLossTotal == 1 &&
            later.lastReportLossSequence == snapshot.sequence && LossExcluded(later),
            "failed formatting remains visible and never qualifies the following window");
        Check(later.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 2 &&
            later.collectorLoss.counterMasks[Kind(CollectorLossKind::Saturation)] ==
                (Mask(LossCounter::MetricTotalTicks) | Mask(LossCounter::MetricTotalUnits)) &&
            later.collectorLoss.lastFailureIdentity == snapshot.collectorLoss.lastFailureIdentity,
            "typed saturation history survives baseline advancement and failed formatting");
    }

    void TestBookkeepingSaturation()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        LossClassificationTestAccess::SeedCallbackEntriesAtMaximum(collector);
        Require(collector.Count(Metric::MenuCallback), "callback event returns normally at seeded bookkeeping maximum");
        Snapshot snapshot{};
        Require(collector.Drain(snapshot), "bookkeeping saturation snapshot drains");
        const auto& metric = snapshot.metrics[static_cast<std::size_t>(Metric::MenuCallback)];
        Require(metric.entries == 1 && metric.completed == 1 && metric.dropped == 0 && snapshot.overflowTotal == 1,
            "actual callback bookkeeping saturation retains zero per-metric drops");
        Check(snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 1 &&
            snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::Saturation)] == Mask(LossCounter::CallbackEntries) &&
            snapshot.collectorLoss.lastFailureIdentity == Identity(CollectorLossKind::Saturation, LossCounter::CallbackEntries),
            "typed bookkeeping saturation identifies callback entries without a metric attribution");
    }

    void TestCapacityAndInvalidUsage()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        constexpr auto count = LossClassificationTestAccess::TokenCapacity() + 1;
        struct alignas(ScopedTimer) Storage { std::array<std::byte, sizeof(ScopedTimer)> bytes{}; };
        std::array<Storage, count> storage{};
        std::array<ScopedTimer*, count> scopes{};
        for (std::size_t i = 0; i < count; ++i)
            scopes[i] = new (static_cast<void*>(storage[i].bytes.data())) ScopedTimer(collector, Metric::CameraCapture);
        for (std::size_t i = count; i != 0; --i) scopes[i - 1]->~ScopedTimer();
        Snapshot snapshot{};
        Require(collector.Drain(snapshot), "fixed token-capacity snapshot drains after normal scope destruction");
        const auto& metric = snapshot.metrics[static_cast<std::size_t>(Metric::CameraCapture)];
        Require(metric.entries == count && metric.completed == count - 1 && metric.dropped == 1 &&
            metric.activeAtSnapshot == 0 && snapshot.overflowTotal == 1,
            "one real token-capacity loss is bounded and releases every admitted scope");
        Check(snapshot.collectorLoss.totals[Kind(CollectorLossKind::Capacity)] == 1 &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 0 &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::RetryExhaustion)] == 0 &&
            snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::Capacity)] == Mask(LossCounter::ScopeTokenCapacity) &&
            snapshot.collectorLoss.lastFailureIdentity ==
                Identity(CollectorLossKind::Capacity, LossCounter::ScopeTokenCapacity, Metric::CameraCapture),
            "capacity loss has its own kind counter and metric identity");

        Collector invalid;
        FakeClock invalidClock;
        InitializeHidden(invalid, invalidClock);
        Require(!invalid.Count(Metric::Count), "invalid metric call is rejected by the existing API");
        Snapshot rejected{};
        Require(invalid.Drain(rejected) && rejected.overflowTotal == 1 && LossExcluded(rejected),
            "invalid usage retains existing legacy loss and exclusion");
        Check(rejected.collectorLoss.totals[Kind(CollectorLossKind::InvalidUsage)] == 1 &&
            rejected.collectorLoss.counterMasks[Kind(CollectorLossKind::InvalidUsage)] == Mask(LossCounter::InvalidMetric),
            "invalid usage is distinct from saturation and retry exhaustion");
    }

    void TestNonrecursiveAccountingFallback()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        LossClassificationTestAccess::SeedLegacyLossAtMaximum(collector);
        Require(collector.Record(Metric::BackingFileRead, kMaximum) &&
            collector.Record(Metric::BackingFileRead, 1), "source fault returns normally when loss accounting is already saturated");
        Snapshot snapshot{};
        Require(collector.Drain(snapshot) && snapshot.overflowTotal == kMaximum && LossExcluded(snapshot),
            "existing nonrecursive accounting maximum remains visible and excluded");
        Check(snapshot.collectorLoss.accountingUncertain &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::AccountingFailure)] == 1 &&
            (snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::AccountingFailure)] & Mask(LossCounter::LossAccountingTotal)) != 0,
            "loss accounting fallback explicitly exposes uncertainty without recursive fault recording");
        Check(snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 1 &&
            snapshot.collectorLoss.lastFailureIdentity ==
                Identity(CollectorLossKind::Saturation, LossCounter::MetricTotalTicks, Metric::BackingFileRead),
            "accounting fallback retains primary fault kind and metric identity");
    }

    void TestTypedAccountingSaturation()
    {
        for (unsigned fallbackAlsoFull = 0; fallbackAlsoFull != 2; ++fallbackAlsoFull)
        {
            Collector collector;
            FakeClock clock;
            InitializeHidden(collector, clock);
            LossClassificationTestAccess::SeedTypedTotalMaximum(collector, CollectorLossKind::Saturation);
            if (fallbackAlsoFull != 0)
                LossClassificationTestAccess::SeedTypedTotalMaximum(collector, CollectorLossKind::AccountingFailure);
            Require(collector.Record(Metric::BackingFileRead, kMaximum) &&
                collector.Record(Metric::BackingFileRead, 1), "source fault completes with saturated typed accounting");
            Snapshot snapshot{};
            Require(collector.Drain(snapshot) && snapshot.overflowTotal == 1 && LossExcluded(snapshot),
                "typed accounting faults preserve the exact legacy source-loss count and exclusion");
            const auto expectedAccountingTotal = fallbackAlsoFull != 0 ? kMaximum : 1;
            const auto member = fallbackAlsoFull != 0
                ? CollectorLossKind::AccountingFailure : CollectorLossKind::Saturation;
            Check(snapshot.collectorLoss.accountingUncertain &&
                snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == kMaximum &&
                snapshot.collectorLoss.totals[Kind(CollectorLossKind::AccountingFailure)] == expectedAccountingTotal &&
                (snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::AccountingFailure)] & Mask(LossCounter::TypedLossTotal)) != 0 &&
                (snapshot.collectorLoss.accountingFailureCauseMask & (std::uint64_t{ 1 } << Kind(CollectorLossKind::Saturation))) != 0,
                "typed total saturation and fallback-of-fallback remain bounded and visibly uncertain");
            Check(snapshot.collectorLoss.lastAccountingFailureIdentity ==
                Identity(CollectorLossKind::Saturation, LossCounter::TypedLossTotal, Metric::Count, static_cast<unsigned>(member)) &&
                snapshot.collectorLoss.lastFailureIdentity ==
                    Identity(CollectorLossKind::Saturation, LossCounter::MetricTotalTicks, Metric::BackingFileRead),
                "nested accounting identity identifies the failing kind-member without replacing the primary fault");
        }
    }

    void TestRealTypedAccountingContention()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        Require(collector.Record(Metric::BackingFileRead, kMaximum), "typed accounting contention prepares a real saturation source");
        LossClassificationTestAccess::SeedCallbackEntriesAtMaximum(collector);
        ContentionGate gate(LossClassificationTestAccess::TypedTotal(collector, CollectorLossKind::Saturation));
        activeGate.store(&gate, std::memory_order_release);
        bool victimAccepted = false;
        std::thread victim([&] {
            gate.SetVictim();
            victimAccepted = collector.Record(Metric::BackingFileRead, 1);
            gate.Finish();
        });
        unsigned contenders = 0;
        bool scheduleCompleted = true;
        for (unsigned request = 1; request <= kAttemptCount; ++request)
        {
            if (!gate.AwaitRequest(request)) { scheduleCompleted = false; break; }
            // Each real saturated callback Count advances the same typed total.
            if (!collector.Count(Metric::MenuCallback)) { scheduleCompleted = false; break; }
            ++contenders;
            gate.Release(request);
        }
        const bool finished = gate.AwaitFinish();
        gate.ReleaseAll();
        victim.join();
        activeGate.store(nullptr, std::memory_order_release);
        Require(scheduleCompleted && finished && !gate.TimedOut() && gate.ProvedEightRealConflicts(),
            "typed accounting exhaustion follows eight real contending source faults with bounded barriers");
        Require(victimAccepted && contenders == kAttemptCount, "all source operations return despite typed accounting exhaustion");
        Snapshot snapshot{};
        Require(collector.Drain(snapshot) && snapshot.overflowTotal == kAttemptCount + 1 && LossExcluded(snapshot),
            "typed accounting retry failure retains every legacy source-loss event");
        Check(snapshot.collectorLoss.accountingUncertain &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == kMaximum &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::AccountingFailure)] == 1 &&
            snapshot.collectorLoss.totals[Kind(CollectorLossKind::RetryExhaustion)] == 0 &&
            snapshot.collectorLoss.accountingFailureCauseMask ==
                (std::uint64_t{ 1 } << Kind(CollectorLossKind::RetryExhaustion)),
            "typed accounting retry failure uses an unknown-count sentinel and a distinct nested cause");
        Check(snapshot.collectorLoss.lastAccountingFailureIdentity ==
            Identity(CollectorLossKind::RetryExhaustion, LossCounter::TypedLossTotal, Metric::Count,
                static_cast<unsigned>(CollectorLossKind::Saturation)) &&
            snapshot.collectorLoss.lastFailureIdentity ==
                Identity(CollectorLossKind::Saturation, LossCounter::MetricTotalTicks, Metric::BackingFileRead),
            "actual typed CAS failure retains accounting counter member and original metric identity");
    }

    void TestGapIdentityAndTypedVeto()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        LossClassificationTestAccess::SeedGapMaximum(collector);
        collector.MarkPresentCallbackMarker();
        Snapshot snapshot{};
        Require(collector.Drain(snapshot) && snapshot.overflowTotal == 1 && LossExcluded(snapshot),
            "real callback-gap saturation preserves legacy exclusion");
        Check(snapshot.collectorLoss.counterMasks[Kind(CollectorLossKind::Saturation)] == Mask(LossCounter::CallbackGapHistogram) &&
            snapshot.collectorLoss.lastFailureIdentity ==
                Identity(CollectorLossKind::Saturation, LossCounter::CallbackGapHistogram, Metric::Count, 7),
            "callback-gap identity exposes its stable counter and exact histogram member");

        Collector typedOnly;
        FakeClock typedClock;
        InitializeHidden(typedOnly, typedClock);
        LossClassificationTestAccess::SeedTypedEvidenceOnly(typedOnly);
        Snapshot typed{};
        Require(typedOnly.Drain(typed) && typed.overflowTotal == 0, "typed-only cut fixture retains zero legacy overflow");
        Check(typed.collectorLoss.totals[Kind(CollectorLossKind::Saturation)] == 1 && LossExcluded(typed),
            "typed loss evidence independently vetoes qualification when legacy overflow was not observed");

        Collector clean;
        FakeClock cleanClock;
        InitializeHidden(clean, cleanClock);
        Snapshot healthy{};
        Require(clean.Drain(healthy), "clean-state classification control drains");
        Check(healthy.stateDisposition == StateDisposition::HiddenSettled && healthy.stateReasonMask == 0 &&
            healthy.overflowTotal == 0 && !healthy.collectorLoss.accountingUncertain &&
            healthy.collectorLoss.lastFailureIdentity == 0,
            "classification does not disqualify clean settled hidden reporting");
    }

    void TestDenseClassificationFormatting()
    {
        Collector collector;
        FakeClock clock;
        InitializeHidden(collector, clock);
        Snapshot dense{};
        Require(collector.Drain(dense), "dense formatter seed is a valid collector snapshot");
        dense.collectorLoss.totals.fill(kMaximum);
        dense.collectorLoss.counterMasks.fill(kMaximum);
        dense.collectorLoss.lastFailureIdentity = kMaximum;
        dense.collectorLoss.accountingUncertain = true;
        dense.collectorLoss.accountingFailureCauseMask = kMaximum;
        dense.collectorLoss.lastAccountingFailureIdentity = kMaximum;
        dense.stateDisposition = StateDisposition::Unqualified;
        dense.stateReasonMask |= StateReasonBit(StateReason::ReportLoss);
        for (auto& metric : dense.metrics)
        {
            metric.entries = metric.completed = metric.timedSamples = kMaximum;
            metric.totalTicks = metric.totalUnits = metric.dropped = kMaximum;
            metric.timingFailures = metric.incomplete = kMaximum;
            metric.crossWindowSamples = metric.crossStateSamples = kMaximum;
            metric.recursiveCalls = metric.overlapEntries = kMaximum;
            metric.maxDepth = metric.activeAtSnapshot = kMaximum;
            metric.maxBucketUpperUs = (std::numeric_limits<std::uint32_t>::max)();
            metric.durationBuckets.fill(kMaximum);
        }
        dense.callbackGapHistogram.fill(kMaximum);
        dense.ownershipOutcomes.fill(kMaximum);
        std::array<char, kMaxFormattedLine> output{};
        std::size_t written = 0;
        Require(FormatSnapshot(dense, output.data(), output.size(), &written) &&
            written != 0 && written < output.size() && written == std::strlen(output.data()) && output[written] == '\0',
            "max-width metrics plus typed metadata format completely within fixed capacity");
        Check(std::strstr(output.data(), "collector-loss-schema=typed-v1") != nullptr &&
            std::strstr(output.data(), "overflow-meaning=legacy-any-collector-loss") != nullptr,
            "formatter versions additive classification and discloses legacy overflow meaning");
        Check(std::strstr(output.data(), "loss-saturation-total=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-retry-total=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-capacity-total=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-invalid-total=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-accounting-total=18446744073709551615") != nullptr,
            "formatter preserves full-width per-kind loss totals");
        Check(std::strstr(output.data(), "loss-saturation-counters=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-retry-counters=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-last-id=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-accounting-uncertain=1") != nullptr &&
            std::strstr(output.data(), "loss-accounting-causes=18446744073709551615") != nullptr &&
            std::strstr(output.data(), "loss-accounting-last-id=18446744073709551615") != nullptr,
            "formatter preserves sticky counter masks packed identity and accounting uncertainty");
    }
}

namespace MinimapAggregateTiming
{
    void AggregateTimingLossTestBeforeCas(const std::atomic<std::uint64_t>* counter,
        std::uint64_t expected, std::uint64_t desired, unsigned attempt) noexcept
    {
        if (auto* gate = activeGate.load(std::memory_order_acquire)) gate->Before(counter, expected, desired, attempt);
    }

    void AggregateTimingLossTestAfterCas(const std::atomic<std::uint64_t>* counter,
        unsigned attempt, bool exchanged) noexcept
    {
        if (auto* gate = activeGate.load(std::memory_order_acquire)) gate->After(counter, attempt, exchanged);
    }
}

int main()
{
    TestRealProducerVersionContention();
    TestTrueSaturationAndFormatLoss();
    TestBookkeepingSaturation();
    TestCapacityAndInvalidUsage();
    TestNonrecursiveAccountingFallback();
    TestTypedAccountingSaturation();
    TestRealTypedAccountingContention();
    TestGapIdentityAndTypedVeto();
    TestDenseClassificationFormatting();
    std::cout << "loss-classification behavior-failures=" << behaviorFailures
        << " harness-failures=" << harnessFailures << '\n';
    return harnessFailures != 0 ? 2 : behaviorFailures != 0 ? 1 : 0;
}
