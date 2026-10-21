#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/aggregate_timing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace MinimapAggregateTiming;

namespace
{
    constexpr std::size_t kWarmupSamples = 1000;
    constexpr std::size_t kMeasuredSamples = 10000;
    constexpr std::size_t kPercentileCount = 4;

    struct MeasurementSummary
    {
        std::array<std::uint64_t, kPercentileCount> percentiles{};
        std::uint64_t observedMaximum = 0;
        std::size_t accepted = 0;
        std::size_t invalidStart = 0;
        std::size_t invalidEnd = 0;
        std::size_t nonmonotonic = 0;
        std::size_t warmupFailures = 0;
        std::size_t operationFailures = 0;
        bool quantilesAvailable = false;
    };

    bool ReadQpc(std::uint64_t& ticks) noexcept
    {
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value) || value.QuadPart < 0)
            return false;
        ticks = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    bool ReadCollectorQpc(void*, std::uint64_t* ticks) noexcept
    {
        if (ticks == nullptr)
            return false;
        return ReadQpc(*ticks);
    }

    long double Microseconds(std::uint64_t ticks, std::uint64_t frequency) noexcept
    {
        return static_cast<long double>(ticks) * 1000000.0L /
            static_cast<long double>(frequency);
    }

    template <typename Operation>
    MeasurementSummary Measure(const char* name, std::uint64_t frequency,
        Operation operation, std::size_t outputBytes = 0)
    {
        std::array<std::uint64_t, kMeasuredSamples> values{};
        MeasurementSummary result{};

        for (std::size_t i = 0; i < kWarmupSamples; ++i)
        {
            if (!operation(i))
                ++result.warmupFailures;
        }

        for (std::size_t i = 0; i < kMeasuredSamples; ++i)
        {
            std::uint64_t start = 0;
            if (!ReadQpc(start))
            {
                ++result.invalidStart;
                continue;
            }

            const bool operationSucceeded = operation(i);
            if (!operationSucceeded)
                ++result.operationFailures;

            std::uint64_t end = 0;
            if (!ReadQpc(end))
            {
                ++result.invalidEnd;
                continue;
            }
            if (end < start)
            {
                ++result.nonmonotonic;
                continue;
            }
            if (!operationSucceeded)
                continue;

            values[result.accepted++] = end - start;
        }

        if (result.accepted == kMeasuredSamples)
        {
            std::sort(values.begin(), values.end());
            constexpr std::array<std::size_t, kPercentileCount> percentileIndices{
                kMeasuredSamples * 50 / 100,
                kMeasuredSamples * 95 / 100,
                kMeasuredSamples * 99 / 100,
                kMeasuredSamples - 1
            };
            for (std::size_t i = 0; i < percentileIndices.size(); ++i)
                result.percentiles[i] = values[percentileIndices[i]];
            result.observedMaximum = values.back();
            result.quantilesAvailable = true;
        }

        std::cout << std::fixed << std::setprecision(3)
            << "calibration operation=" << name
            << " clock=Release-QPC"
            << " frequency-Hz=" << frequency
            << " warmup=" << kWarmupSamples
            << " samples=" << kMeasuredSamples
            << " attempted=" << kMeasuredSamples
            << " accepted=" << result.accepted
            << " invalid-outer-qpc-start=" << result.invalidStart
            << " invalid-outer-qpc-end=" << result.invalidEnd
            << " nonmonotonic-outer-pairs=" << result.nonmonotonic
            << " warmup-operation-failures=" << result.warmupFailures
            << " measured-operation-failures=" << result.operationFailures
            << " external-qpc-pair=included"
            << " collector-contention=none-single-thread";
        if (result.quantilesAvailable)
        {
            std::cout << " p50-ticks=" << result.percentiles[0]
                << " p95-ticks=" << result.percentiles[1]
                << " p99-ticks=" << result.percentiles[2]
                << " observed-max-ticks=" << result.observedMaximum
                << " p50-us=" << Microseconds(result.percentiles[0], frequency)
                << " p95-us=" << Microseconds(result.percentiles[1], frequency)
                << " p99-us=" << Microseconds(result.percentiles[2], frequency)
                << " observed-max-us=" << Microseconds(result.observedMaximum, frequency);
        }
        else
        {
            std::cout << " quantiles=unavailable";
        }
        if (outputBytes != 0)
            std::cout << " formatted-report-bytes=" << outputBytes;
        std::cout << '\n';

        return result;
    }

    bool MeasurementSucceeded(const MeasurementSummary& result) noexcept
    {
        return result.quantilesAvailable &&
            result.accepted == kMeasuredSamples &&
            result.invalidStart == 0 && result.invalidEnd == 0 &&
            result.nonmonotonic == 0 && result.warmupFailures == 0 &&
            result.operationFailures == 0;
    }

    Snapshot MakeDenseSnapshot(std::uint64_t frequency) noexcept
    {
        Snapshot snapshot{};
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        snapshot.sequence = maximum;
        snapshot.previousSequence = maximum - 1;
        snapshot.windowStartTicks = 0;
        snapshot.windowEndTicks = maximum;
        snapshot.frequency = frequency;
        snapshot.stateDrops = maximum;
        snapshot.activeScopesAtSnapshot = maximum;
        snapshot.totalTimedSamples = maximum;
        snapshot.crossStateSamples = maximum;
        snapshot.reportLossTotal = maximum;
        snapshot.lastReportLossSequence = maximum;
        snapshot.overflowTotal = maximum;
        snapshot.drainOverlapTotal = maximum;
        snapshot.initializationViolationTotal = maximum;
        snapshot.clockFailureTotal = maximum;
        snapshot.reportLossReasonMask = (std::numeric_limits<std::uint32_t>::max)();
        snapshot.stateReasonMask = (std::numeric_limits<std::uint32_t>::max)();
        snapshot.visibilityMask = 0xff;
        snapshot.noticeMask = 0xff;
        snapshot.menuMask = 0xff;
        snapshot.transitionMask = 0xff;
        snapshot.initialized = true;

        for (auto& count : snapshot.callbackGapHistogram)
            count = maximum;
        for (auto& outcome : snapshot.ownershipOutcomes)
            outcome = maximum;
        for (auto& metric : snapshot.metrics)
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
            metric.maxBucketUpperUs = (std::numeric_limits<std::uint32_t>::max)();
            for (auto& bucket : metric.durationBuckets)
                bucket = maximum;
        }
        return snapshot;
    }
}

