#pragma once

#include "client_1_0_file_signatures.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace MinimapClient10 { namespace FileLayout {

constexpr std::uint64_t MaximumFileSize = 128ull * 1024ull * 1024ull;
constexpr std::uint32_t MaximumSectionCount = 96;
constexpr std::uint32_t MaximumFileAlignment = 0x10000;
constexpr std::uint32_t MinimumFileAlignment = 0x200;
constexpr std::uint32_t MinimumPageAlignment = 0x1000;
constexpr std::uint32_t SectionReadable = 0x40000000;
constexpr std::uint32_t SectionExecutable = 0x20000000;
constexpr std::uint32_t PeMachineAmd64 = 0x8664;
constexpr std::uint16_t Pe32PlusMagic = 0x020b;
constexpr std::uint32_t StartupCallRva = 0x00fbf573;
constexpr std::uint32_t StartupTargetRva = 0x00fe0be0;

struct Section {
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualEnd = 0;
    std::uint32_t rawOffset = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
};

struct PeLayout {
    std::uint32_t sizeOfImage = 0;
    std::uint32_t fileAlignment = 0;
    std::uint32_t sectionAlignment = 0;
    std::vector<Section> sections;
};

inline bool IsPowerOfTwo(std::uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

inline bool IsRangeInFile(const std::vector<std::uint8_t>& file, std::uint64_t offset, std::uint64_t length)
{
    return offset <= file.size() && length <= static_cast<std::uint64_t>(file.size()) - offset;
}

inline bool ReadU16(const std::vector<std::uint8_t>& file, std::uint64_t offset, std::uint16_t& value)
{
    if (!IsRangeInFile(file, offset, sizeof(value))) return false;
    value = static_cast<std::uint16_t>(file[static_cast<std::size_t>(offset)]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(file[static_cast<std::size_t>(offset + 1)]) << 8);
    return true;
}

inline bool ReadU32(const std::vector<std::uint8_t>& file, std::uint64_t offset, std::uint32_t& value)
{
    if (!IsRangeInFile(file, offset, sizeof(value))) return false;
    value = static_cast<std::uint32_t>(file[static_cast<std::size_t>(offset)]) |
        (static_cast<std::uint32_t>(file[static_cast<std::size_t>(offset + 1)]) << 8) |
        (static_cast<std::uint32_t>(file[static_cast<std::size_t>(offset + 2)]) << 16) |
        (static_cast<std::uint32_t>(file[static_cast<std::size_t>(offset + 3)]) << 24);
    return true;
}

inline bool ReadFileBounded(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const std::streamoff end = input.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) > MaximumFileSize) return false;
    const auto size = static_cast<std::size_t>(end);
    bytes.resize(size);
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return false;
    return input.gcount() == static_cast<std::streamsize>(bytes.size()) && !input.bad();
}

