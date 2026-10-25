// Current Build26082781 only. Static input SHA256 d64966e2...9817882.
// Win64 RCX = dispatch table, RDX = native owner stored at table+0,
// R8 = VkDevice stored at table+8, R9 = opaque initialization options.
// Keep pointer-sized arguments opaque; the caller consumes the exact AL byte.
using VulkanStartupInitFn = std::uint8_t(__fastcall*)(void*, void*, void*, void*);
struct VulkanStartupObserver
{
    Mem::Detour* hook = nullptr; // Never destroyed once reachable; normal startup only.
    std::atomic<VulkanStartupInitFn> original{nullptr};
    uintptr_t callsite = 0;
    std::uint64_t preimage = 0;
    std::atomic<bool> capture{false};
    std::atomic<std::uint64_t> epoch{0};
    bool finalized = false; // Protected by g_vulkanStartupMutex.
};
VulkanStartupObserver g_vulkanStartup;
std::mutex g_vulkanStartupMutex;

std::uint8_t __fastcall CaptureVulkanStartup(void*, void*, void*, void*);

bool IsVulkanStartupOwned(const VulkanStartupObserver& observer)
{
    auto* hook = observer.hook;
    if (!hook || !hook->shellcode || !hook->shellcode->data || !observer.original.load(std::memory_order_acquire)) return false;
    std::uint8_t relay[14] = {0xff,0x25,0,0,0,0}, actual[14]{};
    const auto handler = reinterpret_cast<uintptr_t>(&CaptureVulkanStartup);
    std::memcpy(relay+6, &handler, sizeof(handler));
    return hook->shellcode->data->size == sizeof(relay) &&
        IsCallsiteCalling(observer.callsite, hook->shellcode->data->address) &&
        SafeRead(hook->shellcode->data->address, actual, sizeof(actual)) &&
        std::memcmp(relay, actual, sizeof(relay)) == 0;
}

bool MatchVulkanStartupCallsite()
{
    if (!IsCurrentClientBuild()) return false;
    const std::uint8_t prefix[] = {0x0f,0xb6,0x83,0x00,0x02,0x00,0x00,0x4c,0x8d,0x4d,0x10,0x4c,0x8b,0x87,0x88,0x07,0x00,0x00,0x48,0x8b,0x97,0x80,0x07,0x00,0x00,0x48,0x8b,0x8f,0x70,0x07,0x00,0x00,0x48,0x89,0x45,0x18,0x48,0x89,0x5d,0x10};
    const std::uint8_t suffix[] = {0x84,0xc0,0x74,0x76,0x48,0x8b,0xb5,0x78,0x0a,0x00,0x00,0x4c,0x8d,0x45,0x10,0x48,0x8b,0xce,0x0f,0xb6,0xd0,0xe8,0xae,0xa1,0xfe,0xff};
    const std::uint8_t entry[] = {0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x58,0x48,0x8b,0x41,0x78,0x48,0x8d,0x1d,0xc1,0xe5,0xdb,0x00,0x48,0x89,0x11,0x48,0x8b,0xf9,0x4c,0x89,0x41,0x08,0x33,0xf6,0x48,0x8b,0xd3,0x89,0x75,0x54,0x49,0x8b,0xc8,0x40,0x88,0x75,0x50,0x4d,0x8b,0xf9,0x4d,0x8b,0xf0,0xff,0xd0,0x48,0x89,0x45,0x60,0x44,0x8b,0xee,0x40,0x88,0x75,0x48,0x44,0x8b,0xe6};
    if (!BytesMatchRva(0xFBF54B, prefix, sizeof(prefix)) ||
        !BytesMatchRva(0xFBF578, suffix, sizeof(suffix)) ||
        !BytesMatchRva(0xFE0BE0, entry, sizeof(entry))) return false;
    if (g_vulkanStartup.hook && g_vulkanStartup.hook->active)
        return g_vulkanStartup.finalized && IsVulkanStartupOwned(g_vulkanStartup);
    return IsCallsiteCalling(g_exeBase+MinimapClient10::VulkanStartupCallsiteRva(g_clientProfile),
        g_exeBase+MinimapClient10::VulkanStartupInitializerRva(g_clientProfile));
}

