// MM-PUBLIC-CREATIVE-MINIMAP-IMPLEMENTATION-20261020-01
// Real production TU, with ONLY OS module metadata and the final native-call
// boundary substituted. Fixture pages are private and NX; the SEH case briefly
// marks one owned module page NOACCESS and restores it before further reads.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
namespace PublicCreativeTest { LPVOID WINAPI Allocate(LPVOID, SIZE_T, DWORD, DWORD); }
namespace PublicCreativeTest { ULONGLONG WINAPI TickCount64(); }
// Keep the real Mem::Detour/Shellcode constructors, but force their FIRST
// underlying allocation to RW/NX. Windows itself is already declared above.
#define VirtualAlloc PublicCreativeTest::Allocate
#include <memory_utils.h>
#undef VirtualAlloc
#include <shroudtopia.h>

namespace PublicCreativeTest
{
    BOOL WINAPI Module(DWORD, LPCWSTR, HMODULE*);
    DWORD WINAPI ModulePath(HMODULE, LPWSTR, DWORD);
    FARPROC WINAPI Procedure(HMODULE, LPCSTR);
    BOOL WINAPI Release(HMODULE);
    SIZE_T WINAPI Query(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
    void Dispatch(void (*)());
}
#define GetModuleHandleExW PublicCreativeTest::Module
#define GetModuleFileNameW PublicCreativeTest::ModulePath
#define GetProcAddress PublicCreativeTest::Procedure
#define FreeLibrary PublicCreativeTest::Release
#define VirtualQuery PublicCreativeTest::Query
#define VirtualAlloc PublicCreativeTest::Allocate
#define GetTickCount64 PublicCreativeTest::TickCount64
// The baseline TU has exactly one original() call, in the real HUD wrapper.
// The argument retains gameBase+RendererRva; Dispatch never calls that address.
#define original() PublicCreativeTest::Dispatch(original)
#include "../src/dllmain.cpp"
#undef original
#undef VirtualAlloc
#undef VirtualQuery
#undef FreeLibrary
#undef GetTickCount64
#undef GetProcAddress
#undef GetModuleFileNameW
#undef GetModuleHandleExW

namespace PublicCreativeTest
{
    constexpr size_t CreativeImageSize = 0x555000;
    constexpr size_t CallbackThunk = 0x4877;
    constexpr size_t CallbackBody = 0x18b350;
    constexpr size_t TemplateRva = 0x3eb7e0;
    constexpr size_t TemplateSize = 0x187;
    constexpr size_t SlotRva = 0x514ba8;
    constexpr std::uint8_t Prefix[18] = {
        0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec,0x10,0x03,0,0};
    std::uint8_t* module = nullptr;
    std::uint8_t* game = nullptr;
    size_t gameSize = 0;
    std::wstring modulePath;
    bool moduleAvailable = true;
    bool privateAvailable = false;
    bool denyBlock = false;
    size_t moduleAcquires = 0, moduleReleases = 0, nativeCalls = 0;
    std::uint64_t mockTickCount = 0;
    bool useMockTickCount = false;
    bool nativeAddressCorrect = false, visibleInsideOriginal = false;
    std::function<void()> duringOriginal;
    size_t cases = 0, failed = 0;
    uintptr_t A() { return reinterpret_cast<uintptr_t>(game) + MinimapMenu10::RendererRva; }
    uintptr_t Arena() { return reinterpret_cast<uintptr_t>(game) + gameSize; }
    uintptr_t T() { return Arena() + 0x1000; }
    uintptr_t B() { return Arena() + 0x2000; }
    uintptr_t O() { return Arena() + 0x3000; }
    uintptr_t H() { return O() + 0x80; }
    uintptr_t Backup() { return Arena() + 0x4000; }
    constexpr uintptr_t PrivateHandle = 0x12345000;

    ULONGLONG WINAPI TickCount64()
    { return useMockTickCount ? static_cast<ULONGLONG>(mockTickCount) : ::GetTickCount64(); }

    LPVOID WINAPI Allocate(LPVOID address, SIZE_T size, DWORD allocation, DWORD)
    { return ::VirtualAlloc(address, size, allocation, PAGE_READWRITE); }

