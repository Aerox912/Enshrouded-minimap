#define wmain PublicCreativeCompatibilityFixtureWMain
#include "public_creative_compat_test.cpp"
#undef wmain

#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{
    struct ReportChunk
    {
        std::uint64_t sequence = 0;
        std::size_t index = 0;
        std::size_t total = 0;
        std::size_t reportBytes = 0;
        std::string payload;
    };

    int FormatLoggerWithoutArguments(char* output, std::size_t capacity,
        const char* format, ...) noexcept
    {
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(output, capacity, format, arguments);
        va_end(arguments);
        return written;
    }

    bool ParseUnsignedField(const std::string& text, std::size_t& cursor,
        char terminator, std::uint64_t& value)
    {
        const auto start = cursor;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
            ++cursor;
        if (cursor == start || cursor >= text.size() || text[cursor] != terminator)
            return false;
        const auto parsed = std::from_chars(text.data() + start, text.data() + cursor, value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + cursor)
            return false;
        ++cursor;
        return true;
    }

    bool ParseReportChunk(const std::string& loggerLine, ReportChunk& chunk)
    {
        constexpr char marker[] = "MM-PERF-88 seq=";
        const auto markerAt = loggerLine.find(marker);
        if (markerAt == std::string::npos) return false;
        std::size_t cursor = markerAt + sizeof(marker) - 1;
        std::uint64_t sequence = 0, index = 0, total = 0, reportBytes = 0;
        if (!ParseUnsignedField(loggerLine, cursor, ' ', sequence) ||
            loggerLine.compare(cursor, 6, "chunk=") != 0)
            return false;
        cursor += 6;
        if (!ParseUnsignedField(loggerLine, cursor, '/', index) ||
            !ParseUnsignedField(loggerLine, cursor, ' ', total) ||
            loggerLine.compare(cursor, 13, "report-bytes=") != 0)
            return false;
        cursor += 13;
        if (!ParseUnsignedField(loggerLine, cursor, '|', reportBytes))
            return false;

        chunk.sequence = sequence;
        chunk.index = static_cast<std::size_t>(index);
        chunk.total = static_cast<std::size_t>(total);
        chunk.reportBytes = static_cast<std::size_t>(reportBytes);
        chunk.payload.assign(loggerLine.data() + cursor, loggerLine.size() - cursor);
        return chunk.sequence != 0 && chunk.index != 0 && chunk.total != 0;
    }

    bool Reassemble(std::vector<ReportChunk> chunks, std::string& report)
    {
        if (chunks.empty()) return false;
        std::sort(chunks.begin(), chunks.end(), [](const auto& left, const auto& right) {
            return left.index < right.index;
        });
        const auto sequence = chunks.front().sequence;
        const auto total = chunks.front().total;
        const auto reportBytes = chunks.front().reportBytes;
        if (chunks.size() != total) return false;

        report.clear();
        for (std::size_t i = 0; i < chunks.size(); ++i)
        {
            const auto& chunk = chunks[i];
            if (chunk.sequence != sequence || chunk.total != total ||
                chunk.reportBytes != reportBytes || chunk.index != i + 1)
                return false;
            report.append(chunk.payload);
        }
        return report.size() == reportBytes;
    }

    void AggregateTimingReportChunkCase()
    {
        using namespace MinimapAggregateTiming;
        auto& collector = DiagnosticCollector();
        const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        bool seededEveryMetric = true;
        for (std::size_t i = 0; i < static_cast<std::size_t>(Metric::Count); ++i)
            seededEveryMetric = collector.Record(static_cast<Metric>(i), maximum, maximum) &&
                seededEveryMetric;

        std::vector<std::string> loggerLines;
        bool allCallsWithin400Bytes = true;
        bool allCallsFormatSafe = true;
        bool allCallsAscii = true;
        bool loggerTruncated = false;
        const std::string modulePrefix = "(" + g_metaData.name + ") ";
        ModContext context{};
        context.Log = [&](const char* message)
        {
            if (message == nullptr)
            {
                allCallsFormatSafe = false;
                return;
            }
            const std::string call(message);
            if (call.size() > 400) allCallsWithin400Bytes = false;
            for (const unsigned char byte : call)
                if (byte > 0x7f) allCallsAscii = false;
            if (call.find('%') != std::string::npos)
            {
                // The existing Shroudtopia callback treats the prefixed message
                // as a printf format. Never execute a percent-bearing test line.
                allCallsFormatSafe = false;
                return;
            }

            char loggerBuffer[512]{};
            const std::string format = modulePrefix + call;
            const int required = FormatLoggerWithoutArguments(
                loggerBuffer, sizeof(loggerBuffer), format.c_str());
            if (required < 0 || static_cast<std::size_t>(required) >= sizeof(loggerBuffer))
                loggerTruncated = true;
            loggerLines.emplace_back(loggerBuffer);
        };

        PublicCreativeTest::useMockTickCount = true;
        PublicCreativeTest::mockTickCount = 1000;
        EmitAggregateTimingReport(&context); // establish the existing 15-second cadence
        PublicCreativeTest::mockTickCount += 15000;
        EmitAggregateTimingReport(&context);
        PublicCreativeTest::useMockTickCount = false;

        PublicCreativeTest::Check(seededEveryMetric, "dense formatter fixture records every metric");
        PublicCreativeTest::Check(loggerLines.size() > 1,
            "dense aggregate report is emitted as multiple bounded logger calls");
        PublicCreativeTest::Check(allCallsWithin400Bytes && allCallsAscii && !loggerTruncated,
            "each ASCII chunk plus the real module prefix fits the 512-byte logger buffer");
        PublicCreativeTest::Check(allCallsFormatSafe,
            "logger-bound chunks contain no percent format directives");

        std::vector<ReportChunk> chunks;
        bool metadataValid = !loggerLines.empty();
        for (const auto& line : loggerLines)
        {
            ReportChunk chunk{};
            if (!ParseReportChunk(line, chunk))
                metadataValid = false;
            else
                chunks.push_back(std::move(chunk));
        }
        std::string report;
        const bool completeReport = metadataValid && Reassemble(chunks, report);
        PublicCreativeTest::Check(completeReport,
            "sequence, chunk index/total and report byte count reconstruct the full report");
        if (completeReport)
        {
            bool everyMetricPresent = true;
            for (std::size_t i = 0; i < static_cast<std::size_t>(Metric::Count); ++i)
                everyMetricPresent = report.find(MetricName(static_cast<Metric>(i))) !=
                    std::string::npos && everyMetricPresent;
            PublicCreativeTest::Check(everyMetricPresent &&
                report.find("Shroudweave-Minimap timing-schema=aggregate-v1 seq=") == 0,
                "dense reconstructed report retains its schema and every metric record");
            PublicCreativeTest::Check(report.find('%') == std::string::npos,
                "reconstructed report is safe for the existing format-string logger");

            if (chunks.size() > 1)
            {
                auto incomplete = chunks;
                incomplete.erase(incomplete.begin() + 1);
                std::string discarded;
                PublicCreativeTest::Check(!Reassemble(std::move(incomplete), discarded),
                    "missing chunk is detected from sequence and chunk metadata");
            }
            else
            {
                PublicCreativeTest::Check(false, "multi-chunk metadata permits missing-chunk detection");
            }
        }
        else
        {
            PublicCreativeTest::Check(false, "dense report bytes reassemble without truncation or gaps");
            PublicCreativeTest::Check(false, "missing chunk is detected from sequence and chunk metadata");
            PublicCreativeTest::Check(false, "reconstructed report has no percent format directives");
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    const int existingCompatibilityResult = PublicCreativeCompatibilityFixtureWMain(argc, argv);
    if (existingCompatibilityResult != 0) return existingCompatibilityResult;
    AggregateTimingReportChunkCase();
    return PublicCreativeTest::failed == 0 ? 0 : 1;
}
