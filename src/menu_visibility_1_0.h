#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace MinimapMenu10
{
    constexpr std::uintptr_t RendererRva = 0xc4e0c0;
    constexpr std::uintptr_t UiContextRva = 0x281b498;
    constexpr std::uintptr_t HolderOffset = 0xd0;
    constexpr std::uintptr_t PlayStateOffset = 0x38;
    constexpr std::uintptr_t ScreenOffset = 0x1cd8;
    constexpr std::uint64_t MaxAgeMs = 500;
    constexpr std::uint64_t MaxTick = UINT64_MAX >> 8;

    // Check after exact-profile admission and before patching; the UI context is stack-local.
    inline bool MatchGuards(const std::uint8_t* image, std::size_t imageSize)
    {
        using namespace std::literals;
        struct Guard { std::size_t rva; std::string_view bytes; };
        constexpr Guard guards[] = {
            {0xc4e0c0, "\x4c\x8b\xdc\x55\x49\x8d\xab\xe8\xfd\xff\xff\x48\x81\xec\x10\x03\x00\x00"sv},
            {0xc4e0d2, "\x48\x8b\x05\xbf\xd3\xbc\x01\x48\x8b\x88\xd0\x00\x00\x00\x48\x8b\x01\x48\x85\xc0\x0f\x84\x94\x0e\x00\x00\x4d\x89\x73\xc8\x4c\x8b\x70\x38\x4c\x89\xb5\x30\x02\x00\x00\x4d\x85\xf6\x0f\x84\x74\x0e\x00\x00\x4d\x89\x63\xd8"sv},
            {0xc4e108, "\x4d\x8d\xa6\xd8\x1c\x00\x00\x41\x0f\xb6\x04\x24\x4c\x89\x65\x80\x3c\x33\x0f\x84\x50\x0e\x00\x00\x84\xc0\x0f\x84\x48\x0e\x00\x00"sv},
            {0xbc0446, "\x48\x89\x05\x4b\xb0\xc5\x01"sv},
            {0xbc0969, "\x4c\x89\x25\x28\xab\xc5\x01"sv},
            {0xbc078b, "\x41\x0f\xb6\x47\x0c\xff\xc8\x83\xf8\x06\x0f\x87\xa4\x01\x00\x00\x48\x8d\x15\x5e\xf8\x43\xff\x48\x98\x8b\x8c\x82\xd4\x09\xbc\x00\x48\x03\xca\xff\xe1"sv},
            {0xbc09d4, "\xb0\x07\xbc\x00\xba\x07\xbc\x00\xc4\x07\xbc\x00\xce\x07\xbc\x00\x3b\x08\xbc\x00\xa8\x08\xbc\x00\x3a\x09\xbc\x00"sv},
            {0xbc093a, "\xe8\x81\xd7\x08\x00\xe8\x3c\x78\x0b\x00"sv},
            {0xd6ade0, "\x48\x89\x0d\xb1\x06\xab\x01\xc3"sv}
        };
        if (!image) return false;
        for (const auto& guard : guards)
            if (guard.rva > imageSize || guard.bytes.size() > imageSize - guard.rva ||
                std::memcmp(image + guard.rva, guard.bytes.data(), guard.bytes.size()) != 0) return false;
        return true;
    }

    class Snapshot
    {
        std::atomic<std::uint64_t> packed{0};
    public:
        void Clear() { packed.store(0); }
        void Capture(std::uint8_t screen, std::uint64_t now, bool readSucceeded)
        {
            if (!readSucceeded || screen == 0 || screen > 53 || now > MaxTick) { Clear(); return; }
            packed.store((now << 8) | screen);
        }
        bool AllowsGameplay(std::uint64_t now) const
        {
            const auto value = packed.load();
            const auto captured = value >> 8;
            return (value & 0xff) == 1 && now >= captured && now - captured <= MaxAgeMs;
        }
    };
}
