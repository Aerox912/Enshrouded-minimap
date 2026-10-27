#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>

// Independent of aggregate_timing. No allocation, logger, shared per-event RMW,
// or game state reads. Lifetime thread slots are deliberately never recycled.
namespace MinimapBackgroundDiagnostic
{
    inline constexpr unsigned kThreadSlots = 128;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "Background diagnostic requires lock-free 64-bit publication");
    enum class Metric : unsigned {
        MarkerHook, MarkerDispatchHook, WorldMapHook, RemotePlayerHook, UiSetupHook,
        WaypointHook, ClockHook, WeatherCapture, CachePrune,
        CollectorBeginMarker, CollectorEndMarker, CollectorBeginOther, CollectorEndOther,
        CacheHit, CacheMiss, CacheFastPath, CachePopulation, PrunedMarkers,
        PositionResolve, WorldMapRows, RemotePlayers, Waypoints,
        PositionResolveWork, PositionResolveSuccess, PositionResolveFailure, PositionResolveDeferred, Count
    };
    inline constexpr unsigned kMetrics = static_cast<unsigned>(Metric::Count);
    inline constexpr const char* kNames[kMetrics] = {
        "marker-hook", "marker-dispatch", "world-map-hook", "remote-player-hook", "ui-setup-hook",
        "waypoint-hook", "clock-hook", "weather-capture", "cache-prune",
        "collector-begin-marker", "collector-end-marker", "collector-begin-other", "collector-end-other",
        "cache-hit", "cache-miss", "cache-fast-path", "cache-population", "pruned-markers",
        "position-resolve", "world-map-rows", "remote-players", "waypoints",
        "position-resolve-work", "position-resolve-success", "position-resolve-failure", "position-resolve-deferred"
    };
    using ClockReader = bool (*)(std::uint64_t*) noexcept;
    struct Snapshot {
        std::uint64_t calls=0, samples=0, ticks=0, maxTicks=0, failures=0;
        std::uint64_t units=0, maxUnits=0;
        bool saturated=false;
    };
    struct Cell {
        std::atomic<std::uint64_t> calls{0}, samples{0}, ticks{0}, maxTicks{0}, failures{0};
        std::atomic<std::uint64_t> units{0}, maxUnits{0};
        std::atomic<bool> saturated{false};
    };
    struct alignas(64) Slot { std::array<Cell,kMetrics> cells{}; };
    class Collector;
    // One Writer per producer thread per collector. Never share it across threads.
    struct Writer {
        Collector* owner=nullptr;
        Slot* slot=nullptr;
        std::array<std::uint64_t,kMetrics> sequence{};
    };
    struct Ticket { Cell* cell=nullptr; std::uint64_t start=0; bool valid=false; };

    class Collector {
        std::array<Slot,kThreadSlots> slots_{};
        std::atomic<std::uint64_t> next_{0};
        std::atomic<ClockReader> clock_{nullptr};
        std::atomic<std::uint64_t> frequency_{0};
        static constexpr auto kMax = (std::numeric_limits<std::uint64_t>::max)();
        Slot* Resolve(Writer& w) noexcept {
            if(w.owner) return w.owner==this ? w.slot : nullptr;
            w.owner=this;
            const auto index=next_.fetch_add(1,std::memory_order_relaxed);
            if(index<kThreadSlots) w.slot=&slots_[static_cast<unsigned>(index)];
            return w.slot;
        }
        // These atomics have one writer. Snapshot loads may race safely and are
        // intentionally a fuzzy cut; values within one row are not transactional.
        static void Add(std::atomic<std::uint64_t>& target, std::uint64_t n, Cell& c) noexcept {
            const auto old=target.load(std::memory_order_relaxed);
            if(n>kMax-old) { target.store(kMax,std::memory_order_relaxed); c.saturated.store(true,std::memory_order_relaxed); }
            else target.store(old+n,std::memory_order_relaxed);
        }
        static void Max(std::atomic<std::uint64_t>& target,std::uint64_t n) noexcept {
            if(n>target.load(std::memory_order_relaxed)) target.store(n,std::memory_order_relaxed);
        }
        static void Sum(std::uint64_t& target,std::uint64_t n,bool& saturated) noexcept {
            if(n>kMax-target) { target=kMax; saturated=true; } else target+=n;
        }
    public:
        // Initialize once before admitting hooks. Clock publication synchronizes
        // frequency, and remains immutable for this process lifetime.
        void Initialize(ClockReader clock,std::uint64_t frequency) noexcept {
            if(!clock || !frequency) return;
            frequency_.store(frequency,std::memory_order_relaxed);
            clock_.store(clock,std::memory_order_release);
        }
        Ticket Begin(Writer& w,Metric metric,unsigned period) noexcept {
            const auto clock=clock_.load(std::memory_order_acquire);
            const auto index=static_cast<unsigned>(metric);
            if(!clock || index>=kMetrics || !period) return {};
            auto* slot=Resolve(w); if(!slot) return {};
            auto& c=slot->cells[index];
            Add(c.calls,1,c);
            if(w.sequence[index]++ % period != 0) return {};
            Ticket t; t.cell=&c; t.valid=clock(&t.start); return t;
        }
        void End(Ticket& t) noexcept {
            if(!t.cell) return;
            auto& c=*t.cell; t.cell=nullptr;
            std::uint64_t end=0;
            const auto clock=clock_.load(std::memory_order_acquire);
            if(!t.valid || !clock || !clock(&end) || end<t.start) { Add(c.failures,1,c); return; }
            const auto elapsed=end-t.start;
            Add(c.samples,1,c); Add(c.ticks,elapsed,c); Max(c.maxTicks,elapsed);
        }
        void Count(Writer& w,Metric metric,std::uint64_t units) noexcept {
            if(!clock_.load(std::memory_order_acquire)) return;
            const auto index=static_cast<unsigned>(metric); if(index>=kMetrics) return;
            auto* slot=Resolve(w); if(!slot) return;
            auto& c=slot->cells[index]; Add(c.calls,1,c); Add(c.units,units,c); Max(c.maxUnits,units);
        }
        Snapshot Read(Metric metric) const noexcept {
            Snapshot s;
            const auto index=static_cast<unsigned>(metric); if(index>=kMetrics) return s;
            const auto assigned=next_.load(std::memory_order_relaxed);
            const auto count=assigned<kThreadSlots ? static_cast<unsigned>(assigned) : kThreadSlots;
            for(unsigned i=0;i<count;++i) {
                const auto& c=slots_[i].cells[index];
                Sum(s.calls,c.calls.load(std::memory_order_relaxed),s.saturated);
                Sum(s.samples,c.samples.load(std::memory_order_relaxed),s.saturated);
                Sum(s.ticks,c.ticks.load(std::memory_order_relaxed),s.saturated);
                Sum(s.failures,c.failures.load(std::memory_order_relaxed),s.saturated);
                Sum(s.units,c.units.load(std::memory_order_relaxed),s.saturated);
                const auto mt=c.maxTicks.load(std::memory_order_relaxed), mu=c.maxUnits.load(std::memory_order_relaxed);
                if(mt>s.maxTicks) s.maxTicks=mt;
                if(mu>s.maxUnits) s.maxUnits=mu;
                s.saturated=s.saturated || c.saturated.load(std::memory_order_relaxed);
            }
            return s;
        }
        std::uint64_t AssignedThreads() const noexcept { return next_.load(std::memory_order_relaxed); }
        std::uint64_t RejectedThreads() const noexcept { auto n=AssignedThreads(); return n>kThreadSlots ? n-kThreadSlots : 0; }
        std::uint64_t Frequency() const noexcept { return frequency_.load(std::memory_order_relaxed); }
        bool ReadClock(std::uint64_t& ticks) const noexcept { auto c=clock_.load(std::memory_order_acquire); return c && c(&ticks); }
    };
    inline Collector& Global() noexcept { static Collector c; return c; }
    inline Writer& Local() noexcept { static thread_local Writer w; return w; }
    class Scope {
        Ticket ticket_;
    public:
        explicit Scope(Metric m,unsigned period=1) noexcept : ticket_(Global().Begin(Local(),m,period)) {}
        ~Scope() noexcept { Global().End(ticket_); }
        Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
    };
    inline void Count(Metric m,std::uint64_t units) noexcept { Global().Count(Local(),m,units); }
    template<class Logger> void Emit(std::uint64_t sequence,Logger&& log) {
        auto& c=Global(); std::uint64_t tick=0; const bool valid=c.ReadClock(tick);
        char line[384]{};
        std::snprintf(line,sizeof(line),"MM-BG-1 seq=%llu lifetime=1 fuzzy=1 inclusive=1 periodic-sample=64 frequency=%llu tick=%llu clock-ok=%u threads=%llu rejected-threads=%llu",
            static_cast<unsigned long long>(sequence),static_cast<unsigned long long>(c.Frequency()),
            static_cast<unsigned long long>(tick),valid?1u:0u,static_cast<unsigned long long>(c.AssignedThreads()),static_cast<unsigned long long>(c.RejectedThreads()));
        log(line);
        for(unsigned i=0;i<kMetrics;++i) {
            const auto s=c.Read(static_cast<Metric>(i));
            if(!s.calls) continue;
            const unsigned period=(i==static_cast<unsigned>(Metric::MarkerHook) || i==static_cast<unsigned>(Metric::MarkerDispatchHook) ||
                (i>=static_cast<unsigned>(Metric::CollectorBeginMarker) && i<=static_cast<unsigned>(Metric::CollectorEndOther))) ? 64u : 1u;
            // Two bounded rows avoid the loader's historical per-line truncation.
            std::snprintf(line,sizeof(line),"MM-BG-1 seq=%llu metric=%s calls=%llu samples=%llu sampled-ticks=%llu sample-max=%llu period=%u",
                static_cast<unsigned long long>(sequence),kNames[i],static_cast<unsigned long long>(s.calls),
                static_cast<unsigned long long>(s.samples),static_cast<unsigned long long>(s.ticks),static_cast<unsigned long long>(s.maxTicks),period);
            log(line);
            std::snprintf(line,sizeof(line),"MM-BG-1 seq=%llu metric=%s clock-fail=%llu units=%llu max-units=%llu saturated=%u",
                static_cast<unsigned long long>(sequence),kNames[i],static_cast<unsigned long long>(s.failures),
                static_cast<unsigned long long>(s.units),static_cast<unsigned long long>(s.maxUnits),s.saturated?1u:0u);
            log(line);
        }
    }
}

#define MM_BG_JOIN_IMPL(a,b) a##b
#define MM_BG_JOIN(a,b) MM_BG_JOIN_IMPL(a,b)
#if defined(ESM_BACKGROUND_DIAGNOSTICS)
#define MM_BG_SCOPE(metric,period) MinimapBackgroundDiagnostic::Scope MM_BG_JOIN(bgScope_,__LINE__)(MinimapBackgroundDiagnostic::Metric::metric,period)
#define MM_BG_COLLECTOR_SCOPE(metric) MinimapBackgroundDiagnostic::Scope MM_BG_JOIN(bgScope_,__LINE__)(metric,64)
#define MM_BG_COUNT(metric,units) MinimapBackgroundDiagnostic::Count(MinimapBackgroundDiagnostic::Metric::metric,units)
#else
#define MM_BG_SCOPE(metric,period) ((void)0)
#define MM_BG_COLLECTOR_SCOPE(metric) ((void)0)
#define MM_BG_COUNT(metric,units) ((void)0)
#endif