inline bool ParsePe(const std::vector<std::uint8_t>& file, PeLayout& layout)
{
    if (file.size() < 0x40 || file.size() > MaximumFileSize ||
        file[0] != 'M' || file[1] != 'Z') return false;

    std::uint32_t peOffset = 0;
    if (!ReadU32(file, 0x3c, peOffset)) return false;
    const std::uint64_t coffOffset = static_cast<std::uint64_t>(peOffset) + 4;
    if (!IsRangeInFile(file, peOffset, 4 + 20) ||
        file[peOffset] != 'P' || file[peOffset + 1] != 'E' ||
        file[peOffset + 2] != 0 || file[peOffset + 3] != 0) return false;

    std::uint16_t machine = 0;
    std::uint16_t sectionCount = 0;
    std::uint16_t optionalSize = 0;
    if (!ReadU16(file, coffOffset, machine) ||
        !ReadU16(file, coffOffset + 2, sectionCount) ||
        !ReadU16(file, coffOffset + 16, optionalSize) ||
        machine != PeMachineAmd64 || sectionCount == 0 || sectionCount > MaximumSectionCount ||
        optionalSize < 112) return false;

    const std::uint64_t optionalOffset = coffOffset + 20;
    const std::uint64_t sectionTableOffset = optionalOffset + optionalSize;
    const std::uint64_t tableSize = static_cast<std::uint64_t>(sectionCount) * 40;
    if (!IsRangeInFile(file, optionalOffset, optionalSize) ||
        !IsRangeInFile(file, sectionTableOffset, tableSize)) return false;
    const std::uint32_t directoryCapacity =
        (static_cast<std::uint32_t>(optionalSize) - 112u) / 8u;

    std::uint16_t magic = 0;
    std::uint32_t sectionAlignment = 0;
    std::uint32_t fileAlignment = 0;
    std::uint32_t sizeOfImage = 0;
    std::uint32_t sizeOfHeaders = 0;
    std::uint32_t directoryCount = 0;
    if (!ReadU16(file, optionalOffset, magic) ||
        !ReadU32(file, optionalOffset + 32, sectionAlignment) ||
        !ReadU32(file, optionalOffset + 36, fileAlignment) ||
        !ReadU32(file, optionalOffset + 56, sizeOfImage) ||
        !ReadU32(file, optionalOffset + 60, sizeOfHeaders) ||
        !ReadU32(file, optionalOffset + 108, directoryCount) ||
        magic != Pe32PlusMagic || !IsPowerOfTwo(sectionAlignment) ||
        !IsPowerOfTwo(fileAlignment) || sectionAlignment < fileAlignment ||
        fileAlignment < MinimumFileAlignment || fileAlignment > MaximumFileAlignment ||
        (sectionAlignment < MinimumPageAlignment && sectionAlignment != fileAlignment) ||
        sizeOfImage == 0 || sizeOfHeaders == 0 || sizeOfHeaders > file.size() ||
        sizeOfHeaders % fileAlignment != 0 || sizeOfImage % sectionAlignment != 0 ||
        sectionTableOffset + tableSize > sizeOfHeaders ||
        directoryCount > directoryCapacity) return false;

    const std::uint64_t firstSectionAddress =
        ((static_cast<std::uint64_t>(sizeOfHeaders) + sectionAlignment - 1) / sectionAlignment) * sectionAlignment;
    if (firstSectionAddress > (std::numeric_limits<std::uint32_t>::max)()) return false;

    PeLayout candidate;
    candidate.sizeOfImage = sizeOfImage;
    candidate.fileAlignment = fileAlignment;
    candidate.sectionAlignment = sectionAlignment;
    candidate.sections.reserve(sectionCount);

    std::uint64_t maximumVirtualEnd = firstSectionAddress;
    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        const std::uint64_t sectionHeader = sectionTableOffset + static_cast<std::uint64_t>(index) * 40;
        std::uint32_t virtualSize = 0;
        std::uint32_t virtualAddress = 0;
        std::uint32_t rawSize = 0;
        std::uint32_t rawOffset = 0;
        std::uint32_t characteristics = 0;
        if (!ReadU32(file, sectionHeader + 8, virtualSize) ||
            !ReadU32(file, sectionHeader + 12, virtualAddress) ||
            !ReadU32(file, sectionHeader + 16, rawSize) ||
            !ReadU32(file, sectionHeader + 20, rawOffset) ||
            !ReadU32(file, sectionHeader + 36, characteristics)) return false;

        const std::uint32_t mappedSize = (virtualSize > rawSize) ? virtualSize : rawSize;
        const std::uint64_t virtualEnd = static_cast<std::uint64_t>(virtualAddress) + mappedSize;
        if (mappedSize == 0 || virtualAddress < firstSectionAddress ||
            virtualAddress % sectionAlignment != 0 || virtualEnd > sizeOfImage ||
            virtualEnd > (std::numeric_limits<std::uint32_t>::max)()) return false;

        if (rawSize != 0) {
            const std::uint64_t rawEnd = static_cast<std::uint64_t>(rawOffset) + rawSize;
            if (rawOffset < sizeOfHeaders || rawOffset % fileAlignment != 0 ||
                rawSize % fileAlignment != 0 || rawEnd > file.size() ||
                (sectionAlignment < MinimumPageAlignment && rawOffset != virtualAddress)) return false;
        }

        candidate.sections.push_back(Section{
            virtualAddress,
            static_cast<std::uint32_t>(virtualEnd),
            rawOffset,
            rawSize,
            characteristics
        });
        if (virtualEnd > maximumVirtualEnd) maximumVirtualEnd = virtualEnd;
    }

    std::vector<const Section*> virtualOrder;
    std::vector<const Section*> rawOrder;
    virtualOrder.reserve(candidate.sections.size());
    rawOrder.reserve(candidate.sections.size());
    for (const auto& section : candidate.sections) {
        virtualOrder.push_back(&section);
        if (section.rawSize != 0) rawOrder.push_back(&section);
    }
    std::sort(virtualOrder.begin(), virtualOrder.end(), [](const Section* left, const Section* right) {
        return left->virtualAddress < right->virtualAddress;
    });
    std::sort(rawOrder.begin(), rawOrder.end(), [](const Section* left, const Section* right) {
        return left->rawOffset < right->rawOffset;
    });
    for (std::size_t index = 1; index < virtualOrder.size(); ++index) {
        if (virtualOrder[index]->virtualAddress < virtualOrder[index - 1]->virtualEnd) return false;
    }
    for (std::size_t index = 1; index < rawOrder.size(); ++index) {
        const std::uint64_t previousEnd = static_cast<std::uint64_t>(rawOrder[index - 1]->rawOffset) + rawOrder[index - 1]->rawSize;
        if (rawOrder[index]->rawOffset < previousEnd) return false;
    }
    if (maximumVirtualEnd > sizeOfImage) return false;
    layout = std::move(candidate);
    return true;
}

