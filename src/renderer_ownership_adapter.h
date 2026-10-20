#pragma once

#include "menu_visibility_1_0.h"
#include "renderer_ownership_api.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace RendererOwnershipAdapter
{
    using native_ui_host::renderer_ownership::Api;
    using native_ui_host::renderer_ownership::Proof;
    using native_ui_host::renderer_ownership::Status;

    static_assert(sizeof(Proof) == 40);
    static_assert(offsetof(Proof, image_base) == 0);
    static_assert(offsetof(Proof, renderer_rva) == 8);
    static_assert(offsetof(Proof, prefix_size) == 12);
    static_assert(offsetof(Proof, original_prefix) == 16);
    static_assert(offsetof(Proof, reserved) == 34);
    static_assert(sizeof(Api) == 16);
    static_assert(offsetof(Api, size) == 0);
    static_assert(offsetof(Api, abi) == 4);
    static_assert(offsetof(Api, query) == 8);
    static_assert(std::is_standard_layout_v<Proof> && std::is_standard_layout_v<Api>);
    static_assert(static_cast<std::uint32_t>(Status::uninstalled) == 0);
    static_assert(static_cast<std::uint32_t>(Status::verified) == 1);
    static_assert(static_cast<std::uint32_t>(Status::busy) == 2);
    static_assert(static_cast<std::uint32_t>(Status::failed_partial) == 3);
    static_assert(static_cast<std::uint32_t>(Status::tampered) == 4);
    static_assert(static_cast<std::uint32_t>(Status::invalid_request) == 5);

    enum class MenuAdmission : std::uint8_t
    {
        raw,
        owned,
        busy,
        rejected,
    };

    inline bool IsAdmitted(MenuAdmission admission)
    {
        return admission == MenuAdmission::raw || admission == MenuAdmission::owned;
    }

    inline bool IsBusy(MenuAdmission admission)
    {
        return admission == MenuAdmission::busy;
    }

    inline void RecordInitialMenuAdmission(bool& pending, MenuAdmission admission)
    {
        pending = IsBusy(admission);
    }

    inline MenuAdmission EvaluateMenuGuards(
        const std::uint8_t* image, std::size_t imageSize,
        std::uintptr_t expectedBase, const Api* api)
    {
        if (MinimapMenu10::MatchGuards(image, imageSize))
            return MenuAdmission::raw;

        if (!image || !expectedBase || reinterpret_cast<std::uintptr_t>(image) != expectedBase ||
            !api || api->size != static_cast<std::uint32_t>(sizeof(Api)) ||
            api->abi != native_ui_host::renderer_ownership::abi_version || !api->query)
            return MenuAdmission::rejected;

        Proof proof{};
        const Status status = api->query(&proof, static_cast<std::uint32_t>(sizeof(proof)));
        if (status == Status::busy)
            return MenuAdmission::busy;
        if (status != Status::verified)
            return MenuAdmission::rejected;

        if (proof.image_base != expectedBase ||
            proof.renderer_rva != native_ui_host::renderer_ownership::renderer_rva ||
            proof.prefix_size != native_ui_host::renderer_ownership::original_prefix_size)
            return MenuAdmission::rejected;
        for (const auto reserved : proof.reserved)
            if (reserved != 0)
                return MenuAdmission::rejected;

        return MinimapMenu10::MatchGuardsWithRendererPrefix(
            image, imageSize, proof.original_prefix, proof.prefix_size)
            ? MenuAdmission::owned
            : MenuAdmission::rejected;
    }

    template <typename Evaluate, typename Prepare, typename Activate>
    bool ActivateMenuAtBoundary(
        bool& initialPending, std::atomic<bool>& everPublished,
        Evaluate evaluate, Prepare prepare, Activate activate)
    {
        if (everPublished.load(std::memory_order_acquire))
        {
            initialPending = false;
            return activate(true, MenuAdmission::rejected);
        }

        if (!prepare())
        {
            initialPending = false;
            return false;
        }

        const MenuAdmission admission = evaluate();
        if (IsBusy(admission))
        {
            initialPending = true;
            return false;
        }

        initialPending = false;
        if (!IsAdmitted(admission))
            return false;
        return activate(false, admission);
    }

    template <typename Evaluate, typename Prepare, typename Activate>
    bool RetryPendingMenuAtUpdate(
        bool modActive, bool& initialPending, std::atomic<bool>& everPublished,
        Evaluate evaluate, Prepare prepare, Activate activate)
    {
        if (!modActive)
            return false;
        if (!initialPending || everPublished.load(std::memory_order_acquire))
        {
            initialPending = false;
            return false;
        }
        return ActivateMenuAtBoundary(
            initialPending, everPublished, evaluate, prepare, activate);
    }
}