    bool Inside(uintptr_t p, uintptr_t first, size_t size)
    { return p >= first && p - first < size; }

    BOOL WINAPI Module(DWORD flags, LPCWSTR name, HMODULE* result)
    {
        if (!result) return FALSE;
        *result = nullptr;
        if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
            if (!moduleAvailable || !Inside(reinterpret_cast<uintptr_t>(name),
                reinterpret_cast<uintptr_t>(module), CreativeImageSize)) return FALSE;
            *result = reinterpret_cast<HMODULE>(module);
            ++moduleAcquires;
            return TRUE;
        }
        // Basename lookup deliberately cannot discover public Creative. This
        // prevents a same-name proxy or filename shortcut passing this test.
        if (privateAvailable && name && std::wcscmp(name, L"native_ui_host.dll") == 0) {
            *result = reinterpret_cast<HMODULE>(PrivateHandle);
            ++moduleAcquires;
            return TRUE;
        }
        return FALSE;
    }
    DWORD WINAPI ModulePath(HMODULE value, LPWSTR output, DWORD cap)
    {
        if (value != reinterpret_cast<HMODULE>(module) || !output || cap <= modulePath.size()) return 0;
        std::copy(modulePath.begin(), modulePath.end(), output);
        output[modulePath.size()] = 0;
        return static_cast<DWORD>(modulePath.size());
    }
    BOOL WINAPI Release(HMODULE value)
    {
        if (value == reinterpret_cast<HMODULE>(module) || value == reinterpret_cast<HMODULE>(PrivateHandle)) {
            ++moduleReleases; return TRUE;
        }
        return FALSE;
    }
    native_ui_host::renderer_ownership::Status __cdecl PrivateQuery(
        native_ui_host::renderer_ownership::Proof* proof, std::uint32_t size)
    {
        using namespace native_ui_host::renderer_ownership;
        if (!proof || size != sizeof(Proof)) return Status::invalid_request;
        *proof = {};
        proof->image_base = reinterpret_cast<uintptr_t>(game);
        proof->renderer_rva = MinimapMenu10::RendererRva;
        proof->prefix_size = 18;
        std::memcpy(proof->original_prefix, Prefix, 18);
        return Status::verified;
    }
    const native_ui_host::renderer_ownership::Api* __cdecl PrivateGetter(std::uint32_t version) noexcept
    {
        using namespace native_ui_host::renderer_ownership;
        static const Api api{sizeof(Api), abi_version, &PrivateQuery};
        return version == abi_version ? &api : nullptr;
    }
    FARPROC WINAPI Procedure(HMODULE value, LPCSTR name)
    {
        if (value == reinterpret_cast<HMODULE>(PrivateHandle) && name &&
            std::strcmp(name, "NativeUiHost_GetRendererOwnershipApi") == 0)
            return reinterpret_cast<FARPROC>(&PrivateGetter);
        return nullptr; // No public DLL callable can escape the test boundary.
    }
    SIZE_T WINAPI Query(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T cap)
    {
        const SIZE_T read = ::VirtualQuery(address, info, cap);
        if (!read || !info) return read;
        const uintptr_t p = reinterpret_cast<uintptr_t>(address);
        if (Inside(p, reinterpret_cast<uintptr_t>(module), CreativeImageSize)) {
            info->BaseAddress = module; info->AllocationBase = module;
            info->RegionSize = CreativeImageSize; info->Type = MEM_IMAGE;
            info->Protect = PAGE_EXECUTE_READ;
        } else if (Inside(p, reinterpret_cast<uintptr_t>(game), gameSize)) {
            info->BaseAddress = game; info->AllocationBase = game;
            info->RegionSize = gameSize; info->Type = MEM_IMAGE;
            info->Protect = PAGE_EXECUTE_READ;
        } else if (Inside(p, Arena(), 0x10000)) {
            info->BaseAddress = reinterpret_cast<void*>(Arena());
            info->AllocationBase = game; info->RegionSize = 0x10000; info->Type = MEM_PRIVATE;
            info->Protect = (denyBlock && Inside(p, B(), 0x1000)) ? PAGE_NOACCESS : PAGE_EXECUTE_READ;
        }
        // Real memory remains PAGE_READWRITE/NX. Only metadata is substituted.
        return read;
    }
    void Dispatch(void (*native)())
    {
        ++nativeCalls;
        nativeAddressCorrect = reinterpret_cast<uintptr_t>(native) == A();
        visibleInsideOriginal = g_menuVisibilitySnapshot.AllowsGameplay(TickCount64());
        if (duringOriginal) duringOriginal();
    }

