#pragma once
#include <atomic>
#include <cstdint>

// One atomic snapshot for the loader and render threads. No startup notice.
struct ToggleNoticeState
{
    std::atomic<std::uint64_t> value{0};
    void show(bool enabled, std::uint64_t now) { value.store((now << 2) | 2ULL | (enabled ? 1ULL : 0ULL)); }
    void clear() { value.store(0); }
    bool read(std::uint64_t now, bool gameplay, bool& enabled)
    {
        auto snapshot = value.load();
        if (!snapshot) return false;
        if (!gameplay || now - (snapshot >> 2) >= 1500)
        {
            value.compare_exchange_strong(snapshot, 0);
            return false;
        }
        enabled = (snapshot & 1) != 0;
        return true;
    }
};
