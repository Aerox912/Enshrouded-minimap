#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/aggregate_timing.h"
#include <cstdint>
#include <iostream>

using namespace MinimapAggregateTiming;

struct FakeClock
{
    std::uint64_t ticks = 0;
};

bool ReadFakeClock(void* context, std::uint64_t* ticks) noexcept
{
    if (!context || !ticks) return false;
    *ticks = static_cast<FakeClock*>(context)->ticks;
    return true;
}

int main()
{
    FakeClock clock{ 100 };
    Collector collector;
    collector.Initialize(&ReadFakeClock, &clock, 1000000, 100);

    const bool recorded = collector.Record(Metric::BackingFileRead, 1250, 4096);
    clock.ticks = 15000100;
    Snapshot snapshot{};
    const bool drained = collector.Drain(snapshot);

    const auto& read = snapshot.metrics[static_cast<std::size_t>(Metric::BackingFileRead)];
    const bool aggregatePreserved = recorded && drained &&
        snapshot.sequence == 1 &&
        snapshot.windowStartTicks == 100 &&
        snapshot.windowEndTicks == 15000100 &&
        snapshot.frequency == 1000000 &&
        read.entries == 1 &&
        read.completed == 1 &&
        read.timedSamples == 1 &&
        read.totalTicks == 1250 &&
        read.totalUnits == 4096;

    if (aggregatePreserved)
    {
        std::cout << "PASS aggregate preserves one timed backing read and its byte count\n";
        return 0;
    }

    std::cout << "FAIL aggregate preserves one timed backing read and its byte count"
        << " recorded=" << recorded
        << " drained=" << drained
        << " samples=" << read.timedSamples
        << " ticks=" << read.totalTicks
        << " bytes=" << read.totalUnits << '\n';
    return 1;
}