    void Check(bool ok, const char* name)
    {
        ++cases;
        if (!ok) ++failed;
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    }
    void Require(bool ok, const char* name)
    { if (!ok) { std::cerr << "FIXTURE ERROR " << name << '\n'; std::exit(2); } }

    std::vector<std::uint8_t> ReadFile(const wchar_t* path)
    {
        std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        Require(in.good(), "read fixture");
        const auto length = in.tellg();
        Require(length > 0 && length <= static_cast<std::streamoff>(40000000), "fixture size");
        std::vector<std::uint8_t> bytes(static_cast<size_t>(length));
        in.seekg(0);
        Require(static_cast<bool>(in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "fixture complete");
        return bytes;
    }
    size_t ImageSize(const std::vector<std::uint8_t>& file)
    {
        Require(file.size() > sizeof(IMAGE_DOS_HEADER), "DOS header");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        Require(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= file.size(), "PE bounds");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
            "PE64 header");
        return nt->OptionalHeader.SizeOfImage;
    }
    void MapData(const std::vector<std::uint8_t>& file, std::uint8_t* output, size_t size)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        Require(nt->OptionalHeader.SizeOfHeaders <= file.size() && nt->OptionalHeader.SizeOfHeaders <= size,
            "header size");
        std::memcpy(output, file.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        Require(reinterpret_cast<const std::uint8_t*>(sections + nt->FileHeader.NumberOfSections) <=
            file.data() + file.size(), "section table bounds");
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& s = sections[i];
            Require(s.VirtualAddress <= size && s.SizeOfRawData <= size - s.VirtualAddress &&
                s.PointerToRawData <= file.size() && s.SizeOfRawData <= file.size() - s.PointerToRawData,
                "section bounds");
            std::memcpy(output + s.VirtualAddress, file.data() + s.PointerToRawData, s.SizeOfRawData);
        }
        // Apply only PE DIR64 relocations to data; never resolve/call imports.
        const auto& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        Require(d.VirtualAddress <= size && d.Size <= size - d.VirtualAddress, "relocation bounds");
        size_t pos = 0;
        const std::uint64_t delta = reinterpret_cast<uintptr_t>(output) - nt->OptionalHeader.ImageBase;
        while (pos < d.Size) {
            const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(output + d.VirtualAddress + pos);
            Require(d.Size - pos >= sizeof(*block) && block->SizeOfBlock >= sizeof(*block) &&
                block->SizeOfBlock <= d.Size - pos, "relocation block");
            const auto* item = reinterpret_cast<const WORD*>(block + 1);
            for (size_t j = 0; j < (block->SizeOfBlock - sizeof(*block)) / 2; ++j) {
                const unsigned type = item[j] >> 12;
                const size_t at = block->VirtualAddress + (item[j] & 0xfff);
                if (!type) continue;
                Require(type == IMAGE_REL_BASED_DIR64 && at <= size - 8, "relocation type");
                std::uint64_t v = 0; std::memcpy(&v, output + at, 8); v += delta;
                std::memcpy(output + at, &v, 8);
            }
            pos += block->SizeOfBlock;
        }
    }
    template<class V> void Put(uintptr_t at, V value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }
    void Rel32(uintptr_t at, std::uint8_t op, uintptr_t to)
    {
        const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(at + 5);
        Require(delta >= INT32_MIN && delta <= INT32_MAX, "fixture rel32 range");
        Put(at, op); Put(at + 1, static_cast<std::int32_t>(delta));
    }
    void Raw()
    {
        std::memcpy(reinterpret_cast<void*>(A()), Prefix, 18);
        Put(reinterpret_cast<uintptr_t>(module) + SlotRva, uintptr_t{0});
    }
    void Known(unsigned kind)
    {
        Raw();
        std::memset(reinterpret_cast<void*>(Arena() + 0x1000), 0, 0x4000);
        const size_t n = kind == 1 ? 11 : 18;
        const size_t length = kind == 1 ? 30 : 32;
        std::memcpy(reinterpret_cast<void*>(B()), module + TemplateRva, TemplateSize);
        Put(B() + 0x177, reinterpret_cast<uintptr_t>(module) + CallbackThunk);
        Put(B() + 0x17f, T());
        std::memcpy(reinterpret_cast<void*>(T()), Prefix, n);
        std::memcpy(reinterpret_cast<void*>(Backup()), Prefix, n);
        std::memset(reinterpret_cast<void*>(A()), 0x90, n);
        if (kind == 1) {
            Rel32(A(), 0xe9, T() + 16);
            Rel32(T() + 11, 0xe9, A() + 11);
            const std::uint8_t jump[6] = {0xff,0x25,0,0,0,0};
            std::memcpy(reinterpret_cast<void*>(T() + 16), jump, 6);
            Put(T() + 22, B());
        } else {
            const std::uint8_t jump[6] = {0xff,0x25,0,0,0,0};
            std::memcpy(reinterpret_cast<void*>(A()), jump, 6); Put(A() + 6, B());
            std::memcpy(reinterpret_cast<void*>(T() + 18), jump, 6); Put(T() + 24, A() + 18);
        }
        Put(H(), A()); Put(H() + 8, B());
        Put(H() + 0x10, Arena() + 0x5000); Put(H() + 0x18, Arena() + 0x5100);
        Put(H() + 0x20, T()); Put(H() + 0x28, static_cast<uintptr_t>(length));
        Put(H() + 0x30, Backup()); Put(H() + 0x38, Backup() + n); Put(H() + 0x40, Backup() + n);
        Put(H() + 0x48, static_cast<uintptr_t>(length));
        Put(H() + 0xa0, std::uint8_t{1}); Put(H() + 0xa4, kind);
        Put(H() + 0xa8, A());
        Put(H() + 0xb0, Arena() + 0x5000); Put(H() + 0xb8, Arena() + 0x5100);
        Put(H() + 0xc0, B()); Put(H() + 0xc8, uintptr_t{TemplateSize});
        Put(H() + 0xd0, reinterpret_cast<uintptr_t>(module) + CallbackThunk);
        Put(reinterpret_cast<uintptr_t>(module) + SlotRva, O());
    }
    Mem::Detour* RetainCall()
    {
        std::uint8_t relay[14] = {0xff,0x25,0,0,0,0};
        Put(reinterpret_cast<uintptr_t>(relay) + 6, reinterpret_cast<uintptr_t>(&CaptureMenuVisibilityCallsiteHook));
        const uintptr_t call = reinterpret_cast<uintptr_t>(game) + RVA_MENU_RENDERER_CALL_1_0;
        auto* hook = new Mem::Detour(call, relay, sizeof(relay), false);
        Require(hook && hook->patch && hook->shellcode && hook->shellcode->data &&
            hook->shellcode->data->address, "private retained relay allocation");
        MEMORY_BASIC_INFORMATION backing{};
        Require(::VirtualQuery(reinterpret_cast<void*>(hook->shellcode->data->address),
            &backing, sizeof(backing)) == sizeof(backing) && backing.Protect == PAGE_READWRITE,
            "actual retained fixture allocation is RW/NX from construction");
        hook->patch->originalBytes = new std::uint8_t[5];
        std::memcpy(hook->patch->originalBytes, reinterpret_cast<void*>(call), 5);
        std::uint8_t patched[5]{};
        Require(MakeDirectCallOpcode(call, hook->shellcode->data->address, patched), "retained CALL range");
        hook->patch->updateOpcode(0, patched, 5);
        // Deliberate private-buffer construction, no hook activation or generated execution.
        std::memcpy(reinterpret_cast<void*>(call), patched, 5);
        std::memcpy(reinterpret_cast<void*>(hook->shellcode->data->address), relay, 14);
        hook->active = true; hook->patch->patched = true; hook->shellcode->injected = true;
        g_menuVisibilityCallHook = hook;
        g_menuVisibilityCallsiteEverActivated.store(true);
        g_menuVisibilityCallsitePublicationFinalized.store(true);
        return hook;
    }
    void DestroyCall(Mem::Detour* hook)
    {
        const auto call = reinterpret_cast<uintptr_t>(game) + RVA_MENU_RENDERER_CALL_1_0;
        std::memcpy(reinterpret_cast<void*>(call), hook->patch->originalBytes, 5);
        void* relay = reinterpret_cast<void*>(hook->shellcode->data->address);
        hook->active = false; hook->patch->patched = false; hook->shellcode->injected = false;
        delete hook->shellcode; delete hook->patch; delete hook;
        VirtualFree(relay, 0, MEM_RELEASE);
        g_menuVisibilityCallHook = nullptr;
        g_menuVisibilityCallsiteEverActivated.store(false);
        g_menuVisibilityCallsitePublicationFinalized.store(false);
    }
    void ResetCapture()
    {
        g_menuVisibilitySnapshot.Clear();
        g_runtimeCallbacksActive.store(true);
        g_menuVisibilityHookActive.store(true);
        g_originalMenuRenderer.store(A());
        nativeCalls = 0; nativeAddressCorrect = false; visibleInsideOriginal = false;
        duringOriginal = {};
        Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{1});
    }
    void CaptureCase(const char* name, bool wantVisible, std::function<void()> action = {})
    {
        ResetCapture(); duringOriginal = std::move(action);
        CaptureMenuVisibilityCallsiteHook();
        Check(nativeCalls == 1 && nativeAddressCorrect, "wrapper forwards native address exactly once");
        Check(!visibleInsideOriginal, "candidate is not published before original returns");
        Check(g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()) == wantVisible, name);
        duringOriginal = {};
    }

    void SeedPriorGameplaySnapshot()
    {
        constexpr std::uint64_t capturedAt = 100;
        useMockTickCount = true;
        mockTickCount = capturedAt;
        g_menuVisibilitySnapshot.Capture(1, capturedAt, true);
        mockTickCount = capturedAt + MinimapMenu10::MaxAgeMs;
    }

    void PriorGameplayOverlapCase(const char* name, bool publicOwner)
    {
        // Deterministic overlap: the original boundary observes the old lease,
        // then the test clock advances until only a premature new publication
        // could keep the shared gameplay getter true.
        SeedPriorGameplaySnapshot();
        g_client10 = true;
        g_runtimeCallbacksActive.store(true);
        g_menuVisibilityHookActive.store(true);
        g_originalMenuRenderer.store(A());
        moduleAvailable = true;
        Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{1});
        if (publicOwner) Known(1); else Raw();
        nativeCalls = 0; nativeAddressCorrect = false; visibleInsideOriginal = false;
        bool priorExpiredInsideOriginal = false;
        duringOriginal = [&] {
            ++mockTickCount;
            priorExpiredInsideOriginal = !g_menuVisibilitySnapshot.AllowsGameplay(mockTickCount);
        };

        CaptureMenuVisibilityCallsiteHook();
        duringOriginal = {};
        Check(nativeCalls == 1 && nativeAddressCorrect && visibleInsideOriginal && priorExpiredInsideOriginal &&
            g_menuVisibilitySnapshot.AllowsGameplay(mockTickCount), name);
        useMockTickCount = false;
    }

    void InvalidPreAdmissionClearsPriorCase()
    {
        SeedPriorGameplaySnapshot();
        g_runtimeCallbacksActive.store(true);
        g_menuVisibilityHookActive.store(true);
        g_originalMenuRenderer.store(A());
        Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{1});
        Known(1);
        moduleAvailable = false;
        nativeCalls = 0; nativeAddressCorrect = false; visibleInsideOriginal = true;
        CaptureMenuVisibilityCallsiteHook();
        moduleAvailable = true;
        Check(nativeCalls == 1 && nativeAddressCorrect && !visibleInsideOriginal &&
            !g_menuVisibilitySnapshot.AllowsGameplay(mockTickCount),
            "invalid pre-admission clears prior gameplay before original while still forwarding once");
        useMockTickCount = false;
    }

    void NonGameplayPreScreenClearsPriorCase()
    {
        SeedPriorGameplaySnapshot();
        g_client10 = true;
        g_runtimeCallbacksActive.store(true);
        g_menuVisibilityHookActive.store(true);
        g_originalMenuRenderer.store(A());
        Raw();
        Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{9});
        nativeCalls = 0; nativeAddressCorrect = false; visibleInsideOriginal = true;
        CaptureMenuVisibilityCallsiteHook();
        Check(nativeCalls == 1 && nativeAddressCorrect && !visibleInsideOriginal &&
            !g_menuVisibilitySnapshot.AllowsGameplay(mockTickCount),
            "known non-gameplay pre-screen clears prior gameplay before original while forwarding once");
        useMockTickCount = false;
    }

    void FailedAfterSampleClearsPriorCase()
    {
        SeedPriorGameplaySnapshot();
        g_client10 = true;
        g_runtimeCallbacksActive.store(true);
        g_menuVisibilityHookActive.store(true);
        g_originalMenuRenderer.store(A());
        Raw();
        Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{1});
        nativeCalls = 0; nativeAddressCorrect = false; visibleInsideOriginal = false;
        duringOriginal = [] { Put(g_exeBase + MinimapMenu10::UiContextRva, uintptr_t{0}); };
        CaptureMenuVisibilityCallsiteHook();
        duringOriginal = {};
        Put(g_exeBase + MinimapMenu10::UiContextRva, Arena() + 0x6000);
        Check(nativeCalls == 1 && nativeAddressCorrect && visibleInsideOriginal &&
            !g_menuVisibilitySnapshot.AllowsGameplay(mockTickCount),
            "failed post-sample clears prior gameplay only after forwarding completes");
        useMockTickCount = false;
    }

    void RealSehCopyCase()
    {
        // This is an owned data fixture page. The test Query reports a readable
        // module page while the real page stays NOACCESS for the Copy call.
        const uintptr_t page = reinterpret_cast<uintptr_t>(module) + 0x550000;
        constexpr SIZE_T pageBytes = 0x1000;
        DWORD oldProtection = 0;
        Require(::VirtualProtect(reinterpret_cast<void*>(page), pageBytes, PAGE_NOACCESS, &oldProtection) != FALSE,
            "protect owned inert module page for real SEH case");

        MEMORY_BASIC_INFORMATION actual{};
        const bool actuallyNoAccess = ::VirtualQuery(reinterpret_cast<const void*>(page), &actual, sizeof(actual)) == sizeof(actual) &&
            actual.State == MEM_COMMIT && (actual.Protect & 0xff) == PAGE_NOACCESS;
        MEMORY_BASIC_INFORMATION reported{};
        const bool testQueryReportsReadable = Query(reinterpret_cast<const void*>(page), &reported, sizeof(reported)) == sizeof(reported) &&
            reported.State == MEM_COMMIT && !(reported.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (reported.Protect & 0xff) == PAGE_EXECUTE_READ;
        std::uint8_t output[16]{};
        const bool copied = PublicCreativeCompat::Copy(page, output, sizeof(output));

        DWORD restoredProtection = 0;
        const bool restored = ::VirtualProtect(reinterpret_cast<void*>(page), pageBytes, oldProtection, &restoredProtection) != FALSE;
        Require(restored, "restore owned inert module page before further fixture reads");
        Check(actuallyNoAccess && testQueryReportsReadable && !copied,
            "real inaccessible read is contained by Copy SEH despite readable test VirtualQuery metadata");
    }
}

