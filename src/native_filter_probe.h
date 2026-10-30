#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace NativeFilterProbe
{
    constexpr std::size_t kMaxFilterEntries = 40;
    constexpr std::size_t kMaxPrimaryRows = 2048;
    constexpr std::size_t kMaxCustomRows = 2048;
    constexpr std::size_t kMaxSecondaryRows = 1024;
    constexpr std::size_t kMaxDeathRows = 1024;
    constexpr std::size_t kMaxCapturedRows = kMaxPrimaryRows + kMaxSecondaryRows;
    constexpr std::uint32_t kSpecialRawId = 0xAFCE0C5Fu;

    enum class ArrayKind : std::size_t
    {
        Primary = 0,
        Custom = 1,
        Secondary = 2,
        Death = 3
    };

    enum class RowSource : std::uint8_t
    {
        Primary,
        Secondary
    };

    enum class RowClass : std::uint8_t
    {
        OrdinaryRawId,
        SpecialUnresolved
    };

    enum class ContextState : std::uint8_t
    {
        Unknown
    };

    enum class ValidationStatus : std::uint8_t
    {
        StableTable,
        InvalidRead,
        NullRoot,
        RootMismatch,
        SessionChanged,
        Unstable,
        MalformedTable,
        MalformedHeader,
        TooManyRows,
        RowMetadataMismatch,
        MalformedRows
    };

    struct RootChain
    {
        bool readable = false;
        std::array<std::uintptr_t, 4> links{};
    };

    struct ArrayHeader
    {
        std::uintptr_t pointer = 0;
        std::uint64_t count = 0;
    };

    struct FilterEntryBytes
    {
        std::array<std::uint8_t, 8> bytes{};
    };

    static_assert(sizeof(FilterEntryBytes) == 8, "filter entry must preserve all eight wire bytes");

    struct ReadSnapshot
    {
        RootChain root{};

        bool revisionsReadable = false;
        std::uint32_t revision36E9F8 = 0;
        std::uint32_t revision36E9FC = 0;

        bool arrayHeadersReadable = false;
        std::array<ArrayHeader, 4> arrays{};

        // Set only after the count read and the bounded payload capture have
        // completed without a safe-read fault. A zero count is still a valid
        // readable snapshot and must not stand in for an unreadable count.
        bool filterCountReadable = false;
        std::uint8_t filterCount = 0;
        bool filterEntriesReadable = false;
        std::array<FilterEntryBytes, kMaxFilterEntries> filterTable{};
    };

    struct RowMetadata
    {
        RowSource source = RowSource::Primary;
        std::uint32_t rawId = 0;
        std::uint8_t selector = 0;
        std::uint32_t secondaryKey = 0;
    };

    // This is a private owned copy. Its rows must come from the already bounded
    // primary/secondary buffers; the parser performs no native reads.
    struct OwnedReadSet
    {
        std::uint64_t sequence = 0;
        std::uint64_t tick = 0;
        std::uint64_t gapTicks = 0;
        std::uint64_t sessionGenerationBefore = 0;
        std::uint64_t sessionGenerationAfter = 0;

        bool queryStateReadable = false;
        std::uintptr_t queryState = 0;
        bool rowMetadataReadable = false;

        std::array<ReadSnapshot, 2> snapshots{};
        std::array<RowMetadata, kMaxCapturedRows> rows{};
        std::size_t rowCount = 0;
    };

    struct FilterEntrySummary
    {
        std::uint32_t id = 0;
        bool enabled = false;
        std::uint16_t primaryMatches = 0;
        std::uint16_t secondaryMatches = 0;
    };

    // Deliberately contains no native addresses and no visibility decision.
    struct SampleSummary
    {
        std::uint64_t sequence = 0;
        std::uint64_t tick = 0;
        std::uint64_t gapTicks = 0;
        std::uint64_t sessionGeneration = 0;
        ContextState context = ContextState::Unknown;

        std::uint8_t filterEntryCount = 0;
        std::array<FilterEntrySummary, kMaxFilterEntries> filters{};

        std::uint16_t unmatchedPrimaryOrdinary = 0;
        std::uint16_t unmatchedSecondaryOrdinary = 0;
        std::uint16_t unresolvedSpecialPrimary = 0;
        std::uint16_t unresolvedSpecialSecondary = 0;
    };

    inline RowClass ClassifyRow(const RowMetadata& row) noexcept
    {
        if (row.selector == 1 || row.rawId == kSpecialRawId)
            return RowClass::SpecialUnresolved;

        return RowClass::OrdinaryRawId;
    }

    inline std::uint32_t DecodeRawId(const FilterEntryBytes& entry) noexcept
    {
        const auto& bytes = entry.bytes;
        return static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8u) |
            (static_cast<std::uint32_t>(bytes[2]) << 16u) |
            (static_cast<std::uint32_t>(bytes[3]) << 24u);
    }

    inline ValidationStatus ValidateAndSummarize(
        const OwnedReadSet& sample,
        SampleSummary& output)
    {
        if (!sample.queryStateReadable || !sample.rowMetadataReadable)
            return ValidationStatus::InvalidRead;

        const ReadSnapshot& before = sample.snapshots[0];
        const ReadSnapshot& after = sample.snapshots[1];
        const ReadSnapshot* snapshots[] = { &before, &after };

        for (const ReadSnapshot* snapshot : snapshots)
        {
            if (!snapshot->root.readable ||
                !snapshot->revisionsReadable ||
                !snapshot->arrayHeadersReadable ||
                !snapshot->filterCountReadable ||
                !snapshot->filterEntriesReadable)
            {
                return ValidationStatus::InvalidRead;
            }
        }

        if (sample.sessionGenerationBefore != sample.sessionGenerationAfter)
            return ValidationStatus::SessionChanged;

        if (sample.queryState == 0)
            return ValidationStatus::NullRoot;

        for (const ReadSnapshot* snapshot : snapshots)
        {
            for (const std::uintptr_t link : snapshot->root.links)
            {
                if (link == 0)
                    return ValidationStatus::NullRoot;
            }
            if (snapshot->root.links[3] != sample.queryState)
                return ValidationStatus::RootMismatch;
        }

        if (before.root.links != after.root.links)
            return ValidationStatus::Unstable;

        if (before.revision36E9F8 != after.revision36E9F8 ||
            before.revision36E9FC != after.revision36E9FC)
        {
            return ValidationStatus::Unstable;
        }

        for (std::size_t index = 0; index < before.arrays.size(); ++index)
        {
            if (before.arrays[index].pointer != after.arrays[index].pointer ||
                before.arrays[index].count != after.arrays[index].count)
            {
                return ValidationStatus::Unstable;
            }
        }

        if (before.filterCount > kMaxFilterEntries ||
            after.filterCount > kMaxFilterEntries)
        {
            return ValidationStatus::MalformedTable;
        }

        if (before.filterCount != after.filterCount)
            return ValidationStatus::Unstable;

        for (std::size_t index = 0; index < before.filterCount; ++index)
        {
            if (before.filterTable[index].bytes != after.filterTable[index].bytes)
                return ValidationStatus::Unstable;
        }

        std::array<std::uint32_t, kMaxFilterEntries> ids{};
        SampleSummary candidate{};
        candidate.sequence = sample.sequence;
        candidate.tick = sample.tick;
        candidate.gapTicks = sample.gapTicks;
        candidate.sessionGeneration = sample.sessionGenerationBefore;
        candidate.context = ContextState::Unknown;
        candidate.filterEntryCount = before.filterCount;

        for (std::size_t index = 0; index < before.filterCount; ++index)
        {
            const FilterEntryBytes& entry = before.filterTable[index];
            const std::uint8_t enabled = entry.bytes[5];
            if (enabled > 1)
                return ValidationStatus::MalformedTable;

            const std::uint32_t id = DecodeRawId(entry);
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (ids[previous] == id)
                    return ValidationStatus::MalformedTable;
            }

            ids[index] = id;
            candidate.filters[index].id = id;
            candidate.filters[index].enabled = enabled == 1;
        }

        if (sample.rowCount > kMaxCapturedRows)
            return ValidationStatus::TooManyRows;

        std::size_t primaryRows = 0;
        std::size_t secondaryRows = 0;
        for (std::size_t rowIndex = 0; rowIndex < sample.rowCount; ++rowIndex)
        {
            const RowMetadata& row = sample.rows[rowIndex];
            switch (row.source)
            {
            case RowSource::Primary:
                if (++primaryRows > kMaxPrimaryRows)
                    return ValidationStatus::TooManyRows;
                break;
            case RowSource::Secondary:
                if (++secondaryRows > kMaxSecondaryRows)
                    return ValidationStatus::TooManyRows;
                break;
            default:
                return ValidationStatus::MalformedRows;
            }
        }

        const ArrayHeader& primaryHeader = before.arrays[static_cast<std::size_t>(ArrayKind::Primary)];
        const ArrayHeader& secondaryHeader = before.arrays[static_cast<std::size_t>(ArrayKind::Secondary)];
        const std::uint64_t rowLimits[] = {
            kMaxPrimaryRows, kMaxCustomRows, kMaxSecondaryRows, kMaxDeathRows
        };
        for (std::size_t index = 0; index < before.arrays.size(); ++index)
        {
            const ArrayHeader& header = before.arrays[index];
            if (header.count > rowLimits[index])
                return ValidationStatus::TooManyRows;
            if (header.count != 0 && header.pointer == 0)
                return ValidationStatus::MalformedHeader;
        }

        if (primaryHeader.count != primaryRows || secondaryHeader.count != secondaryRows)
            return ValidationStatus::RowMetadataMismatch;

        for (std::size_t rowIndex = 0; rowIndex < sample.rowCount; ++rowIndex)
        {
            const RowMetadata& row = sample.rows[rowIndex];
            if (ClassifyRow(row) == RowClass::SpecialUnresolved)
            {
                if (row.source == RowSource::Primary)
                    ++candidate.unresolvedSpecialPrimary;
                else
                    ++candidate.unresolvedSpecialSecondary;
                continue;
            }

            std::size_t match = before.filterCount;
            for (std::size_t index = 0; index < before.filterCount; ++index)
            {
                if (candidate.filters[index].id == row.rawId)
                {
                    match = index;
                    break;
                }
            }

            if (match == before.filterCount)
            {
                if (row.source == RowSource::Primary)
                    ++candidate.unmatchedPrimaryOrdinary;
                else
                    ++candidate.unmatchedSecondaryOrdinary;
                continue;
            }

            if (row.source == RowSource::Primary)
                ++candidate.filters[match].primaryMatches;
            else
                ++candidate.filters[match].secondaryMatches;
        }

        output = candidate;
        return ValidationStatus::StableTable;
    }
}

