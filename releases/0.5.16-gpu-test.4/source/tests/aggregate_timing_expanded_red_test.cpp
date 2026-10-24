#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/aggregate_timing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace MinimapAggregateTiming;

namespace
{
    struct FakeClock
    {
        std::atomic<std::uint64_t> ticks{ 0 };
        std::atomic<bool> fail{ false };
    };

    bool ReadFakeClock(void* context, std::uint64_t* ticks) noexcept
    {
        if (context == nullptr || ticks == nullptr)
            return false;
        auto& clock = *static_cast<FakeClock*>(context);
        if (clock.fail.load(std::memory_order_relaxed))
            return false;
        *ticks = clock.ticks.load(std::memory_order_relaxed);
        return true;
    }

    struct BlockingClock
    {
        std::atomic<std::uint64_t> ticks{ 100 };
        std::atomic<bool> blockNext{ false };
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false;
        bool released = false;
    };

    bool ReadBlockingClock(void* context, std::uint64_t* ticks) noexcept
    {
        if (context == nullptr || ticks == nullptr)
            return false;
        auto& clock = *static_cast<BlockingClock*>(context);
        if (clock.blockNext.exchange(false, std::memory_order_acq_rel))
        {
            std::unique_lock<std::mutex> lock(clock.mutex);
            clock.entered = true;
            clock.changed.notify_all();
            clock.changed.wait(lock, [&clock] { return clock.released; });
        }
        *ticks = clock.ticks.load(std::memory_order_relaxed);
        return true;
    }

    int failures = 0;