int wmain(int argc, wchar_t** argv)
{
    using namespace PublicCreativeTest;
    if (argc != 4) { std::cerr << "usage: test GAME_EXE GOOD_MODULE_DATA WRONG_MODULE_DATA\n"; return 2; }
    Require(MinimapClient10::MatchesExactFile(argv[1]), "pinned offline game fixture identity");
    const auto gameFile = ReadFile(argv[1]); const auto publicFile = ReadFile(argv[2]);
    gameSize = ImageSize(gameFile); Require(ImageSize(publicFile) == CreativeImageSize, "public image size");
    game = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, gameSize + 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    module = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, CreativeImageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    Require(game && module, "private NX image allocations");
    MapData(gameFile, game, gameSize); MapData(publicFile, module, CreativeImageSize);
    g_exeBase = reinterpret_cast<uintptr_t>(game); g_exeImageSize = gameSize; g_client10 = true;
    modulePath = argv[2];
    Raw(); Check(EvaluateCurrentMenuAdmission() == RendererOwnershipAdapter::MenuAdmission::raw, "native standalone admission");
    Known(1); modulePath = argv[3];
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong public whole-file identity rejected");
    modulePath = std::wstring(argv[2]) + L".missing";
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "missing module file rejected");
    modulePath = argv[2];
    // Independent literals above derive from pinned emitter proof -07, not production helpers.
    Known(1); Check(RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "complete public near form admitted");
    Known(2); Check(RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "complete public absolute form admitted");
    Known(1); moduleAvailable = false;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "unavailable reached module rejected"); moduleAvailable = true;
    Known(1); Put(B() + 0x177, B());
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "callback outside reached module rejected");
    Known(1); Put(H() + 0xa0, std::uint8_t{0});
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "disabled hook rejected");
    Known(1); Put(reinterpret_cast<uintptr_t>(module) + SlotRva, uintptr_t{0});
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "missing early-published object rejected");
    Known(1); std::memset(reinterpret_cast<void*>(H()), 0, 0xd8);
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "early publication without complete fields rejected");
    Known(1); Put(H() + 0x38, Backup() + 10);
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "short original-byte backup rejected");
    Known(1); Put(T() + 22, B() + 1);
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong near detour edge rejected");
    Known(2); Put(T() + 24, A() + 17);
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong absolute continuation rejected");
    Known(2); Put(Backup() + 17, std::uint8_t{1});
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "last original18 byte mutation rejected");
    Known(1); Put(B() + 0x10, std::uint8_t{0});
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "immutable callback template mutation rejected");
    // Same pinned FILE/path, independently corrupted mapped-image bytes. A
    // comparison of B to the mapped template alone cannot establish identity.
    const auto templateByte = module[TemplateRva + 0x10];
    Known(1); module[TemplateRva + 0x10] ^= 1;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped module template rejected with pinned disk identity");
    module[TemplateRva + 0x10] = templateByte;
    Known(1); module[TemplateRva + 0x10] ^= 1;
    *reinterpret_cast<std::uint8_t*>(B() + 0x10) ^= 1;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "identically corrupted mapped and copied template rejected");
    module[TemplateRva + 0x10] = templateByte;
    const auto thunkByte = module[CallbackThunk + 1];
    Known(1); module[CallbackThunk + 1] ^= 1;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped callback thunk rejected with pinned disk identity");
    module[CallbackThunk + 1] = thunkByte;
    const auto bodyByte = module[CallbackBody];
    Known(1); module[CallbackBody] ^= 1;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped callback body rejected with pinned disk identity");
    module[CallbackBody] = bodyByte;
    Known(1); denyBlock = true;
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "unreadable block protection rejected"); denyBlock = false;
    Known(1); Put(B() + 0x17f, UINTPTR_MAX - uintptr_t{4});
    Check(!RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "overflowing continuation pointer rejected");
    Raw(); Put(A(), std::uint8_t{0xe9}); privateAvailable = true;
    Check(RendererOwnershipAdapter::IsAdmitted(EvaluateCurrentMenuAdmission()), "existing verified private owner preserved"); privateAvailable = false;
    Raw();
    // Authoritative native UI chain resides entirely in private inert buffers.
    Put(g_exeBase + MinimapMenu10::UiContextRva, Arena() + 0x6000);
    Put(Arena() + 0x6000 + MinimapMenu10::HolderOffset, Arena() + 0x7000);
    Put(Arena() + 0x7000, Arena() + 0x7800);
    Put(Arena() + 0x7800 + MinimapMenu10::PlayStateOffset, Arena() + 0x8000);
    g_originalMenuRenderer.store(A());
    auto* retained = RetainCall();
    Require(IsRetainedCurrentMenuCallsiteOwned(retained), "real retained relay/native target proof");
    CaptureCase("native retained capture accepted after original", true);
    Known(1); CaptureCase("public near retained capture accepted after original", true);
    Known(2); CaptureCase("public absolute retained capture accepted after original", true);
    Known(1); CaptureCase("changed callback template clears staged snapshot", false, [] { Put(B() + 0x10, std::uint8_t{0}); });
    Known(1); CaptureCase("changed mapped template after original clears staged snapshot", false,
        [] { module[TemplateRva + 0x10] ^= 1; });
    module[TemplateRva + 0x10] = templateByte;
    Known(1); CaptureCase("mapped and copied template change after original clears staged snapshot", false,
        [] { module[TemplateRva + 0x10] ^= 1; *reinterpret_cast<std::uint8_t*>(B() + 0x10) ^= 1; });
    module[TemplateRva + 0x10] = templateByte;
    Known(1); CaptureCase("owner loss clears staged snapshot", false, [] { moduleAvailable = false; }); moduleAvailable = true;
    Raw(); CaptureCase("raw to known transition rejects mixed sample", false, [] { Known(1); });
    CaptureCase("next stable public sample recovers", true);
    Known(1); CaptureCase("near to absolute transition rejects mixed sample", false, [] { Known(2); });
    Raw(); CaptureCase("epoch change cannot republish staged screen", false, [] { ClearMenuVisibilityObservation(); });
    Raw(); CaptureCase("runtime deactivation cannot republish staged screen", false, [] { g_runtimeCallbacksActive.store(false); });
    Raw(); CaptureCase("menu result hides gameplay", false, [] { Put(Arena() + 0x8000 + MinimapMenu10::ScreenOffset, std::uint8_t{9}); });
    Raw(); CaptureCase("failed post context read retires candidate", false, [] { Put(g_exeBase + MinimapMenu10::UiContextRva, uintptr_t{0}); });
    Put(g_exeBase + MinimapMenu10::UiContextRva, Arena() + 0x6000);
    PriorGameplayOverlapCase("prior gameplay stays visible during raw forwarding without premature publication", false);
    PriorGameplayOverlapCase("prior gameplay stays visible during public forwarding without premature publication", true);
    InvalidPreAdmissionClearsPriorCase();
    NonGameplayPreScreenClearsPriorCase();
    FailedAfterSampleClearsPriorCase();
    RealSehCopyCase();
    Raw(); const auto tail = g_exeBase + RVA_MENU_RENDERER_CALL_1_0 + 5;
    const std::uint8_t originalTail = *reinterpret_cast<std::uint8_t*>(tail);
    Put(tail, std::uint8_t{0x90}); CaptureCase("following native CALL remains guarded", false); Put(tail, originalTail);
    Raw(); const auto caller = g_exeBase + 0xbc0446;
    const std::uint8_t originalCaller = *reinterpret_cast<std::uint8_t*>(caller);
    Put(caller, std::uint8_t{0x90}); CaptureCase("non-owned context publication guard remains exact", false); Put(caller, originalCaller);
    Raw(); const auto relay = retained->shellcode->data->address;
    Put(relay + 13, static_cast<std::uint8_t>(*reinterpret_cast<std::uint8_t*>(relay + 13) ^ 1));
    CaptureCase("tampered retained relay rejects capture", false);
    DestroyCall(retained);
    g_runtimeCallbacksActive.store(false); g_menuVisibilityHookActive.store(false);
    g_menuVisibilitySnapshot.Clear(); nativeCalls = 0;
    CaptureMenuVisibilityCallsiteHook();
    Check(nativeCalls == 1 && nativeAddressCorrect && !g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()),
        "inactive retained callback still forwards exactly once without publishing");
    Check(moduleAcquires == moduleReleases, "temporary module references balanced");
    std::cout << "cases=" << cases << " failed=" << failed << '\n';
    g_originalMenuRenderer.store(0); g_exeBase = 0; g_exeImageSize = 0;
    VirtualFree(game, 0, MEM_RELEASE); VirtualFree(module, 0, MEM_RELEASE);
    return failed ? 1 : 0;
}