bool PrepareVulkanStartupCallsite()
{
    std::lock_guard<std::mutex> lock(g_vulkanStartupMutex);
    if (!MatchVulkanStartupCallsite()) return false;
    if (g_vulkanStartup.hook) return true;
    const auto callsiteRva = MinimapClient10::VulkanStartupCallsiteRva(g_clientProfile);
    const auto targetRva = MinimapClient10::VulkanStartupInitializerRva(g_clientProfile);
    const auto callsite = g_exeBase+callsiteRva;
    std::uint64_t preimage = 0;
    // CALL begins at byte 3 of an aligned word and ends at byte 7.
    if ((callsite & 7) != 3 || !SafeRead(callsite-3, &preimage, sizeof(preimage))) return false;
    auto* hook = InstallVerifiedCallHook(callsiteRva, targetRva, reinterpret_cast<uintptr_t>(&CaptureVulkanStartup));
    if (!hook) return false;
    g_vulkanStartup.callsite = callsite;
    g_vulkanStartup.preimage = preimage;
    g_vulkanStartup.original.store(reinterpret_cast<VulkanStartupInitFn>(g_exeBase+targetRva), std::memory_order_release);
    g_vulkanStartup.hook = hook;
    return true;
}

template<typename BeforeCompareExchange>
bool PublishVulkanStartupCallsite(VulkanStartupObserver& observer, BeforeCompareExchange beforeCompareExchange,
    bool (*finalize)(uintptr_t, bool, DWORD) = FinalizeMenuCallsitePublication)
{
    auto* hook = observer.hook;
    if (!hook || hook->active || !observer.original.load(std::memory_order_acquire) ||
        !hook->shellcode || !hook->shellcode->data || !hook->shellcode->data->address ||
        hook->shellcode->data->size != 14 || !hook->patch || hook->patch->patched ||
        hook->patch->originalBytes || !hook->patch->data || hook->patch->data->size != 5 ||
        hook->patch->data->address != observer.callsite || (observer.callsite & 7) != 3 || !finalize) return false;
    std::uint8_t expected[5]{}, expectedPatch[5]{};
    if (!MakeDirectCallOpcode(observer.callsite, reinterpret_cast<uintptr_t>(observer.original.load()), expected) ||
        std::memcmp(reinterpret_cast<const std::uint8_t*>(&observer.preimage)+3, expected, 5) ||
        !MakeDirectCallOpcode(observer.callsite, hook->shellcode->data->address, expectedPatch) ||
        std::memcmp(hook->patch->data->opcode, expectedPatch, sizeof(expectedPatch))) return false;
    std::uint8_t relay[14] = {0xff,0x25,0,0,0,0}, actual[14]{};
    const auto handler = reinterpret_cast<uintptr_t>(&CaptureVulkanStartup);
    std::memcpy(relay+6, &handler, sizeof(handler));
    if (std::memcmp(relay, hook->shellcode->data->opcode, sizeof(relay)) ||
        !IsCallsiteCalling(observer.callsite, reinterpret_cast<uintptr_t>(observer.original.load())) ||
        !hook->shellcode->inject() || !SafeRead(hook->shellcode->data->address, actual, sizeof(actual)) ||
        std::memcmp(actual, relay, sizeof(relay)) ||
        !FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(hook->shellcode->data->address), sizeof(relay))) return false;
    auto* saved = new std::uint8_t[5];
    std::memcpy(saved, expected, 5);
    auto replacement = observer.preimage;
    std::memcpy(reinterpret_cast<std::uint8_t*>(&replacement)+3, hook->patch->data->opcode, 5);
    beforeCompareExchange();
    DWORD protection = 0;
    const auto aligned = observer.callsite-3;
    if (!VirtualProtect(reinterpret_cast<void*>(aligned), 8, PAGE_EXECUTE_READWRITE, &protection))
    { delete[] saved; return false; }
    const bool replaced = static_cast<std::uint64_t>(InterlockedCompareExchange64(
        reinterpret_cast<volatile LONG64*>(aligned), static_cast<LONG64>(replacement),
        static_cast<LONG64>(observer.preimage))) == observer.preimage;
    if (replaced)
    {
        hook->patch->originalBytes = saved;
        saved = nullptr;
        hook->patch->patched = true;
        hook->active = true; // Reachable even if protection/cache finalization fails.
    }
    observer.finalized = finalize(aligned, replaced, protection) && replaced;
    delete[] saved;
    return observer.finalized;
}