int main()
{
    LARGE_INTEGER frequencyValue{};
    if (!QueryPerformanceFrequency(&frequencyValue) || frequencyValue.QuadPart <= 0)
    {
        std::cerr << "calibration rejected: QueryPerformanceFrequency failed\n";
        return 2;
    }
    const auto frequency = static_cast<std::uint64_t>(frequencyValue.QuadPart);
    std::uint64_t initialTicks = 0;
    if (!ReadQpc(initialTicks))
    {
        std::cerr << "calibration rejected: initial QueryPerformanceCounter failed\n";
        return 2;
    }

    Collector collector{};
    collector.Initialize(&ReadCollectorQpc, nullptr, frequency, initialTicks);

    bool allMeasurementsValid = true;
    const auto run = [&allMeasurementsValid](const char* name, std::uint64_t qpcFrequency,
        auto operation, std::size_t bytes = 0)
    {
        const auto summary = Measure(name, qpcFrequency, operation, bytes);
        allMeasurementsValid = MeasurementSucceeded(summary) && allMeasurementsValid;
    };

    run("outer-qpc-pair-baseline", frequency, [](std::size_t) { return true; });
    run("counter-count-update", frequency,
        [&collector](std::size_t) { return collector.Count(Metric::CameraCapture); });
    run("raw-duration-record-update", frequency,
        [&collector](std::size_t) { return collector.Record(Metric::BackingFileRead, 1250, 4096); });
    run("scope-timer-counter-aggregation", frequency, [&collector](std::size_t)
    {
        ScopedTimer timer(collector, Metric::CameraCapture);
        return true;
    });
    run("four-timestamp-wrapper-partition", frequency, [&collector](std::size_t)
    {
        MenuWrapperPartition wrapper(collector);
        wrapper.ForwardStarted();
        wrapper.ForwardReturned();
        return true;
    });
    run("timing-failure-bookkeeping", frequency,
        [&collector](std::size_t) { return collector.RecordTimingFailure(Metric::MarkerCapture, false); });

    Snapshot drained{};
    if (!collector.Drain(drained))
    {
        std::cerr << "calibration rejected: initial drain failed\n";
        return 2;
    }
    run("drain-cut-and-delta", frequency, [&collector](std::size_t)
    {
        Snapshot output{};
        return collector.Drain(output);
    });

    std::array<char, kMaxFormattedLine> output{};
    std::size_t written = 0;
    Collector sparseCollector{};
    sparseCollector.Initialize(&ReadCollectorQpc, nullptr, frequency, initialTicks);
    Snapshot sparse{};
    if (!sparseCollector.Drain(sparse) ||
        !FormatSnapshot(sparse, output.data(), output.size(), &written) ||
        written == 0 || written >= output.size())
    {
        std::cerr << "calibration rejected: sparse idle snapshot format failed\n";
        return 2;
    }
    const auto sparseBytes = written;
    run("format-sparse-idle-snapshot", frequency, [&sparse, &output, &written, sparseBytes](std::size_t)
    {
        written = 0;
        return FormatSnapshot(sparse, output.data(), output.size(), &written) &&
            written == sparseBytes && written < output.size() && output[written] == '\0';
    }, sparseBytes);

    const auto dense = MakeDenseSnapshot(frequency);
    if (!FormatSnapshot(dense, output.data(), output.size(), &written) ||
        written == 0 || written >= output.size())
    {
        std::cerr << "calibration rejected: dense snapshot exceeds bounded formatter capacity\n";
        return 2;
    }
    const auto denseBytes = written;
    run("format-dense-max-width-snapshot", frequency, [&dense, &output, &written, denseBytes](std::size_t)
    {
        written = 0;
        return FormatSnapshot(dense, output.data(), output.size(), &written) &&
            written == denseBytes && written < output.size() && output[written] == '\0';
    }, denseBytes);

    std::array<char, kMaxFormattedLine> denseText{};
    if (!FormatSnapshot(dense, denseText.data(), denseText.size(), &written) ||
        written == 0 || written >= denseText.size())
    {
        std::cerr << "calibration rejected: dense text reformat failed\n";
        return 2;
    }
    std::atomic<std::uint64_t> syntheticSinkSubmissions{ 0 };
    run("emission-synthetic-sink-call-only", frequency, [&syntheticSinkSubmissions](std::size_t)
    {
        syntheticSinkSubmissions.fetch_add(1, std::memory_order_relaxed);
        return true;
    }, written);
    std::cout << "calibration emission=sink-call-only physical-game-log-delivery=unverified"
        << " synthetic-submissions=" << syntheticSinkSubmissions.load(std::memory_order_relaxed)
        << " runtime-LoggerSubmission=actual-existing-Log-call-only\n";

    Snapshot finalSnapshot{};
    if (!collector.Drain(finalSnapshot))
    {
        std::cerr << "calibration rejected: final collector diagnostic drain failed\n";
        return 2;
    }
    std::cout << "calibration collector-clock-failures="
        << finalSnapshot.clockFailureTotal << '\n';
    if (finalSnapshot.clockFailureTotal != 0)
    {
        std::cerr << "calibration rejected: collector QPC failure total is nonzero\n";
        return 2;
    }

    return allMeasurementsValid ? 0 : 1;
}
