// Standalone offline contract checks for the diagnostic-only native filter parser.
#include "../src/native_filter_probe.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void Check(bool condition, const char* description)
    {
        if (condition)
        {
            std::cout << "PASS: " << description << '\n';
            return;
        }

        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }

    using namespace NativeFilterProbe;

    constexpr std::size_t kBefore = 0;
    constexpr std::size_t kAfter = 1;
    constexpr std::uintptr_t kQueryRoot = 0x4000;

    FilterEntryBytes MakeEntry(std::uint32_t id, std::uint8_t opaque4,
        std::uint8_t enabled5, std::uint8_t opaque6, std::uint8_t opaque7)
    {
        FilterEntryBytes entry{};
        entry.bytes[0] = static_cast<std::uint8_t>(id & 0xffu);
        entry.bytes[1] = static_cast<std::uint8_t>((id >> 8) & 0xffu);
        entry.bytes[2] = static_cast<std::uint8_t>((id >> 16) & 0xffu);
        entry.bytes[3] = static_cast<std::uint8_t>((id >> 24) & 0xffu);
        entry.bytes[4] = opaque4;
        entry.bytes[5] = enabled5;
        entry.bytes[6] = opaque6;
        entry.bytes[7] = opaque7;
        return entry;
    }

    void SetCount(OwnedReadSet& sample, std::uint8_t count)
    {
        sample.snapshots[kBefore].filterCount = count;
        sample.snapshots[kAfter].filterCount = count;
    }

    void SetEntry(OwnedReadSet& sample, std::size_t index,
        const FilterEntryBytes& entry)
    {
        sample.snapshots[kBefore].filterTable[index] = entry;
        sample.snapshots[kAfter].filterTable[index] = entry;
    }

    void SetArrayCount(OwnedReadSet& sample, ArrayKind kind, std::uint64_t count)
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        sample.snapshots[kBefore].arrays[index].count = count;
        sample.snapshots[kAfter].arrays[index].count = count;
    }

    void AddRow(OwnedReadSet& sample, RowMetadata row)
    {
        if (sample.rowCount < sample.rows.size())
            sample.rows[sample.rowCount++] = row;
    }

    OwnedReadSet MakeStableReadSet()
    {
        OwnedReadSet sample{};
        sample.sequence = 17;
        sample.tick = 9001;
        sample.gapTicks = 120;
        sample.sessionGenerationBefore = 8;
        sample.sessionGenerationAfter = 8;
        sample.rowMetadataReadable = true;
        sample.queryStateReadable = true;
        sample.queryState = kQueryRoot;

        for (auto& snapshot : sample.snapshots)
        {
            snapshot.root.readable = true;
            snapshot.revisionsReadable = true;
            snapshot.arrayHeadersReadable = true;
            snapshot.filterCountReadable = true;
            snapshot.filterEntriesReadable = true;
            snapshot.root.links = { 0x1000, 0x2000, 0x3000, kQueryRoot };
            snapshot.revision36E9F8 = 11;
            snapshot.revision36E9FC = 12;
            snapshot.arrays[static_cast<std::size_t>(ArrayKind::Primary)] = { 0x5000, 0 };
            snapshot.arrays[static_cast<std::size_t>(ArrayKind::Custom)] = { 0x6000, 0 };
            snapshot.arrays[static_cast<std::size_t>(ArrayKind::Secondary)] = { 0x7000, 0 };
            snapshot.arrays[static_cast<std::size_t>(ArrayKind::Death)] = { 0x8000, 0 };
        }

        return sample;
    }

    void CheckRejectedWithoutChangingOutput(const OwnedReadSet& sample,
        ValidationStatus expected, const char* name)
    {
        SampleSummary output{};
        output.sequence = 0x1111222233334444ull;
        output.tick = 0x5555666677778888ull;
        output.gapTicks = 91;
        output.sessionGeneration = 29;
        output.filterEntryCount = 7;
        output.filters[0].id = 0xaabbccddu;
        output.filters[0].enabled = true;
        output.filters[0].primaryMatches = 13;
        output.unmatchedPrimaryOrdinary = 19;

        const ValidationStatus actual = ValidateAndSummarize(sample, output);
        Check(actual == expected, name);
        Check(output.sequence == 0x1111222233334444ull &&
            output.tick == 0x5555666677778888ull &&
            output.gapTicks == 91 &&
            output.sessionGeneration == 29 &&
            output.filterEntryCount == 7 &&
            output.filters[0].id == 0xaabbccddu &&
            output.filters[0].enabled &&
            output.filters[0].primaryMatches == 13 &&
            output.unmatchedPrimaryOrdinary == 19,
            "rejected sample leaves the caller's diagnostic output unchanged");
    }

    void TestEntryCountBounds()
    {
        OwnedReadSet empty = MakeStableReadSet();
        SampleSummary emptySummary{};
        Check(ValidateAndSummarize(empty, emptySummary) == ValidationStatus::StableTable,
            "zero-entry table is a stable valid sample");
        Check(emptySummary.filterEntryCount == 0 &&
            emptySummary.context == ContextState::Unknown,
            "zero-entry summary keeps native visibility context unknown");

        OwnedReadSet full = MakeStableReadSet();
        SetCount(full, static_cast<std::uint8_t>(kMaxFilterEntries));
        for (std::size_t index = 0; index < kMaxFilterEntries; ++index)
        {
            SetEntry(full, index, MakeEntry(
                static_cast<std::uint32_t>(1000 + index),
                static_cast<std::uint8_t>(index),
                static_cast<std::uint8_t>(index % 2),
                static_cast<std::uint8_t>(index + 1),
                static_cast<std::uint8_t>(index + 2)));
        }

        SampleSummary fullSummary{};
        Check(ValidateAndSummarize(full, fullSummary) == ValidationStatus::StableTable,
            "forty inline entries are accepted at the exact bound");
        Check(fullSummary.filterEntryCount == 40 &&
            fullSummary.filters[0].id == 1000 &&
            fullSummary.filters[39].id == 1039,
            "bounded output contains all forty IDs in captured order");

        OwnedReadSet overLimit = MakeStableReadSet();
        SetCount(overLimit, 41);
        CheckRejectedWithoutChangingOutput(overLimit, ValidationStatus::MalformedTable,
            "forty-one entries are rejected");
    }

    void TestEnabledByteLayoutAndRowClassification()
    {
        OwnedReadSet sample = MakeStableReadSet();
        SetCount(sample, 2);
        SetEntry(sample, 0, MakeEntry(101, 0, 1, 0x66, 0x77));
        SetEntry(sample, 1, MakeEntry(102, 1, 0, 0x88, 0x99));

        SetArrayCount(sample, ArrayKind::Primary, 5);
        SetArrayCount(sample, ArrayKind::Secondary, 3);
        AddRow(sample, { RowSource::Primary, 101, 0, 0 });
        AddRow(sample, { RowSource::Primary, 102, 0, 0 });
        AddRow(sample, { RowSource::Primary, 101, 1, 0 });
        AddRow(sample, { RowSource::Primary, 0xafce0c5fu, 0, 0 });
        AddRow(sample, { RowSource::Primary, 999, 0, 0 });
        AddRow(sample, { RowSource::Secondary, 101, 2, 0x11223344 });
        AddRow(sample, { RowSource::Secondary, 0xafce0c5fu, 0, 0x55667788 });
        AddRow(sample, { RowSource::Secondary, 777, 0, 0x99aabbcc });

        Check(ClassifyRow({ RowSource::Primary, 101, 0, 0 }) == RowClass::OrdinaryRawId,
            "selector other than one with a non-special raw ID uses the ordinary raw-ID branch");
        Check(ClassifyRow({ RowSource::Primary, 101, 1, 0 }) == RowClass::SpecialUnresolved,
            "selector byte one is unresolved without calling the native resolver");
        Check(ClassifyRow({ RowSource::Primary, 0xafce0c5fu, 0, 0 }) == RowClass::SpecialUnresolved,
            "special raw ID is unresolved without calling the native resolver");

        SampleSummary summary{};
        Check(ValidateAndSummarize(sample, summary) == ValidationStatus::StableTable,
            "stable table and copied row tags parse into an owned diagnostic summary");
        Check(summary.filters[0].enabled && !summary.filters[1].enabled,
            "enabled state reads byte five, while bytes four, six and seven remain opaque");
        Check(summary.filters[0].primaryMatches == 1 &&
            summary.filters[0].secondaryMatches == 1,
            "matched ordinary rows retain their primary and secondary source counts");
        Check(summary.filters[1].primaryMatches == 1 &&
            summary.unmatchedPrimaryOrdinary == 1 &&
            summary.unmatchedSecondaryOrdinary == 1,
            "ordinary rows absent from the table are counted by source layer");
        Check(summary.unresolvedSpecialPrimary == 2 &&
            summary.unresolvedSpecialSecondary == 1 &&
            summary.context == ContextState::Unknown,
            "special rows stay unresolved and selected/contextual visibility remains unknown");
        Check(summary.sequence == 17 && summary.tick == 9001 && summary.gapTicks == 120 &&
            summary.sessionGeneration == 8,
            "summary retains the sample sequence, monotonic tick, gap and session generation");
    }

    void TestMalformedTables()
    {
        OwnedReadSet invalidEnabled = MakeStableReadSet();
        SetCount(invalidEnabled, 1);
        SetEntry(invalidEnabled, 0, MakeEntry(31, 0, 2, 0, 0));
        CheckRejectedWithoutChangingOutput(invalidEnabled, ValidationStatus::MalformedTable,
            "enabled byte outside zero or one rejects the sample");

        OwnedReadSet conflictingDuplicate = MakeStableReadSet();
        SetCount(conflictingDuplicate, 2);
        SetEntry(conflictingDuplicate, 0, MakeEntry(44, 0, 0, 0, 0));
        SetEntry(conflictingDuplicate, 1, MakeEntry(44, 0, 1, 0, 0));
        CheckRejectedWithoutChangingOutput(conflictingDuplicate, ValidationStatus::MalformedTable,
            "duplicate raw ID with conflicting enabled state rejects the sample");
    }

    void TestRootAndSessionGuards()
    {
        OwnedReadSet nullRoot = MakeStableReadSet();
        nullRoot.snapshots[kBefore].root.links[3] = 0;
        CheckRejectedWithoutChangingOutput(nullRoot, ValidationStatus::NullRoot,
            "null link in the checked native root chain rejects the sample");

        OwnedReadSet unreadableRoot = MakeStableReadSet();
        unreadableRoot.snapshots[kAfter].root.readable = false;
        CheckRejectedWithoutChangingOutput(unreadableRoot, ValidationStatus::InvalidRead,
            "unreadable checked native root link rejects the sample");

        OwnedReadSet unreadableQuery = MakeStableReadSet();
        unreadableQuery.queryStateReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableQuery, ValidationStatus::InvalidRead,
            "unreadable iterator query state rejects the sample");

        OwnedReadSet rootMismatch = MakeStableReadSet();
        rootMismatch.queryState = 0x9000;
        CheckRejectedWithoutChangingOutput(rootMismatch, ValidationStatus::RootMismatch,
            "iterator query state must equal the resolved global filter root");

        OwnedReadSet changedRoot = MakeStableReadSet();
        changedRoot.snapshots[kAfter].root.links[0] ^= 0x10;
        CheckRejectedWithoutChangingOutput(changedRoot, ValidationStatus::Unstable,
            "changed native root chain across the sample rejects the sample");

        OwnedReadSet changedSession = MakeStableReadSet();
        changedSession.sessionGenerationAfter++;
        CheckRejectedWithoutChangingOutput(changedSession, ValidationStatus::SessionChanged,
            "session generation change across the sample rejects the sample");
    }

    void TestUnreadableMetadata()
    {
        OwnedReadSet unreadableRevision = MakeStableReadSet();
        unreadableRevision.snapshots[kBefore].revisionsReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableRevision, ValidationStatus::InvalidRead,
            "unreadable revision metadata rejects the sample");

        OwnedReadSet unreadableHeaders = MakeStableReadSet();
        unreadableHeaders.snapshots[kAfter].arrayHeadersReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableHeaders, ValidationStatus::InvalidRead,
            "unreadable array header metadata rejects the sample");

        OwnedReadSet unreadableCount = MakeStableReadSet();
        unreadableCount.snapshots[kBefore].filterCountReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableCount, ValidationStatus::InvalidRead,
            "unreadable count cannot be mistaken for a valid empty table");

        OwnedReadSet unreadableEntries = MakeStableReadSet();
        SetCount(unreadableEntries, 1);
        unreadableEntries.snapshots[kAfter].filterEntriesReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableEntries, ValidationStatus::InvalidRead,
            "unreadable inline table bytes reject when the captured count is nonzero");

        OwnedReadSet unreadableRows = MakeStableReadSet();
        unreadableRows.rowMetadataReadable = false;
        CheckRejectedWithoutChangingOutput(unreadableRows, ValidationStatus::InvalidRead,
            "unreadable copied row metadata rejects the sample");
    }

    void TestRevisionHeaderAndTableStability()
    {
        OwnedReadSet changedRevision = MakeStableReadSet();
        changedRevision.snapshots[kAfter].revision36E9FC++;
        CheckRejectedWithoutChangingOutput(changedRevision, ValidationStatus::Unstable,
            "changed second native revision rejects the sample");

        OwnedReadSet changedOtherRevision = MakeStableReadSet();
        changedOtherRevision.snapshots[kAfter].revision36E9F8++;
        CheckRejectedWithoutChangingOutput(changedOtherRevision, ValidationStatus::Unstable,
            "changed first native revision rejects the sample");

        for (std::size_t index = 0; index < 4; ++index)
        {
            OwnedReadSet changedPointer = MakeStableReadSet();
            changedPointer.snapshots[kAfter].arrays[index].pointer ^= 0x10;
            CheckRejectedWithoutChangingOutput(changedPointer, ValidationStatus::Unstable,
                "changed primary, custom, secondary or death array pointer rejects the sample");

            OwnedReadSet changedCount = MakeStableReadSet();
            changedCount.snapshots[kAfter].arrays[index].count++;
            CheckRejectedWithoutChangingOutput(changedCount, ValidationStatus::Unstable,
                "changed primary, custom, secondary or death array count rejects the sample");
        }

        OwnedReadSet changedTable = MakeStableReadSet();
        SetCount(changedTable, 1);
        SetEntry(changedTable, 0, MakeEntry(55, 0x12, 1, 0x34, 0x56));
        changedTable.snapshots[kAfter].filterTable[0].bytes[7] ^= 0x01;
        CheckRejectedWithoutChangingOutput(changedTable, ValidationStatus::Unstable,
            "any changed byte in the inline eight-byte table rejects the sample");

        OwnedReadSet changedCount = MakeStableReadSet();
        changedCount.snapshots[kAfter].filterCount = 1;
        CheckRejectedWithoutChangingOutput(changedCount, ValidationStatus::Unstable,
            "changed inline table count across the sample rejects the sample");
    }

    void TestCopiedRowBounds()
    {
        OwnedReadSet tooManyPrimary = MakeStableReadSet();
        SetArrayCount(tooManyPrimary, ArrayKind::Primary,
            static_cast<std::uint64_t>(kMaxPrimaryRows + 1));
        CheckRejectedWithoutChangingOutput(tooManyPrimary, ValidationStatus::TooManyRows,
            "primary header above the existing 2048-row copy cap is rejected");

        OwnedReadSet tooManySecondary = MakeStableReadSet();
        SetArrayCount(tooManySecondary, ArrayKind::Secondary,
            static_cast<std::uint64_t>(kMaxSecondaryRows + 1));
        CheckRejectedWithoutChangingOutput(tooManySecondary, ValidationStatus::TooManyRows,
            "secondary header above the existing 1024-row copy cap is rejected");

        OwnedReadSet tooManyCustom = MakeStableReadSet();
        SetArrayCount(tooManyCustom, ArrayKind::Custom,
            static_cast<std::uint64_t>(kMaxCustomRows + 1));
        CheckRejectedWithoutChangingOutput(tooManyCustom, ValidationStatus::TooManyRows,
            "custom header above the existing 2048-row copy cap is rejected");

        OwnedReadSet tooManyDeath = MakeStableReadSet();
        SetArrayCount(tooManyDeath, ArrayKind::Death,
            static_cast<std::uint64_t>(kMaxDeathRows + 1));
        CheckRejectedWithoutChangingOutput(tooManyDeath, ValidationStatus::TooManyRows,
            "death header above the existing 1024-row copy cap is rejected");

        OwnedReadSet highBits = MakeStableReadSet();
        SetArrayCount(highBits, ArrayKind::Primary, 0x100000000ull);
        CheckRejectedWithoutChangingOutput(highBits, ValidationStatus::TooManyRows,
            "native header count preserves and rejects values above 32 bits");

        OwnedReadSet highBitsChanged = MakeStableReadSet();
        highBitsChanged.snapshots[kAfter].arrays[
            static_cast<std::size_t>(ArrayKind::Primary)].count = 0x100000000ull;
        CheckRejectedWithoutChangingOutput(highBitsChanged, ValidationStatus::Unstable,
            "header stability compares the full 64-bit count");

        for (std::size_t index = 0; index < 4; ++index)
        {
            OwnedReadSet nullArray = MakeStableReadSet();
            nullArray.snapshots[kBefore].arrays[index].count = 1;
            nullArray.snapshots[kAfter].arrays[index].count = 1;
            nullArray.snapshots[kBefore].arrays[index].pointer = 0;
            nullArray.snapshots[kAfter].arrays[index].pointer = 0;
            CheckRejectedWithoutChangingOutput(nullArray, ValidationStatus::MalformedHeader,
                "nonempty primary, custom, secondary or death header cannot have a null pointer");
        }

        OwnedReadSet countOverflow = MakeStableReadSet();
        countOverflow.rowCount = kMaxCapturedRows + 1;
        CheckRejectedWithoutChangingOutput(countOverflow, ValidationStatus::TooManyRows,
            "owned row metadata count cannot exceed its fixed buffer capacity");

        OwnedReadSet rowMismatch = MakeStableReadSet();
        SetArrayCount(rowMismatch, ArrayKind::Primary, 1);
        CheckRejectedWithoutChangingOutput(rowMismatch, ValidationStatus::RowMetadataMismatch,
            "copied primary metadata must account for every row in the stable header");
    }
}

int main()
{
    TestEntryCountBounds();
    TestEnabledByteLayoutAndRowClassification();
    TestMalformedTables();
    TestRootAndSessionGuards();
    TestUnreadableMetadata();
    TestRevisionHeaderAndTableStability();
    TestCopiedRowBounds();

    return failures == 0 ? 0 : 1;
}

