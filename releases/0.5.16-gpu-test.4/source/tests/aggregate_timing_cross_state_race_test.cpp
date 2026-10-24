#include "../src/aggregate_timing.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

using namespace MinimapAggregateTiming;

namespace
{
    using namespace std::chrono_literals;
    constexpr auto kWaitLimit = 5s;

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

    enum class PausePoint : std::size_t
    {
        ProducerBeforeCrossState,
        DrainAfterTotalTicks,
        ProducerAfterCrossState,
        DrainAfterCrossState,
        ProducerAfterCarry,
        DrainBeforeCarry,
        Count
    };

    class RaceGate final
    {
    public:
        template<typename Predicate>
        bool WaitFor(Predicate predicate)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const bool ready = changed_.wait_for(lock, kWaitLimit, predicate);
            if (!ready) timedOut_.store(true, std::memory_order_relaxed);
            return ready;
        }

        void ProducerScopeReadyAndWait() noexcept
        {
            try
            {
                std::unique_lock<std::mutex> lock(mutex_);
                scopeStarted_ = true;
                changed_.notify_all();
                if (!changed_.wait_for(lock, kWaitLimit, [this] { return allowScopeStop_; }))
                    timedOut_.store(true, std::memory_order_relaxed);
            }
            catch (...)
            {
                timedOut_.store(true, std::memory_order_relaxed);
            }
        }

        bool WaitForScopeStart()
        {
            return WaitFor([this] { return scopeStarted_; });
        }

        bool WaitForPause(PausePoint point)
        {
            const auto index = static_cast<std::size_t>(point);
            return WaitFor([this, index] { return reached_[index]; });
        }

        bool WaitForProducerDone()
        {
            return WaitFor([this] { return producerDone_; });
        }

        bool WaitForDrainDone()
        {
            return WaitFor([this] { return drainDone_; });
        }

        void Pause(PausePoint point) noexcept
        {
            try
            {
                const auto index = static_cast<std::size_t>(point);
                std::unique_lock<std::mutex> lock(mutex_);
                reached_[index] = true;
                changed_.notify_all();
                if (!changed_.wait_for(lock, kWaitLimit, [this, index] { return released_[index]; }))
                    timedOut_.store(true, std::memory_order_relaxed);
            }
            catch (...)
            {
                timedOut_.store(true, std::memory_order_relaxed);
            }
        }

        void AllowScopeStop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            allowScopeStop_ = true;
            changed_.notify_all();
        }

        void Release(PausePoint point)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_[static_cast<std::size_t>(point)] = true;
            changed_.notify_all();
        }

        void ReleaseAll()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            allowScopeStop_ = true;
            released_.fill(true);
            changed_.notify_all();
        }

        void MarkProducerDone()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            producerDone_ = true;
            changed_.notify_all();
        }

        void MarkDrainDone()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            drainDone_ = true;
            changed_.notify_all();
        }

        bool TimedOut() const noexcept
        {
            return timedOut_.load(std::memory_order_relaxed);
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        std::array<bool, static_cast<std::size_t>(PausePoint::Count)> reached_{};
        std::array<bool, static_cast<std::size_t>(PausePoint::Count)> released_{};
        bool scopeStarted_ = false;
        bool allowScopeStop_ = false;
        bool producerDone_ = false;
        bool drainDone_ = false;
        std::atomic<bool> timedOut_{ false };
    };

    RaceGate g_gate;
    std::atomic<bool> g_seamsEnabled{ false };
    const Collector* g_targetCollector = nullptr;
    int g_failures = 0;

    void Check(bool condition, const char* label)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << label << '\n';
        if (!condition) ++g_failures;
    }

    bool Require(bool condition, const char* label)
    {
        Check(condition, label);
        return condition;
    }
}

namespace MinimapAggregateTiming
{
    // Defined only by this focused test executable. Shipped targets omit the seam
    // macro and contain neither this symbol nor the test pause calls.
    void AggregateTimingTestPause(const Collector* collector, Metric metric,
        unsigned point) noexcept
    {
        if (!g_seamsEnabled.load(std::memory_order_acquire) ||
            collector != g_targetCollector)
            return;

        if (metric == Metric::CameraCapture)
        {
            switch (point)
            {
            case 1: g_gate.Pause(PausePoint::ProducerBeforeCrossState); break;
            case 2: g_gate.Pause(PausePoint::ProducerAfterCrossState); break;
            case 3: g_gate.Pause(PausePoint::DrainAfterTotalTicks); break;
            case 4: g_gate.Pause(PausePoint::DrainAfterCrossState); break;
            default: break;
            }
        }
        else if (metric == Metric::Count)
        {
            if (point == 5) g_gate.Pause(PausePoint::DrainBeforeCarry);
            if (point == 6) g_gate.Pause(PausePoint::ProducerAfterCarry);
        }
    }
}

