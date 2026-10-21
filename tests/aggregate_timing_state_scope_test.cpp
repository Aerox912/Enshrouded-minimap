#include "../src/aggregate_timing.h"

#include <atomic>
#include <cstdint>
#include <iostream>

using namespace MinimapAggregateTiming;

namespace
{
    struct FakeClock
    {
        std::atomic<std::uint64_t> ticks{ 0 };
    };

    bool ReadFakeClock(void* context, std::uint64_t* ticks) noexcept
    {
        if (context == nullptr || ticks == nullptr) return false;
        *ticks = static_cast<FakeClock*>(context)->ticks.load(std::memory_order_relaxed);
        return true;
    }

    int failures = 0;

    void Check(bool ok, const char* label)
    {
        std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
        if (!ok) ++failures;
    }

    bool HasReason(const Snapshot& snapshot, StateReason reason)
    {
        return (snapshot.stateReasonMask & StateReasonBit(reason)) != 0;
    }

    void CheckSettledSameStateActive(Visibility visibility, StateDisposition expected,
        const char* label)
    {
        FakeClock clock{};
        Collector collector;
        collector.Initialize(ReadFakeClock, &clock, 1000, 0);

        StateContext state{};
        state.visibility = visibility;
        state.menu = MenuState::Gameplay;
        Check(collector.Observe(state, 0), "same-state initial observation accepted");
        clock.ticks.store(2000, std::memory_order_relaxed);
        Snapshot settled{};
        Check(collector.Drain(settled) && settled.stateDisposition == expected,
            "state is settled before starting same-state active scope");

        ScopedTimer active(collector, Metric::CameraCapture);
        Snapshot during{};
        const bool drained = collector.Drain(during);
        Check(drained && during.stateDisposition == expected &&
            HasReason(during, StateReason::ActiveScopes) &&
            during.activeScopesAtSnapshot == 1,
            label);
        active.Stop();
    }

    void CheckChangedStateActiveDisqualifies()
    {
        FakeClock clock{};
        Collector collector;
        collector.Initialize(ReadFakeClock, &clock, 1000, 0);

        StateContext visible{};
        visible.visibility = Visibility::Visible;
        visible.menu = MenuState::Gameplay;
        Check(collector.Observe(visible, 0), "transition test initial observation accepted");
        clock.ticks.store(2000, std::memory_order_relaxed);
        Snapshot settled{};
        Check(collector.Drain(settled) &&
            settled.stateDisposition == StateDisposition::VisibleSettled,
            "visible state settles before active-scope transition");

        ScopedTimer active(collector, Metric::CameraCapture);
        StateContext hidden = visible;
        hidden.visibility = Visibility::Hidden;
        Check(collector.Observe(hidden, 2000), "state transition observed while old scope remains active");
        clock.ticks.store(2001, std::memory_order_relaxed);
        Snapshot crossed{};
        const bool drained = collector.Drain(crossed);
        Check(drained && crossed.stateDisposition == StateDisposition::Unqualified &&
            HasReason(crossed, StateReason::CrossStateWork) &&
            HasReason(crossed, StateReason::MixedWindow),
            "active token from prior state disqualifies the transitioned window");
        active.Stop();
    }
}

int main()
{
    CheckSettledSameStateActive(Visibility::Hidden, StateDisposition::HiddenSettled,
        "known hidden state stays labelled with ActiveScopes timing limitation");
    CheckSettledSameStateActive(Visibility::Visible, StateDisposition::VisibleSettled,
        "known visible state stays labelled with ActiveScopes timing limitation");
    CheckChangedStateActiveDisqualifies();
    return failures == 0 ? 0 : 1;
}