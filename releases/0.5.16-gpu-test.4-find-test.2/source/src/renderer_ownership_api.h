#pragma once

#include <cstdint>

namespace native_ui_host::renderer_ownership {

inline constexpr std::uint32_t abi_version = 1;
inline constexpr std::uint32_t renderer_rva = 0xc4e0c0;
inline constexpr std::uint32_t original_prefix_size = 18;

enum class Status : std::uint32_t {
    uninstalled = 0,
    verified = 1,
    busy = 2,
    failed_partial = 3,
    tampered = 4,
    invalid_request = 5,
};

struct Proof {
    std::uint64_t image_base;
    std::uint32_t renderer_rva;
    std::uint32_t prefix_size;
    std::uint8_t original_prefix[original_prefix_size];
    std::uint8_t reserved[6];
};

using Query = Status (*)(Proof*, std::uint32_t out_size);

struct Api {
    std::uint32_t size;
    std::uint32_t abi;
    Query query;
};

} // namespace native_ui_host::renderer_ownership

#if defined(_WIN32) && defined(NATIVE_UI_HOST_BUILD)
#define NATIVE_UI_HOST_RENDERER_OWNERSHIP_EXPORT __declspec(dllexport)
#else
#define NATIVE_UI_HOST_RENDERER_OWNERSHIP_EXPORT
#endif

extern "C" NATIVE_UI_HOST_RENDERER_OWNERSHIP_EXPORT
const native_ui_host::renderer_ownership::Api* NativeUiHost_GetRendererOwnershipApi(
    std::uint32_t version) noexcept;

#undef NATIVE_UI_HOST_RENDERER_OWNERSHIP_EXPORT
