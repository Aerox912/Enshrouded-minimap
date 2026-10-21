#include "../src/aggregate_timing.h"

namespace MinimapAggregateTiming
{
    void Collector::Initialize(ClockReader clock, void* context,
        std::uint64_t frequency, std::uint64_t start) noexcept
    { clock_ = clock; clockContext_ = context; frequency_ = frequency; windowStartTicks_ = start; }
    bool Collector::Count(Metric) noexcept { return false; }
    bool Collector::Record(Metric, std::uint64_t, std::uint64_t) noexcept { return false; }
    bool Collector::RecordTimingFailure(Metric, bool) noexcept { return false; }
    bool Collector::RecordOwnershipOutcome(OwnershipStage, OwnershipResult) noexcept { return false; }
    bool Collector::Observe(const StateContext&, std::uint64_t) noexcept { return false; }
    void Collector::MarkPresentCallbackMarker() noexcept {}
    bool Collector::Drain(Snapshot&) noexcept { return false; }
    bool Collector::IsValid(Metric) const noexcept { return false; }
    bool Collector::ReadClock(std::uint64_t&) const noexcept { return false; }
    void Collector::Begin(Metric, std::uint64_t&, std::uint64_t&,
        std::uint64_t&, bool&) noexcept {}
    void Collector::Complete(Metric, std::uint64_t, std::uint64_t,
        std::uint64_t, bool, bool, std::uint64_t, bool, std::uint64_t) noexcept {}
    void Collector::CountInternal(Metric) noexcept {}
    void Collector::RecordInternal(Metric, std::uint64_t, std::uint64_t,
        bool, std::uint64_t) noexcept {}
    void Collector::RecordFailureInternal(Metric, bool, std::uint64_t) noexcept {}
    ScopedTimer::ScopedTimer(Collector& c, Metric m) noexcept : collector_(&c), metric_(m) {}
    ScopedTimer::~ScopedTimer() noexcept = default;
    void ScopedTimer::Stop() noexcept {}
    MenuWrapperPartition::MenuWrapperPartition(Collector& c) noexcept : collector_(&c) {}
    MenuWrapperPartition::~MenuWrapperPartition() noexcept = default;
    void MenuWrapperPartition::ForwardStarted() noexcept {}
    void MenuWrapperPartition::ForwardReturned() noexcept {}
    void MenuWrapperPartition::NullOriginalExit() noexcept {}

    const char* MetricName(Metric m) noexcept
    {
        static constexpr const char* names[] = {
            "menu-callback","own-pre","own-forward","own-post","proof-callback-pre",
            "proof-callback-post","proof-admission-update","proof-other","present-callback",
            "present-mutex-acquire","present-local","original-present","submit-gate",
            "cached-camera","camera-fallback","file-mutex-acquire","file-mutex-hold",
            "renderer-mutex-acquire","camera","marker","fog","config","log-poll",
            "overlay-record","widget","notice","submit","cache-warm-hit","cache-cold-attempt",
            "cold-success","cold-failure","file-open-attempt","file-open-success",
            "file-open-failure","file-read","null-original","forward-attempt","forward-return",
            "adjusted-present","invalidate-observation-clear","invalidate-raw-admission",
            "invalidate-module-failure","gate-no-runtime","gate-no-content","gate-no-queue-waits",
            "gate-no-swapchain","gate-renderer-build-failure","submit-success","submit-failure"
        };
        const auto i = static_cast<std::size_t>(m);
        return i < static_cast<std::size_t>(Metric::Count) ? names[i] : "invalid";
    }

    bool FormatSnapshot(const Snapshot&, char* output, std::size_t capacity,
        std::size_t* written) noexcept
    {
        if (written) *written = 0;
        if (output && capacity) output[0] = '\0';
        return false;
    }
}