int main()
{
    FakeClock clock{};
    Collector collector;
    collector.Initialize(ReadFakeClock, &clock, 1000, 0);

    StateContext visible{};
    visible.visibility = Visibility::Visible;
    visible.menu = MenuState::Gameplay;
    Check(collector.Observe(visible, 0), "initial visible gameplay state observed");
    clock.ticks.store(2000, std::memory_order_relaxed);
    Snapshot initial{};
    Check(collector.Drain(initial) &&
        initial.stateDisposition == StateDisposition::VisibleSettled,
        "initial state settles before race setup");

    g_targetCollector = &collector;
    g_seamsEnabled.store(true, std::memory_order_release);

    StateContext hidden = visible;
    hidden.visibility = Visibility::Hidden;
    Snapshot interleaved{};
    bool drainReturned = false;
    std::thread producer([&]
    {
        ScopedTimer cameraScope(collector, Metric::CameraCapture);
        g_gate.ProducerScopeReadyAndWait();
        cameraScope.Stop();
        g_gate.MarkProducerDone();
    });

    bool scheduleReady = Require(g_gate.WaitForScopeStart(),
        "producer scope starts before the state transition");
    if (scheduleReady)
    {
        Check(collector.Observe(hidden, 2000), "hidden transition observed during active camera scope");
        clock.ticks.store(2100, std::memory_order_relaxed);
        g_gate.AllowScopeStop();
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::ProducerBeforeCrossState),
            "producer pauses before publishing the cross-state flag");
    }

    std::thread drain;
    if (scheduleReady)
    {
        drain = std::thread([&]
        {
            collector.Drain(interleaved);
            drainReturned = true;
            g_gate.MarkDrainDone();
        });
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::DrainAfterTotalTicks),
            "drain reads total ticks before the producer publishes them");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::ProducerBeforeCrossState);
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::ProducerAfterCrossState),
            "producer publishes cross-state before the drain reads it");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::DrainAfterTotalTicks);
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::DrainAfterCrossState),
            "drain observes cross-state before the duration fields");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::ProducerAfterCrossState);
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::ProducerAfterCarry),
            "producer publishes ticks, sample and carry while the drain is paused");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::DrainAfterCrossState);
        scheduleReady = Require(g_gate.WaitForPause(PausePoint::DrainBeforeCarry),
            "drain reaches the carry read after producer publication");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::ProducerAfterCarry);
        scheduleReady = Require(g_gate.WaitForProducerDone(),
            "producer completes before the drain consumes carry");
    }
    if (scheduleReady)
    {
        g_gate.Release(PausePoint::DrainBeforeCarry);
        scheduleReady = Require(g_gate.WaitForDrainDone(),
            "interleaved drain completes after carry publication");
    }

    // Always release every bounded barrier before joining, including on failure.
    g_gate.ReleaseAll();
    if (producer.joinable()) producer.join();
    if (drain.joinable()) drain.join();
    g_seamsEnabled.store(false, std::memory_order_release);

    Check(!g_gate.TimedOut(), "every deterministic barrier completed within its timeout");
    Check(scheduleReady && drainReturned, "the specified publication/drain interleaving completed");
    if (scheduleReady && drainReturned)
    {
        const auto camera = static_cast<std::size_t>(Metric::CameraCapture);
        Check(interleaved.metrics[camera].totalTicks == 0 &&
            interleaved.metrics[camera].crossStateSamples == 1 &&
            interleaved.stateUpdateInProgressAtCut,
            "first cut records cross-state/carry while its earlier total-ticks read is zero");

        clock.ticks.store(4100, std::memory_order_relaxed);
        Check(collector.Observe(hidden, 4100), "settled hidden state is re-observed for the next window");
        Snapshot next{};
        Check(collector.Drain(next), "next window drains successfully");
        Check(next.metrics[camera].totalTicks == 100 &&
            next.metrics[camera].crossStateSamples == 0,
            "next window exposes the duration fragment after the prior cut consumed cross-state");
        Check(next.stateDisposition == StateDisposition::Unqualified,
            "next window remains unqualified when a cross-state duration fragment moves across the cut");

        clock.ticks.store(6100, std::memory_order_relaxed);
        Check(collector.Observe(hidden, 6100), "idle hidden state is re-observed for recovery");
        Snapshot recovered{};
        Check(collector.Drain(recovered) &&
            recovered.stateDisposition == StateDisposition::HiddenSettled &&
            recovered.metrics[camera].totalTicks == 0 &&
            recovered.metrics[camera].crossStateSamples == 0,
            "later idle hidden window recovers after the carried uncertainty expires");
    }

    return g_failures == 0 ? 0 : 1;
}
