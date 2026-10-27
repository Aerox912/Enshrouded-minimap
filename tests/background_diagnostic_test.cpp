#include "background_diagnostic.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <type_traits>
#include <Windows.h>

namespace B = MinimapBackgroundDiagnostic;
static std::atomic<std::uint64_t> clockTick{100};
static std::atomic<bool> clockOk{true};
static bool Clock(std::uint64_t* value) noexcept { *value=clockTick.load(); return clockOk.load(); }
static void Check(bool value, const char* why) { if(!value) { std::cerr << why << '\n'; std::exit(1); } }
// Exercise the real MSVC SEH/unwind restriction, including return from a
// nested handler. The recording ticket must not require C++ unwinding.
static_assert(std::is_trivially_destructible<B::Ticket>::value, "SEH ticket must remain trivial");
static void SehProbe(B::Collector& collector, B::Writer& writer, unsigned path) {
    auto ticket=collector.Begin(writer,B::Metric::MarkerHook,1);
    __try {
        clockTick=120;
        if(path==1) return;
        __try {
            if(path==2) return;
            if(path==3) RaiseException(0xE0424242,0,0,nullptr);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
    } __finally {
        collector.End(ticket);
    }
}
int main() {
    B::Collector seh; seh.Initialize(Clock,10000000); B::Writer sehWriter;
    for(unsigned path=0;path<4;++path) { clockTick=100; SehProbe(seh,sehWriter,path); }
    const auto sehResult=seh.Read(B::Metric::MarkerHook);
    Check(sehResult.calls==4 && sehResult.samples==4 && sehResult.ticks==80 && sehResult.failures==0,
        "SEH normal / early / nested / handled-exception exits recorded exactly once");
    B::Collector c; c.Initialize(Clock, 10000000);
    B::Writer w;
    for(unsigned i=0;i<128;++i) {
        clockTick=100; auto t=c.Begin(w,B::Metric::MarkerHook,64);
        clockTick=130; c.End(t); c.End(t);
    }
    auto s=c.Read(B::Metric::MarkerHook);
    Check(s.calls==128 && s.samples==2 && s.ticks==60 && s.maxTicks==30,"periodic sampling / idempotent stop");
    clockTick=200; auto a=c.Begin(w,B::Metric::WorldMapHook,1);
    clockTick=210; auto b=c.Begin(w,B::Metric::WorldMapHook,1);
    clockTick=220; c.End(b); clockTick=240; c.End(a);
    s=c.Read(B::Metric::WorldMapHook);
    Check(s.calls==2 && s.samples==2 && s.ticks==50 && s.maxTicks==40,"nested inclusive durations");
    clockOk=false; a=c.Begin(w,B::Metric::WorldMapHook,1); clockOk=true; c.End(a);
    clockTick=250; a=c.Begin(w,B::Metric::WorldMapHook,1); clockTick=249; c.End(a);
    s=c.Read(B::Metric::WorldMapHook); Check(s.failures==2 && s.samples==2,"clock failures must not become durations");
    a=c.Begin(w,B::Metric::WorldMapHook,1); clockOk=false; c.End(a); clockOk=true;
    Check(c.Read(B::Metric::WorldMapHook).failures==3,"end clock failure visible");
    c.Count(w,B::Metric::CachePopulation,2048); c.Count(w,B::Metric::CachePopulation,12);
    s=c.Read(B::Metric::CachePopulation); Check(s.calls==2 && s.units==2060 && s.maxUnits==2048,"population observations");
    c.Count(w,B::Metric::CacheMiss,UINT64_MAX); c.Count(w,B::Metric::CacheMiss,1);
    s=c.Read(B::Metric::CacheMiss); Check(s.units==UINT64_MAX && s.saturated,"saturation visible");
    B::Collector concurrent; concurrent.Initialize(Clock,10000000);
    std::atomic<bool> go{false}; std::atomic<unsigned> done{0};
    std::vector<std::thread> threads;
    for(unsigned j=0;j<8;++j) threads.emplace_back([&]{ B::Writer own; while(!go.load()) std::this_thread::yield(); for(unsigned i=0;i<10000;++i) concurrent.Count(own,B::Metric::CacheHit,1); ++done; });
    go=true; std::uint64_t last=0;
    while(done.load()!=8) { auto v=concurrent.Read(B::Metric::CacheHit); Check(v.calls>=last && v.calls<=80000,"concurrent cumulative count"); last=v.calls; }
    for(auto& t:threads) t.join();
    s=concurrent.Read(B::Metric::CacheHit); Check(s.calls==80000 && s.units==80000,"all thread counters retained");
    B::Collector capacity; capacity.Initialize(Clock,10000000);
    B::Writer writers[B::kThreadSlots+1];
    for(auto& own:writers) capacity.Count(own,B::Metric::CacheHit,1);
    capacity.Count(writers[B::kThreadSlots],B::Metric::CacheHit,1);
    Check(capacity.Read(B::Metric::CacheHit).calls==B::kThreadSlots && capacity.RejectedThreads()==1,"fixed capacity and explicit lost thread");
    B::Collector disabled; B::Writer dw; auto off=disabled.Begin(dw,B::Metric::MarkerHook,1); disabled.End(off);
    Check(disabled.Read(B::Metric::MarkerHook).calls==0,"uninitialized clock inert");
    B::Global().Initialize(Clock,10000000);
    B::Count(B::Metric::CacheHit,1);
    unsigned lines=0;
    B::Emit(7,[&](const char* row){ ++lines; Check(std::char_traits<char>::length(row)<300,"loader-safe bounded report rows"); });
    Check(lines==3,"header and two metric report rows");
    std::cout << "PASS background diagnostic: sampling, nesting, clock errors, counters, saturation, concurrent reads, capacity, inert initialization, SEH exits\n";
}