    void Check(bool ok, const char* name)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok)
            ++failures;
    }

    constexpr std::size_t Index(Metric metric)
    {
        return static_cast<std::size_t>(metric);
    }

    const MetricSnapshot& Get(const Snapshot& snapshot, Metric metric)
    {
        return snapshot.metrics[Index(metric)];
    }

    bool HasReason(const Snapshot& snapshot, StateReason reason)
    {
        return (snapshot.stateReasonMask & StateReasonBit(reason)) != 0;
    }

    void Initialize(Collector& collector, FakeClock& clock,
        std::uint64_t frequency = 1000000)
    {
        collector.Initialize(&ReadFakeClock, &clock, frequency,
            clock.ticks.load(std::memory_order_relaxed));
    }

    void TestBasicCountsAndRawRead()
    {
        FakeClock clock;
        clock.ticks.store(100);
        Collector collector;
        Initialize(collector, clock);

        const bool event = collector.Count(Metric::PresentCallback);
        const bool read = collector.Record(Metric::BackingFileRead, 1250, 4096);
        clock.ticks.store(15000100);
        Snapshot snapshot{};
        const bool drained = collector.Drain(snapshot);
        const auto& present = Get(snapshot, Metric::PresentCallback);
        const auto& backingRead = Get(snapshot, Metric::BackingFileRead);

        Check(event && read && drained, "collector accepts an event, timed read, and drain");
        Check(snapshot.sequence == 1 && snapshot.previousSequence == 0 &&
            snapshot.windowStartTicks == 100 && snapshot.windowEndTicks == 15000100 &&
            snapshot.frequency == 1000000,
            "first drain reports its attempted sequence and raw window bounds");
        Check(present.entries == 1 && present.completed == 1 && present.timedSamples == 0,
            "Count records one completed instantaneous event without fabricating a duration");
        Check(backingRead.entries == 1 && backingRead.completed == 1 &&
            backingRead.timedSamples == 1 && backingRead.totalTicks == 1250 &&
            backingRead.totalUnits == 4096,
            "one timed backing read preserves raw QPC ticks and bytes");
        Check(snapshot.counterCutFuzzy && snapshot.ratesApproximate &&
            snapshot.spanDurationsInclusive && snapshot.loggerSubmissionOnly,
            "snapshot labels fuzzy counters, approximate rates, inclusive spans, and submission-only logging");
    }

    void TestWrapperPartitionsAndOutcomes()
    {
        FakeClock clock;
        clock.ticks.store(100);
        Collector collector;
        Initialize(collector, clock);

        {
            MenuWrapperPartition wrapper(collector);
            clock.ticks.store(200);
            wrapper.ForwardStarted();
            clock.ticks.store(225);
            wrapper.ForwardReturned();
            clock.ticks.store(300);
        }
        clock.ticks.store(400);
        Snapshot completed{};
        const bool drained = collector.Drain(completed);
        Check(drained, "successful wrapper partition drains");
        Check(Get(completed, Metric::MenuCallback).entries == 1 &&
            Get(completed, Metric::MenuCallback).completed == 1,
            "successful native wrapper has one completed callback");
        Check(Get(completed, Metric::OwnershipPreProof).timedSamples == 1 &&
            Get(completed, Metric::OwnershipPreProof).totalTicks == 100 &&
            Get(completed, Metric::OwnershipOriginalForward).timedSamples == 1 &&
            Get(completed, Metric::OwnershipOriginalForward).totalTicks == 25 &&
            Get(completed, Metric::OwnershipPostProof).timedSamples == 1 &&
            Get(completed, Metric::OwnershipPostProof).totalTicks == 75,
            "four wrapper timestamps produce disjoint pre, returned-original, and post spans");
        Check(Get(completed, Metric::ForwardAttempt).entries == 1 &&
            Get(completed, Metric::ForwardReturn).entries == 1,
            "wrapper counts one forward attempt and one returned original");

        FakeClock nullClock;
        nullClock.ticks.store(500);
        Collector nullCollector;
        Initialize(nullCollector, nullClock);
        {
            MenuWrapperPartition wrapper(nullCollector);
            nullClock.ticks.store(550);
            wrapper.NullOriginalExit();
            nullClock.ticks.store(560);
        }
        nullClock.ticks.store(600);
        Snapshot nullSnapshot{};
        const bool nullDrained = nullCollector.Drain(nullSnapshot);
        Check(nullDrained && Get(nullSnapshot, Metric::NullOriginalExit).entries == 1 &&
            Get(nullSnapshot, Metric::ForwardAttempt).entries == 0 &&
            Get(nullSnapshot, Metric::ForwardReturn).entries == 0,
            "null original is counted separately without a fabricated forward");
        Check(Get(nullSnapshot, Metric::OwnershipOriginalForward).timedSamples == 0 &&
            Get(nullSnapshot, Metric::OwnershipPostProof).timedSamples == 0,
            "null original produces no original or post-proof duration");

        FakeClock throwClock;
        throwClock.ticks.store(700);
        Collector throwCollector;
        Initialize(throwCollector, throwClock);
        bool originalExceptionPreserved = false;
        try
        {
            MenuWrapperPartition wrapper(throwCollector);
            throwClock.ticks.store(725);
            wrapper.ForwardStarted();
            throwClock.ticks.store(750);
            throw std::runtime_error("original");
        }
        catch (const std::runtime_error&)
        {
            originalExceptionPreserved = true;
        }
        throwClock.ticks.store(800);
        Snapshot throwSnapshot{};
        const bool throwDrained = throwCollector.Drain(throwSnapshot);
        Check(originalExceptionPreserved,
            "timing scope preserves the original exception and adds no recovery path");
        Check(throwDrained && Get(throwSnapshot, Metric::ForwardAttempt).entries == 1 &&
            Get(throwSnapshot, Metric::ForwardReturn).entries == 0 &&
            Get(throwSnapshot, Metric::OwnershipOriginalForward).timedSamples == 0 &&
            Get(throwSnapshot, Metric::OwnershipOriginalForward).incomplete == 1 &&
            Get(throwSnapshot, Metric::OwnershipPostProof).timedSamples == 0 &&
            Get(throwSnapshot, Metric::MenuCallback).incomplete == 1,
            "throwing original is incomplete and has no successful-forward or post sample");
    }

    void TestInitializationAndClockFailures()
    {
        Collector invalid;
        invalid.Initialize(nullptr, nullptr, 0, 0);
        Check(!invalid.Count(Metric::PresentCallback),
            "invalid first initialization fails closed without recording gameplay telemetry");
        Snapshot invalidSnapshot{};
        Check(!invalid.Drain(invalidSnapshot) && !invalidSnapshot.initialized,
            "invalid clock initialization cannot publish a snapshot");

        FakeClock firstClock;
        firstClock.ticks.store(40);
        FakeClock replacementClock;
        replacementClock.ticks.store(900);
        Collector once;
        Initialize(once, firstClock, 1000000);
        const bool firstEvent = once.Count(Metric::PresentCallback);
        once.Initialize(&ReadFakeClock, &replacementClock, 2000000, 900);
        firstClock.ticks.store(80);
        Snapshot onceSnapshot{};
        const bool onceDrained = once.Drain(onceSnapshot);
        Check(firstEvent && onceDrained && onceSnapshot.frequency == 1000000 &&
            onceSnapshot.windowStartTicks == 40 && onceSnapshot.initializationViolationTotal == 1,
            "second initialization is reported and cannot reset the original clock or window");

        FakeClock failedClock;
        failedClock.ticks.store(100);
        Collector clockFailure;
        Initialize(clockFailure, failedClock);
        failedClock.fail.store(true);
        {
            ScopedTimer timer(clockFailure, Metric::CameraCapture);
        }
        failedClock.fail.store(false);
        failedClock.ticks.store(200);
        Snapshot failedSnapshot{};
        const bool failedDrain = clockFailure.Drain(failedSnapshot);
        Check(failedDrain && Get(failedSnapshot, Metric::CameraCapture).entries == 1 &&
            Get(failedSnapshot, Metric::CameraCapture).timedSamples == 0 &&
            Get(failedSnapshot, Metric::CameraCapture).timingFailures == 1 &&
            failedSnapshot.clockFailureTotal > 0 &&
            HasReason(failedSnapshot, StateReason::ClockFailure),
            "failed clock reads remain visible as failures without a duration sample");

        FakeClock backwardsClock;
        backwardsClock.ticks.store(200);
        Collector backwards;
        Initialize(backwards, backwardsClock);
        {
            ScopedTimer timer(backwards, Metric::MarkerCapture);
            backwardsClock.ticks.store(190);
        }
        backwardsClock.ticks.store(300);
        Snapshot backwardsSnapshot{};
        const bool backwardsDrained = backwards.Drain(backwardsSnapshot);
        Check(backwardsDrained &&
            Get(backwardsSnapshot, Metric::MarkerCapture).timedSamples == 0 &&
            Get(backwardsSnapshot, Metric::MarkerCapture).timingFailures == 1,
            "nonmonotonic timestamps are rejected rather than wrapped into a duration");
    }

    void TestStateAndWindowInterleavings()
    {
        FakeClock clock;
        clock.ticks.store(100);
        Collector collector;
        Initialize(collector, clock, 1000000);

        StateContext visible{};
        visible.visibility = Visibility::Visible;
        visible.menu = MenuState::Gameplay;
        Check(collector.Observe(visible, 100), "visible gameplay observation accepted");

        clock.ticks.store(150);
        ScopedTimer active(collector, Metric::CameraCapture);
        clock.ticks.store(200);
        Snapshot activeSnapshot{};
        const bool activeDrained = collector.Drain(activeSnapshot);
        Check(activeDrained && activeSnapshot.activeScopesAtSnapshot == 1 &&
            HasReason(activeSnapshot, StateReason::ActiveScopes),
            "drain with an active callback marks the window ineligible");

        StateContext hidden = visible;
        hidden.visibility = Visibility::Hidden;
        clock.ticks.store(250);
        Check(collector.Observe(hidden, 250), "hidden transition observation accepted");
        clock.ticks.store(300);
        active.Stop();
        clock.ticks.store(400);
        Snapshot crossed{};
        const bool crossedDrained = collector.Drain(crossed);
        Check(crossedDrained &&
            Get(crossed, Metric::CameraCapture).crossStateSamples == 1 &&
            HasReason(crossed, StateReason::CrossStateWork) &&
            HasReason(crossed, StateReason::Settling) &&
            crossed.stateDisposition == StateDisposition::Unqualified,
            "work spanning a visible-to-hidden change cannot be attributed to pure hidden");

        clock.ticks.store(3000000);
        Check(collector.Observe(hidden, 3000000), "stable hidden observation accepted");
        clock.ticks.store(3000100);
        Snapshot stableHidden{};
        const bool stableDrained = collector.Drain(stableHidden);
        Check(stableDrained && stableHidden.stateDisposition == StateDisposition::HiddenSettled &&
            stableHidden.stateReasonMask == 0 && stableHidden.activeScopesAtSnapshot == 0,
            "a later settled hidden window with no active or cross-state work remains interpretable");
    }

    void TestRecursionConcurrencyAndOverflow()
    {
        FakeClock clockA;
        FakeClock clockB;
        clockA.ticks.store(100);
        clockB.ticks.store(100);
        Collector first;
        Collector second;
        Initialize(first, clockA);
        Initialize(second, clockB);
        {
            ScopedTimer aOuter(first, Metric::CameraCapture);
            {
                ScopedTimer bInner(second, Metric::CameraCapture);
                ScopedTimer aInner(first, Metric::CameraCapture);
                clockA.ticks.store(150);
                clockB.ticks.store(150);
                Snapshot firstActive{};
                Snapshot secondActive{};
                const bool firstDrained = first.Drain(firstActive);
                const bool secondDrained = second.Drain(secondActive);
                Check(firstDrained && secondDrained &&
                    Get(firstActive, Metric::CameraCapture).activeAtSnapshot == 2 &&
                    Get(secondActive, Metric::CameraCapture).activeAtSnapshot == 1,
                    "active-at-snapshot is an instantaneous per-collector gauge");
                Check(Get(firstActive, Metric::CameraCapture).recursiveCalls == 1 &&
                    Get(secondActive, Metric::CameraCapture).recursiveCalls == 0 &&
                    Get(firstActive, Metric::CameraCapture).maxDepth == 2 &&
                    Get(secondActive, Metric::CameraCapture).maxDepth == 1,
                    "recursion depth is keyed by collector instance and maxDepth is lifetime high-water");
            }
        }
        clockA.ticks.store(200);
        Snapshot firstIdle{};
        const bool idleDrained = first.Drain(firstIdle);
        Check(idleDrained && Get(firstIdle, Metric::CameraCapture).activeAtSnapshot == 0 &&
            Get(firstIdle, Metric::CameraCapture).maxDepth == 2,
            "active gauge returns to zero while lifetime maximum depth remains");

        FakeClock parallelClock;
        parallelClock.ticks.store(300);
        Collector parallel;
        Initialize(parallel, parallelClock);
        std::atomic<unsigned> started{ 0 };
        std::atomic<bool> release{ false };
        auto worker = [&] {
            ScopedTimer timer(parallel, Metric::MarkerCapture);
            started.fetch_add(1, std::memory_order_release);
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        };
        std::thread one(worker);
        std::thread two(worker);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (started.load(std::memory_order_acquire) != 2 &&
            std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        parallelClock.ticks.store(350);
        Snapshot overlap{};
        const bool overlapDrained = parallel.Drain(overlap);
        release.store(true, std::memory_order_release);
        one.join();
        two.join();
        Check(overlapDrained && Get(overlap, Metric::MarkerCapture).activeAtSnapshot == 2 &&
            Get(overlap, Metric::MarkerCapture).overlapEntries >= 1 &&
            Get(overlap, Metric::MarkerCapture).maxDepth == 1,
            "concurrent callbacks show overlap without being mislabeled as recursion");

        FakeClock overflowClock;
        overflowClock.ticks.store(400);
        Collector overflow;
        Initialize(overflow, overflowClock);
        const auto max = std::numeric_limits<std::uint64_t>::max();
        const bool firstRecord = overflow.Record(Metric::BackingFileRead, max, max);
        const bool secondRecord = overflow.Record(Metric::BackingFileRead, 1, 1);
        overflowClock.ticks.store(500);
        Snapshot overflowSnapshot{};
        const bool overflowDrained = overflow.Drain(overflowSnapshot);
        const auto& saturated = Get(overflowSnapshot, Metric::BackingFileRead);
        Check(firstRecord && secondRecord && overflowDrained &&
            saturated.totalTicks == max && saturated.totalUnits == max &&
            saturated.dropped > 0 && overflowSnapshot.overflowTotal > 0,
            "counter saturation preserves maxima and exposes telemetry loss");
    }

    void TestOverlappingDrainIsRejected()
    {
        BlockingClock clock;
        Collector collector;
        collector.Initialize(&ReadBlockingClock, &clock, 1000000, 100);
        clock.blockNext.store(true, std::memory_order_release);

        Snapshot firstSnapshot{};
        bool firstResult = false;
        std::thread first([&] { firstResult = collector.Drain(firstSnapshot); });

        bool entered = false;
        {
            std::unique_lock<std::mutex> lock(clock.mutex);
            entered = clock.changed.wait_for(lock, std::chrono::seconds(2),
                [&clock] { return clock.entered; });
        }

        Snapshot rejected{};
        const bool secondResult = collector.Drain(rejected);
        {
            std::lock_guard<std::mutex> lock(clock.mutex);
            clock.released = true;
        }
        clock.changed.notify_all();
        first.join();

        clock.ticks.store(200);
        Snapshot later{};
        const bool laterResult = collector.Drain(later);
        Check(entered && !secondResult && firstResult && laterResult,
            "overlapping drain is rejected without waiting for the active drain");
        Check(later.drainOverlapTotal == 1 && later.sequence >= 3,
            "overlap consumes an attempted sequence and remains visible after the drain completes");
    }

    std::uint64_t QueryTicks()
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return static_cast<std::uint64_t>(value.QuadPart);
    }

    bool ReadQueryClock(void*, std::uint64_t* ticks) noexcept
    {
        if (ticks == nullptr)
            return false;
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value))
            return false;
        *ticks = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    std::uint64_t ToMicroseconds(std::uint64_t ticks, std::uint64_t frequency)
    {
        if (frequency == 0)
            return 0;
        return (ticks / frequency) * 1000000u +
            ((ticks % frequency) * 1000000u) / frequency;
    }

    constexpr std::size_t kCalibrationWarmup = 1000;
    constexpr std::size_t kCalibrationSamples = 10000;

    template <typename Operation>
    void MeasureCalibration(const char* name, std::uint64_t frequency, Operation operation)
    {
        std::array<std::uint64_t, kCalibrationSamples> samples{};
        for (std::size_t i = 0; i < kCalibrationWarmup; ++i)
            operation(i);
        for (std::size_t i = 0; i < kCalibrationSamples; ++i)
        {
            LARGE_INTEGER start{}, end{};
            QueryPerformanceCounter(&start);
            operation(i);
            QueryPerformanceCounter(&end);
            samples[i] = end.QuadPart >= start.QuadPart
                ? static_cast<std::uint64_t>(end.QuadPart - start.QuadPart) : 0;
        }
        std::sort(samples.begin(), samples.end());
        const auto p50 = samples[kCalibrationSamples * 50 / 100];
        const auto p95 = samples[kCalibrationSamples * 95 / 100];
        const auto p99 = samples[kCalibrationSamples * 99 / 100];
        const auto maximum = samples.back();
        std::cout << "calibration operation=" << name
            << " samples=" << kCalibrationSamples
            << " warmup=" << kCalibrationWarmup
            << " frequency=" << frequency
            << " external_qpc_pair=included"
            << " thread=single-uncontended"
            << " p50_us=" << ToMicroseconds(p50, frequency)
            << " p95_us=" << ToMicroseconds(p95, frequency)
            << " p99_us=" << ToMicroseconds(p99, frequency)
            << " observed_max_us=" << ToMicroseconds(maximum, frequency) << '\n';
    }

    void RunCalibration()
    {
        LARGE_INTEGER frequencyValue{};
        QueryPerformanceFrequency(&frequencyValue);
        const auto frequency = static_cast<std::uint64_t>(frequencyValue.QuadPart);
        Collector collector;
        collector.Initialize(&ReadQueryClock, nullptr, frequency, QueryTicks());

        MeasureCalibration("outer-qpc-pair", frequency, [](std::size_t) {});
        MeasureCalibration("scope-counter-aggregation", frequency, [&collector](std::size_t) {
            ScopedTimer timer(collector, Metric::CameraCapture);
        });
        MeasureCalibration("four-timestamp-wrapper-partition", frequency, [&collector](std::size_t) {
            MenuWrapperPartition wrapper(collector);
            wrapper.ForwardStarted();
            wrapper.ForwardReturned();
        });
        MeasureCalibration("timing-failure-bookkeeping", frequency, [&collector](std::size_t) {
            collector.RecordTimingFailure(Metric::MarkerCapture, false);
        });

        Snapshot snapshot{};
        collector.Drain(snapshot);
        MeasureCalibration("drain", frequency, [&collector](std::size_t) {
            Snapshot output{};
            collector.Drain(output);
        });

        std::array<char, kMaxFormattedLine> formatted{};
        std::size_t written = 0;
        collector.Drain(snapshot);
        MeasureCalibration("format", frequency, [&snapshot, &formatted, &written](std::size_t) {
            FormatSnapshot(snapshot, formatted.data(), formatted.size(), &written);
        });

        std::atomic<std::uint64_t> submitted{ 0 };
        MeasureCalibration("emission-test-sink-call-only", frequency, [&submitted](std::size_t) {
            submitted.fetch_add(1, std::memory_order_relaxed);
        });
        std::cout << "calibration note=emission-test-sink-is-not-the-game-logger;"
            << "runtime LoggerSubmission measures the existing Log call; physical delivery is unverified\n";
    }

    void TestDenseFormattingAndPersistentLoss()
    {
        FakeClock clock;
        clock.ticks.store(100);
        Collector collector;
        Initialize(collector, clock);
        collector.Count(Metric::PresentCallback);
        clock.ticks.store(200);
        Snapshot seed{};
        const bool firstDrain = collector.Drain(seed);

        Snapshot dense = seed;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        dense.totalTimedSamples = maximum;
        dense.crossStateSamples = maximum;
        dense.visibilityMask = 0x7;
        dense.noticeMask = 0x3;
        dense.menuMask = 0x7;
        dense.transitionMask = 0x3;
        for (auto& metric : dense.metrics)
        {
            metric.entries = maximum;
            metric.completed = maximum;
            metric.timedSamples = maximum;
            metric.totalTicks = maximum;
            metric.totalUnits = maximum;
            metric.dropped = maximum;
            metric.timingFailures = maximum;
            metric.incomplete = maximum;
            metric.crossWindowSamples = maximum;
            metric.crossStateSamples = maximum;
            metric.recursiveCalls = maximum;
            metric.overlapEntries = maximum;
            metric.maxDepth = maximum;
            metric.activeAtSnapshot = maximum;
            metric.maxBucketUpperUs = 5000;
            metric.durationBuckets.fill(maximum);
        }
        dense.callbackGapHistogram.fill(maximum);
        dense.ownershipOutcomes.fill(maximum);

        std::array<char, kMaxFormattedLine> full{};
        std::size_t written = 0;
        const bool formatted = FormatSnapshot(dense, full.data(), full.size(), &written);
        const std::string line(full.data());
        Check(firstDrain && formatted && written == line.size() && written > 0 && written < full.size() &&
            full[written] == '\0',
            "dense legal max-width snapshot formats completely inside fixed capacity");
        Check(line.find("18446744073709551615") != std::string::npos &&
            line.find("camera") != std::string::npos &&
            line.find("submit-failure") != std::string::npos &&
            line.find(">5000us=open-ended") != std::string::npos,
            "dense output preserves full-width counts, all-metric coverage, and open-ended final bucket");
        Check(line.find("counter-cut=nearby") != std::string::npos &&
            line.find("callback-rate=approx") != std::string::npos &&
            line.find("duration=inclusive") != std::string::npos &&
            line.find("logger=submit-call-only") != std::string::npos,
            "report labels atomic skew, approximate callback rate, inclusive durations, and logger submission");

        std::array<char, 64> tooSmall{};
        tooSmall.fill('x');
        written = 99;
        const bool smallFormatted = FormatSnapshot(dense, tooSmall.data(), tooSmall.size(), &written);
        Check(!smallFormatted && written == 0 && tooSmall[0] == '\0',
            "capacity failure drops the whole report without emitting a partial line");
        const bool lossRecorded = collector.RecordReportLoss(seed.sequence, ReportLossReason::FormatCapacity);
        clock.ticks.store(300);
        Snapshot afterLoss{};
        const bool secondDrain = collector.Drain(afterLoss);
        std::array<char, kMaxFormattedLine> recovered{};
        written = 0;
        const bool recoveredFormatted = FormatSnapshot(afterLoss,
            recovered.data(), recovered.size(), &written);
        const std::string recoveredLine(recovered.data());
        Check(lossRecorded && secondDrain && recoveredFormatted &&
            afterLoss.sequence == seed.sequence + 1 &&
            afterLoss.reportLossTotal == 1 &&
            afterLoss.lastReportLossSequence == seed.sequence,
            "failed formatting advances the drain baseline but loss and attempted sequence persist");
        Check(recoveredLine.find("report-loss=1") != std::string::npos &&
            recoveredLine.find("lost-seq=" + std::to_string(seed.sequence)) != std::string::npos,
            "later successful output carries the prior report loss and lost sequence");
    }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--overhead") == 0)
    {
        RunCalibration();
        return 0;
    }

    TestBasicCountsAndRawRead();
    TestWrapperPartitionsAndOutcomes();
    TestInitializationAndClockFailures();
    TestStateAndWindowInterleavings();
    TestRecursionConcurrencyAndOverflow();
    TestOverlappingDrainIsRejected();
    TestDenseFormattingAndPersistentLoss();

    std::cout << "expanded behavioral red failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}