bool ActivateVulkanStartupCallsite()
{
    std::lock_guard<std::mutex> lock(g_vulkanStartupMutex);
    if (!MatchVulkanStartupCallsite()) return false;
    auto* hook = g_vulkanStartup.hook;
    if (!hook) return false;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&CaptureVulkanStartup), &self)) return false;
    // Fully publish forwarding state before the atomic CALL transition. A wrapper
    // finishing during activation waits on this mutex for the finalized outcome.
    g_vulkanStartup.capture.store(true, std::memory_order_release);
    if (!(hook->active ? g_vulkanStartup.finalized : PublishVulkanStartupCallsite(g_vulkanStartup, []{})) ||
        !IsVulkanStartupOwned(g_vulkanStartup))
    {
        g_vulkanStartup.capture.store(false, std::memory_order_release);
        return false;
    }
    // The caller has already established the required core/profile hooks. Commit
    // callback readiness while this barrier still holds: the post-init wrapper
    // must not return to a native first create while its runtime gate is closed.
    g_runtimeCallbacksActive.store(true, std::memory_order_release);
    return true;
}

void DisableVulkanStartupCapture()
{
    // A CPU may have fetched the CALL already. Keep relay, original and module
    // pinned; disabling capture never undoes/frees a reachable callsite.
    g_vulkanStartup.capture.store(false, std::memory_order_release);
    g_vulkanStartup.epoch.fetch_add(1, std::memory_order_acq_rel);
}

std::uint8_t __fastcall CaptureVulkanStartup(void* table, void* nativeOwner, void* device, void* options)
{
    const auto epoch = g_vulkanStartup.epoch.load(std::memory_order_acquire);
    const auto original = g_vulkanStartup.original.load(std::memory_order_acquire);
    // A reachable wrapper always has its trusted original. Never normalize AL.
    const std::uint8_t result = original ? original(table, nativeOwner, device, options) : 0;
    if (!result) return result;
    std::lock_guard<std::mutex> startup(g_vulkanStartupMutex);
    std::lock_guard<std::mutex> callback(g_presentCallbackMutex);
    uintptr_t owner = 0, observedDevice = 0;
    if (!g_client10 || !IsCurrentClientBuild() || !g_vulkanStartup.capture.load(std::memory_order_acquire) ||
        epoch != g_vulkanStartup.epoch.load(std::memory_order_acquire) || !g_vulkanStartup.finalized ||
        !IsVulkanStartupOwned(g_vulkanStartup) || !table || !device ||
        !SafeReadValue(reinterpret_cast<uintptr_t>(table), owner) || owner != reinterpret_cast<uintptr_t>(nativeOwner) ||
        !SafeReadValue(reinterpret_cast<uintptr_t>(table)+8, observedDevice) || observedDevice != reinterpret_cast<uintptr_t>(device)) return result;
    if (TryInstallVulkanTableHooks(reinterpret_cast<uintptr_t>(table), observedDevice))
    {
        g_vulkanDeviceTable.store(reinterpret_cast<uintptr_t>(table));
        g_lastVulkanDevice.store(observedDevice);
    }
    return result;
}
