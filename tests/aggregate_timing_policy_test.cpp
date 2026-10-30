// Proves that the aggregate timing policy gates real DLL initialization,
// representative DLL and public-compat callbacks, and the update/report path.
#include "../src/pch.h"
#include "../src/aggregate_timing.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifndef ESM_AGGREGATE_TIMING_DIAGNOSTICS
#error The focused test project must select an aggregate timing mode.
#endif

std::uint64_t g_aggregateTimingPolicyCollectorAccessorCalls = 0;
ULONGLONG g_aggregateTimingPolicyFakeTick = 1000;

ULONGLONG WINAPI AggregateTimingPolicyTestGetTickCount64() noexcept
{
    return g_aggregateTimingPolicyFakeTick;
}

namespace MinimapAggregateTimingPolicyTest
{
    using ::MinimapAggregateTiming::Collector;
    using ::MinimapAggregateTiming::FormatSnapshot;
    using ::MinimapAggregateTiming::kMaxFormattedLine;
    using ::MinimapAggregateTiming::MenuState;
    using ::MinimapAggregateTiming::MenuWrapperPartition;
    using ::MinimapAggregateTiming::Metric;
    using ::MinimapAggregateTiming::OwnershipResult;
    using ::MinimapAggregateTiming::OwnershipStage;
    using ::MinimapAggregateTiming::ReportLossReason;
    using ::MinimapAggregateTiming::ScopedTimer;
    using ::MinimapAggregateTiming::Snapshot;
    using ::MinimapAggregateTiming::StateContext;
    using ::MinimapAggregateTiming::Visibility;

    Collector& DiagnosticCollector() noexcept
    {
        ++::g_aggregateTimingPolicyCollectorAccessorCalls;
        return ::MinimapAggregateTiming::DiagnosticCollector();
    }
}

// Redirect only the included production TU's collector lookups to the test
// counter. The original header and later test inspection remain untouched.
#define MinimapAggregateTiming MinimapAggregateTimingPolicyTest
#define GetTickCount64() AggregateTimingPolicyTestGetTickCount64()
#include "../src/dllmain.cpp"
#undef GetTickCount64
#undef MinimapAggregateTiming

namespace
{
    int failures = 0;

    void Check(bool condition, const char* label)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
            return;
        }
        std::cout << "PASS: " << label << '\n';
    }

    bool HasAggregateLog(const std::vector<std::string>& lines)
    {
        return std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.find("aggregate timing diagnostic") != std::string::npos ||
                line.find("MM-PERF-88 seq=") == 0;
        });
    }
}

int main()
{
    constexpr bool aggregateEnabled = ESM_AGGREGATE_TIMING_DIAGNOSTICS != 0;
    std::vector<std::string> logs;
    ModContext context{};
    context.game.isClient = false;
    context.shroudtopia.config_file =
        R"(E:\Scratch\minimap017-aggregate-policy-fixture-config-does-not-exist.json)";
    context.config.GetString = [](const char*, const char*, std::string fallback) {
        return fallback;
    };
    context.Log = [&logs](const char* line) {
        logs.emplace_back(line == nullptr ? "<null>" : line);
    };

    MinimapMod mod;
    mod.Load(&context);

    MinimapAggregateTiming::Snapshot beforeConfig{};
    const bool beforeDrain = MinimapAggregateTiming::DiagnosticCollector().Drain(beforeConfig);
    const auto beforeConfigAccessorCalls = g_aggregateTimingPolicyCollectorAccessorCalls;
    RefreshMinimapConfig(&context);
    const auto configAccessorDelta =
        g_aggregateTimingPolicyCollectorAccessorCalls - beforeConfigAccessorCalls;
    MinimapAggregateTiming::Snapshot afterConfig{};
    const bool afterDrain = MinimapAggregateTiming::DiagnosticCollector().Drain(afterConfig);
    const auto configMetric = static_cast<std::size_t>(MinimapAggregateTiming::Metric::ConfigPoll);

    const auto beforePublicCompatAccessorCalls = g_aggregateTimingPolicyCollectorAccessorCalls;
    PublicCreativeCompat::ForgetIdentity();
    const auto publicCompatAccessorDelta =
        g_aggregateTimingPolicyCollectorAccessorCalls - beforePublicCompatAccessorCalls;

    // Move the report cadence with a test clock instead of waiting 15 seconds.
    g_aggregateTimingPolicyFakeTick = 1000;
    mod.Update(&context);
    g_aggregateTimingPolicyFakeTick = 16000;
    mod.Update(&context);
    const auto productionCollectorAccessorCalls =
        g_aggregateTimingPolicyCollectorAccessorCalls;
    const bool aggregateLog = HasAggregateLog(logs);

    MinimapAggregateTiming::Snapshot finalSnapshot{};
    const bool finalDrain = MinimapAggregateTiming::DiagnosticCollector().Drain(finalSnapshot);

    if (aggregateEnabled)
    {
        Check(beforeDrain && beforeConfig.initialized && afterDrain && afterConfig.initialized &&
            finalDrain && finalSnapshot.initialized,
            "enabled control initializes and retains the collector across the same server lifecycle");
        Check(configAccessorDelta > 0 &&
            afterConfig.metrics[configMetric].entries == beforeConfig.metrics[configMetric].entries + 1,
            "enabled control records the ConfigPoll timer on a real production callback");
        Check(publicCompatAccessorDelta > 0,
            "enabled control records the public compatibility cache callback");
        Check(productionCollectorAccessorCalls > 0 && aggregateLog,
            "enabled control accesses the collector and emits its scheduled aggregate report");
    }
    else
    {
        Check(!beforeDrain && !beforeConfig.initialized && !afterDrain && !afterConfig.initialized &&
            !finalDrain && !finalSnapshot.initialized,
            "disabled mode leaves the collector uninitialized through Load, callbacks, and Update");
        Check(configAccessorDelta == 0 && publicCompatAccessorDelta == 0 &&
            productionCollectorAccessorCalls == 0,
            "disabled mode performs no collector access on representative callbacks or report path");
        Check(!aggregateLog, "disabled mode emits no aggregate startup or report log");
    }

    return failures == 0 ? 0 : 1;
}