inline bool FindRawBackedRva(
    const std::vector<std::uint8_t>& file,
    const PeLayout& layout,
    std::uint32_t rva,
    std::size_t length,
    bool requireExecutable,
    std::size_t& fileOffset)
{
    if (length == 0 || static_cast<std::uint64_t>(rva) + length > layout.sizeOfImage) return false;
    const Section* match = nullptr;
    for (const auto& section : layout.sections) {
        if (rva < section.virtualAddress || rva >= section.virtualEnd) continue;
        const std::uint64_t delta = static_cast<std::uint64_t>(rva) - section.virtualAddress;
        if (delta > section.rawSize || length > static_cast<std::uint64_t>(section.rawSize) - delta) return false;
        if (match != nullptr) return false;
        match = &section;
    }
    if (match == nullptr || (match->characteristics & SectionReadable) == 0 ||
        (requireExecutable && (match->characteristics & SectionExecutable) == 0)) return false;

    const std::uint64_t delta = static_cast<std::uint64_t>(rva) - match->virtualAddress;
    const std::uint64_t rawFileOffset = static_cast<std::uint64_t>(match->rawOffset) + delta;
    if (!IsRangeInFile(file, rawFileOffset, length) ||
        rawFileOffset > (std::numeric_limits<std::size_t>::max)()) return false;
    fileOffset = static_cast<std::size_t>(rawFileOffset);
    return true;
}

inline bool MatchesCurrentSignatures(const std::vector<std::uint8_t>& file, const PeLayout& layout)
{
    for (const auto& signature : CurrentSignatures) {
        const std::uint64_t signatureEnd = static_cast<std::uint64_t>(signature.byteOffset) + signature.byteCount;
        if (signature.byteCount == 0 || signatureEnd > sizeof(CurrentSignatureBytes)) return false;
        std::size_t fileOffset = 0;
        if (!FindRawBackedRva(file, layout, signature.rva, signature.byteCount, signature.executable, fileOffset)) return false;
        if (std::memcmp(file.data() + fileOffset, CurrentSignatureBytes + signature.byteOffset, signature.byteCount) != 0)
            return false;
    }
    return true;
}

inline bool MatchesDirectStartupCall(const std::vector<std::uint8_t>& file, const PeLayout& layout)
{
    std::size_t fileOffset = 0;
    if (!FindRawBackedRva(file, layout, StartupCallRva, 5, true, fileOffset) || file[fileOffset] != 0xe8)
        return false;

    std::uint32_t encodedDisplacement = 0;
    if (!ReadU32(file, fileOffset + 1, encodedDisplacement)) return false;
    const std::int64_t displacement = (encodedDisplacement & 0x80000000u) != 0
        ? static_cast<std::int64_t>(encodedDisplacement) - 0x100000000ll
        : static_cast<std::int64_t>(encodedDisplacement);
    const std::int64_t target = static_cast<std::int64_t>(StartupCallRva) + 5 + displacement;
    return target >= 0 && target <= (std::numeric_limits<std::uint32_t>::max)() &&
        static_cast<std::uint32_t>(target) == StartupTargetRva;
}

inline bool MatchesCurrentProfileLayout(const std::filesystem::path& path)
{
    std::vector<std::uint8_t> file;
    if (!ReadFileBounded(path, file)) return false;
    PeLayout layout;
    return ParsePe(file, layout) && MatchesCurrentSignatures(file, layout) && MatchesDirectStartupCall(file, layout);
}

} } // namespace MinimapClient10::FileLayout
