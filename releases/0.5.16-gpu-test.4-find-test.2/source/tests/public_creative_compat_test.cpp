// MM-PUBLIC-CREATIVE-MINIMAP-IMPLEMENTATION-20261020-01
// Real production TU, with ONLY OS module metadata and the final native-call
// boundary substituted. Fixture pages are private and NX; the SEH case briefly
// marks one owned module page NOACCESS and restores it before further reads.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include "../src/aggregate_timing.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
namespace PublicCreativeTest { LPVOID WINAPI Allocate(LPVOID, SIZE_T, DWORD, DWORD); }
namespace PublicCreativeTest
{
    ULONGLONG WINAPI TickCount64();
    NTSTATUS WINAPI OpenAlgorithmProvider(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG);
    NTSTATUS WINAPI CloseAlgorithmProvider(BCRYPT_ALG_HANDLE, ULONG);
    NTSTATUS WINAPI CreateHash(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
}
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
    BOOL WINAPI Working(HANDLE, PVOID, DWORD);
    HANDLE WINAPI BackingFile(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
    void Dispatch(void (*)());
}
#define BCryptOpenAlgorithmProvider PublicCreativeTest::OpenAlgorithmProvider
#define BCryptCloseAlgorithmProvider PublicCreativeTest::CloseAlgorithmProvider
#define BCryptCreateHash PublicCreativeTest::CreateHash
#define GetModuleHandleExW PublicCreativeTest::Module
#define CreateFileW PublicCreativeTest::BackingFile
#define GetModuleFileNameW PublicCreativeTest::ModulePath
#define GetProcAddress PublicCreativeTest::Procedure
#define FreeLibrary PublicCreativeTest::Release
#define VirtualQuery PublicCreativeTest::Query
#define K32QueryWorkingSetEx PublicCreativeTest::Working
#define VirtualAlloc PublicCreativeTest::Allocate
#define GetTickCount64 PublicCreativeTest::TickCount64
// The baseline TU has exactly one original() call, in the real HUD wrapper.
// The argument retains gameBase+RendererRva; Dispatch never calls that address.
#define original() PublicCreativeTest::Dispatch(original)
#include "../src/dllmain.cpp"
#undef original
#undef BCryptOpenAlgorithmProvider
#undef BCryptCloseAlgorithmProvider
#undef BCryptCreateHash
#undef VirtualAlloc
#undef VirtualQuery
#undef K32QueryWorkingSetEx
#undef FreeLibrary
#undef GetTickCount64
#undef GetProcAddress
#undef GetModuleFileNameW
#undef GetModuleHandleExW
#undef CreateFileW

namespace PublicCreativeTest
{
    size_t CreativeImageSize = 0x555000;
    size_t CallbackThunk = 0x4877;
    size_t CallbackBody = 0x18b350;
    size_t TemplateRva = 0x3eb7e0;
    constexpr size_t TemplateSize = 0x187;
    size_t SlotRva = 0x514ba8;
    constexpr std::uint8_t Prefix[18] = {
        0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec,0x10,0x03,0,0};
    std::uint8_t* module = nullptr;
    std::uint8_t* game = nullptr;
    size_t gameSize = 0;
    std::wstring modulePath;
    std::uint8_t* findModule = nullptr;
    bool findModuleAvailable = true;
    std::wstring findModulePath;
    constexpr size_t FindImageSize = 0x106000;
    unsigned backingFileOpenCalls = 0;
    bool denyBackingFileOpens = false;
    bool moduleAvailable = true;
    bool denyBlock = false;
    size_t moduleAcquires = 0, moduleReleases = 0, nativeCalls = 0;
    std::uint64_t mockTickCount = 0;
    bool useMockTickCount = false;
    bool nativeAddressCorrect = false, visibleInsideOriginal = false;
    std::function<void()> duringOriginal;
    size_t cases = 0, failed = 0;
    std::atomic<unsigned> algorithmOpenCalls{0}, algorithmCloseCalls{0};
    std::atomic<unsigned> hashCreateCalls{0}, failHashCreateAt{0};
    std::function<void(unsigned)> onHashCreate;
    std::atomic<bool> failNextAlgorithmOpen{false};
    bool measureQuery = false;
    unsigned long long queryTicks = 0, queryCount = 0, queryMaxTicks = 0;
    unsigned long long Clock() { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart; }

    uintptr_t A() { return reinterpret_cast<uintptr_t>(game) + ClientRva(MinimapMenu10::RendererRva); }
    uintptr_t Arena() { return reinterpret_cast<uintptr_t>(game) + gameSize; }
    uintptr_t T() { return Arena() + 0x1000; }
    uintptr_t B() { return Arena() + 0x2000; }
    uintptr_t O() { return Arena() + 0x3000; }
    uintptr_t H() { return O() + 0x80; }
    uintptr_t Backup() { return Arena() + 0x4000; }

    ULONGLONG WINAPI TickCount64()
    { return useMockTickCount ? static_cast<ULONGLONG>(mockTickCount) : ::GetTickCount64(); }

    NTSTATUS WINAPI OpenAlgorithmProvider(BCRYPT_ALG_HANDLE* algorithm, LPCWSTR id,
        LPCWSTR implementation, ULONG flags)
    {
        ++algorithmOpenCalls;
        const NTSTATUS status = ::BCryptOpenAlgorithmProvider(algorithm, id, implementation, flags);
        if (status >= 0 && failNextAlgorithmOpen.exchange(false))
            return static_cast<NTSTATUS>(0xC0000001u);
        return status;
    }
    NTSTATUS WINAPI CloseAlgorithmProvider(BCRYPT_ALG_HANDLE algorithm, ULONG flags)
    {
        ++algorithmCloseCalls;
        return ::BCryptCloseAlgorithmProvider(algorithm, flags);
    }
    NTSTATUS WINAPI CreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* hash,
        PUCHAR object, ULONG objectLength, PUCHAR secret, ULONG secretLength, ULONG flags)
    {
        const unsigned call = ++hashCreateCalls;
        if (onHashCreate) onHashCreate(call);
        if (failHashCreateAt.load() == call)
        {
            if (hash) *hash = nullptr;
            return static_cast<NTSTATUS>(0xC0000001u);
        }
        return ::BCryptCreateHash(algorithm, hash, object, objectLength, secret, secretLength, flags);
    }

    LPVOID WINAPI Allocate(LPVOID address, SIZE_T size, DWORD allocation, DWORD)
    { return ::VirtualAlloc(address, size, allocation, PAGE_READWRITE); }

    bool Inside(uintptr_t p, uintptr_t first, size_t size)
    { return p >= first && p - first < size; }

    BOOL WINAPI Module(DWORD flags, LPCWSTR name, HMODULE* result)
    {
        if (!result) return FALSE;
        *result = nullptr;
        if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
            if (moduleAvailable && findModuleAvailable && findModule && Inside(reinterpret_cast<uintptr_t>(name),
                reinterpret_cast<uintptr_t>(findModule), FindImageSize)) {
                *result = reinterpret_cast<HMODULE>(findModule); ++moduleAcquires; return TRUE;
            }
            if (!moduleAvailable || !Inside(reinterpret_cast<uintptr_t>(name),
                reinterpret_cast<uintptr_t>(module), CreativeImageSize)) return FALSE;
            *result = reinterpret_cast<HMODULE>(module);
            ++moduleAcquires;
            return TRUE;
        }
        return FALSE;
    }
    HANDLE WINAPI BackingFile(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,
        DWORD disposition,DWORD attributes,HANDLE templateFile)
    {
        ++backingFileOpenCalls;
        if (denyBackingFileOpens) return INVALID_HANDLE_VALUE;
        return ::CreateFileW(path,access,sharing,security,disposition,attributes,templateFile);
    }
    DWORD WINAPI ModulePath(HMODULE value, LPWSTR output, DWORD cap)
    {
        if (findModule && value == reinterpret_cast<HMODULE>(findModule)) {
            if (!output || cap <= findModulePath.size()) return 0;
            std::copy(findModulePath.begin(), findModulePath.end(), output);
            output[findModulePath.size()] = 0;
            return static_cast<DWORD>(findModulePath.size());
        }
        if (value != reinterpret_cast<HMODULE>(module) || !output || cap <= modulePath.size()) return 0;
        std::copy(modulePath.begin(), modulePath.end(), output);
        output[modulePath.size()] = 0;
        return static_cast<DWORD>(modulePath.size());
    }
    BOOL WINAPI Release(HMODULE value)
    {
        if (findModule && value == reinterpret_cast<HMODULE>(findModule)) {
            ++moduleReleases; return TRUE;
        }
        if (value == reinterpret_cast<HMODULE>(module)) {
            ++moduleReleases; return TRUE;
        }
        return FALSE;
    }
    FARPROC WINAPI Procedure(HMODULE, LPCSTR)
    {
        return nullptr; // No public DLL callable can escape the test boundary.
    }
    SIZE_T WINAPI Query(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T cap)
    {
        const auto start = measureQuery ? Clock() : 0;
        const SIZE_T read = ::VirtualQuery(address, info, cap);
        if (measureQuery) { const auto elapsed = Clock() - start; queryTicks += elapsed; ++queryCount; queryMaxTicks = (std::max)(queryMaxTicks,elapsed); }
        if (!read || !info) return read;
        const uintptr_t p = reinterpret_cast<uintptr_t>(address);
        if (findModule && Inside(p, reinterpret_cast<uintptr_t>(findModule), FindImageSize)) {
            info->BaseAddress = findModule; info->AllocationBase = findModule;
            info->RegionSize = FindImageSize; info->Type = MEM_IMAGE;
            info->Protect = PAGE_EXECUTE_READ;
        } else if (Inside(p, reinterpret_cast<uintptr_t>(module), CreativeImageSize)) {
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
    BOOL WINAPI Working(HANDLE handle, PVOID output, DWORD bytes)
    {
        const BOOL ok=::K32QueryWorkingSetEx(handle,output,bytes);
        if(!ok || !output) return ok;
        auto* rows=static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(output);
        for(DWORD i=0;i<bytes/sizeof(*rows);++i) {
            auto& row=rows[i]; const auto p=reinterpret_cast<uintptr_t>(row.VirtualAddress);
            // The same fixture metadata seam as Query; no real page is executable.
            if(row.VirtualAttributes.Valid &&
                ((findModule && Inside(p,reinterpret_cast<uintptr_t>(findModule),FindImageSize)) ||
                 Inside(p,reinterpret_cast<uintptr_t>(module),CreativeImageSize) ||
                 Inside(p,reinterpret_cast<uintptr_t>(game),gameSize) || Inside(p,Arena(),0x10000))) {
                if(denyBlock && Inside(p,B(),0x1000)) row.VirtualAttributes.Valid=0;
                else row.VirtualAttributes.Win32Protection=PAGE_EXECUTE_READ;
            }
        }
        return ok;
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
    bool IsAdmitted(MenuAdmission admission)
    { return admission == MenuAdmission::raw || admission == MenuAdmission::publicCreative; }
    void Require(bool ok, const char* name)
    { if (!ok) { std::cerr << "FIXTURE ERROR " << name << '\n'; std::exit(2); } }

    void HashProviderChecks()
    {
        cases = failed = 0;
        algorithmOpenCalls = 0;
        algorithmCloseCalls = 0;
        failNextAlgorithmOpen = true;
        {
            PublicCreativeCompat::Hash failedHash;
            PublicCreativeCompat::Digest failedDigest{};
            Check(!failedHash.Add("abc", 3) && !failedHash.Finish(failedDigest),
                "provider initialization failure fails this hash closed");
        }
        Check(algorithmOpenCalls == 1 && algorithmCloseCalls == 1,
            "failed partial provider initialization is closed and not cached");

        auto digest = [](const char* input, std::size_t size, PublicCreativeCompat::Digest& output) {
            PublicCreativeCompat::Hash hash;
            return hash.Add(input, size) && hash.Finish(output);
        };
        PublicCreativeCompat::Digest abc{}, abcd{}, repeated{};
        Check(digest("abc", 3, abc) && PublicCreativeCompat::HexEquals(abc,
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
            "retry after transient provider initialization failure hashes known input");
        Check(digest("abcd", 4, abcd) && PublicCreativeCompat::HexEquals(abcd,
            "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589") &&
            digest("abc", 3, repeated) && repeated == abc,
            "reused provider keeps a fresh independent hash state for each input");
        Check(algorithmOpenCalls == 2,
            "one successful owned SHA256 provider is reused after retry");

        std::atomic<bool> concurrentOk{true};
        std::vector<std::thread> workers;
        for (unsigned thread = 0; thread < 8; ++thread)
            workers.emplace_back([thread, &concurrentOk, &digest]() {
                for (unsigned iteration = 0; iteration < 16; ++iteration) {
                    const std::string input = "thread-" + std::to_string(thread) + "-" +
                        std::to_string(iteration);
                    PublicCreativeCompat::Digest expected{}, actual{};
                    if (!digest(input.data(), input.size(), expected) ||
                        !digest(input.data(), input.size(), actual) || expected != actual)
                        concurrentOk = false;
                }
            });
        for (auto& worker : workers) worker.join();
        Check(concurrentOk.load(), "concurrent hash calls retain independent fresh hash handles");
        Check(algorithmOpenCalls == 2 && algorithmCloseCalls == 1,
            "concurrent hash calls share only the successfully initialized provider");
        std::cout << "cases=" << cases << " failed=" << failed << '\n';
    }

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
        const uintptr_t call = reinterpret_cast<uintptr_t>(game) + ClientRva(RVA_MENU_RENDERER_CALL_1_0);
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
        const auto call = reinterpret_cast<uintptr_t>(game) + ClientRva(RVA_MENU_RENDERER_CALL_1_0);
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

    bool ReadTimingQpc(void*, std::uint64_t* ticks) noexcept
    {
        if (ticks == nullptr)
            return false;
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value))
            return false;
        *ticks = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    void AggregateTimingCallbackCase()
    {
        using namespace MinimapAggregateTiming;
        auto& collector = DiagnosticCollector();
        LARGE_INTEGER frequency{}, start{};
        Require(QueryPerformanceFrequency(&frequency) != FALSE &&
            QueryPerformanceCounter(&start) != FALSE, "query diagnostic clock");
        collector.Initialize(&ReadTimingQpc, nullptr,
            static_cast<std::uint64_t>(frequency.QuadPart),
            static_cast<std::uint64_t>(start.QuadPart));

        Snapshot initialized{};
        Require(collector.Drain(initialized), "initial diagnostic drain");
        const auto outcomeIndex = [](OwnershipStage stage, OwnershipResult result) {
            // Snapshot ownership outcomes are stage-major, then result-major.
            return static_cast<std::size_t>(stage) * 3 +
                static_cast<std::size_t>(result);
        };
        const auto metricIndex = [](Metric metric) {
            return static_cast<std::size_t>(metric);
        };
        const auto runPublicMode = [&](unsigned kind,
            const char* preDrainMessage, const char* postDrainMessage,
            const char* forwardMessage, const char* spanMessage,
            const char* proofMessage) {
            Known(kind);
            ResetCapture();
            Snapshot before{};
            Require(collector.Drain(before), preDrainMessage);
            CaptureMenuVisibilityCallsiteHook();
            const bool forwardedOnce = nativeCalls == 1 && nativeAddressCorrect;
            Snapshot after{};
            Require(collector.Drain(after), postDrainMessage);

            const auto& callbackBefore = before.metrics[metricIndex(Metric::MenuCallback)];
            const auto& callbackAfter = after.metrics[metricIndex(Metric::MenuCallback)];
            const auto& preBefore = before.metrics[metricIndex(Metric::OwnershipPreProof)];
            const auto& preAfter = after.metrics[metricIndex(Metric::OwnershipPreProof)];
            const auto& originalBefore = before.metrics[metricIndex(Metric::OwnershipOriginalForward)];
            const auto& originalAfter = after.metrics[metricIndex(Metric::OwnershipOriginalForward)];
            const auto& postBefore = before.metrics[metricIndex(Metric::OwnershipPostProof)];
            const auto& postAfter = after.metrics[metricIndex(Metric::OwnershipPostProof)];
            const auto& attemptBefore = before.metrics[metricIndex(Metric::ForwardAttempt)];
            const auto& attemptAfter = after.metrics[metricIndex(Metric::ForwardAttempt)];
            const auto& returnBefore = before.metrics[metricIndex(Metric::ForwardReturn)];
            const auto& returnAfter = after.metrics[metricIndex(Metric::ForwardReturn)];
            const auto& proofPreBefore = before.metrics[metricIndex(Metric::CreativeProofCallbackPre)];
            const auto& proofPreAfter = after.metrics[metricIndex(Metric::CreativeProofCallbackPre)];
            const auto& proofPostBefore = before.metrics[metricIndex(Metric::CreativeProofCallbackPost)];
            const auto& proofPostAfter = after.metrics[metricIndex(Metric::CreativeProofCallbackPost)];
            const auto prePublic = outcomeIndex(OwnershipStage::Pre, OwnershipResult::PublicCreative);
            const auto postPublic = outcomeIndex(OwnershipStage::Post, OwnershipResult::PublicCreative);
            const auto preRaw = outcomeIndex(OwnershipStage::Pre, OwnershipResult::Raw);
            const auto postRaw = outcomeIndex(OwnershipStage::Post, OwnershipResult::Raw);
            const auto preRejected = outcomeIndex(OwnershipStage::Pre, OwnershipResult::Rejected);
            const auto postRejected = outcomeIndex(OwnershipStage::Post, OwnershipResult::Rejected);

            Check(forwardedOnce, forwardMessage);
            Check(callbackAfter.entries == callbackBefore.entries + 1 &&
                preAfter.timedSamples == preBefore.timedSamples + 1 &&
                originalAfter.timedSamples == originalBefore.timedSamples + 1 &&
                postAfter.timedSamples == postBefore.timedSamples + 1 &&
                attemptAfter.entries == attemptBefore.entries + 1 &&
                returnAfter.entries == returnBefore.entries + 1,
                spanMessage);
            Check(proofPreAfter.entries == proofPreBefore.entries + 1 &&
                proofPostAfter.entries == proofPostBefore.entries + 1 &&
                after.ownershipOutcomes[prePublic] == before.ownershipOutcomes[prePublic] + 1 &&
                after.ownershipOutcomes[postPublic] == before.ownershipOutcomes[postPublic] + 1 &&
                after.ownershipOutcomes[preRaw] == before.ownershipOutcomes[preRaw] &&
                after.ownershipOutcomes[postRaw] == before.ownershipOutcomes[postRaw] &&
                after.ownershipOutcomes[preRejected] == before.ownershipOutcomes[preRejected] &&
                after.ownershipOutcomes[postRejected] == before.ownershipOutcomes[postRejected],
                proofMessage);
        };

        runPublicMode(1,
            "public near pre-callback diagnostic drain",
            "public near post-callback diagnostic drain",
            "public near Creative callback retains the exact native target and forwards once",
            "public near callback records one pre, returned-original, and post span",
            "public near callback records callback-origin proof and PublicCreative outcomes only");
        runPublicMode(2,
            "public absolute pre-callback diagnostic drain",
            "public absolute post-callback diagnostic drain",
            "public absolute Creative callback retains the exact native target and forwards once",
            "public absolute callback records one pre, returned-original, and post span",
            "public absolute callback records callback-origin proof and PublicCreative outcomes only");
        Raw();
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
        duringOriginal = [] { Put(g_exeBase + ClientRva(MinimapMenu10::UiContextRva), uintptr_t{0}); };
        CaptureMenuVisibilityCallsiteHook();
        duringOriginal = {};
        Put(g_exeBase + ClientRva(MinimapMenu10::UiContextRva), Arena() + 0x6000);
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

    void CheckHashCount(unsigned actual, unsigned expected, const char* name)
    {
        ++cases;
        const bool ok = actual == expected;
        if (!ok) ++failed;
        std::cout << (ok ? "PASS " : "FAIL ") << name
            << " expected=" << expected << " observed=" << actual << '\n';
    }

    void PrintProofDigest(unsigned kind, const PublicCreativeCompat::Digest& digest)
    {
        constexpr char hex[] = "0123456789abcdef";
        std::cout << "proof_digest_kind=" << kind << ':';
        for (const auto byte : digest)
            std::cout << hex[byte >> 4] << hex[byte & 15];
        std::cout << '\n';
    }

    void ProofOptimizationChecks(const wchar_t* publicModuleDataPath)
    {
        using PublicCreativeCompat::Digest;
        using PublicCreativeCompat::ImageContent;
        using PublicCreativeCompat::Proof;

        cases = failed = 0;
        hashCreateCalls = 0;
        failHashCreateAt = 0;
        onHashCreate = {};
        moduleAvailable = true;
        denyBlock = false;
        CreativeImageSize = PublicCreativeCompat::Creative214.imageSize;
        CallbackThunk = PublicCreativeCompat::Creative214.callback;
        CallbackBody = PublicCreativeCompat::Creative214.body;
        TemplateRva = PublicCreativeCompat::Creative214.pattern;
        SlotRva = PublicCreativeCompat::Creative214.slot;

        const auto publicFile = ReadFile(publicModuleDataPath);
        Require(ImageSize(publicFile) == CreativeImageSize, "pinned inert Creative image size");

        constexpr std::size_t syntheticGameSize = 0x02000000;
        constexpr std::uintptr_t syntheticGameBase = 0x0000010000000000ULL;
        constexpr std::uintptr_t syntheticModuleBase = 0x0000020000000000ULL;
        gameSize = syntheticGameSize;
        g_clientProfile = MinimapClient10::Profile::Build25988036;
        game = static_cast<std::uint8_t*>(::VirtualAlloc(reinterpret_cast<void*>(syntheticGameBase),
            gameSize + 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        module = static_cast<std::uint8_t*>(::VirtualAlloc(reinterpret_cast<void*>(syntheticModuleBase),
            CreativeImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Require(game && module && reinterpret_cast<std::uintptr_t>(game) == syntheticGameBase &&
            reinterpret_cast<std::uintptr_t>(module) == syntheticModuleBase,
            "fixed-address private fixture allocations");
        MEMORY_BASIC_INFORMATION gameRegion{}, moduleRegion{};
        Require(::VirtualQuery(game, &gameRegion, sizeof(gameRegion)) == sizeof(gameRegion) &&
            gameRegion.State == MEM_COMMIT && (gameRegion.Protect & 0xff) == PAGE_READWRITE,
            "synthetic game fixture is RW/NX");
        Require(::VirtualQuery(module, &moduleRegion, sizeof(moduleRegion)) == sizeof(moduleRegion) &&
            moduleRegion.State == MEM_COMMIT && (moduleRegion.Protect & 0xff) == PAGE_READWRITE,
            "Creative data fixture is RW/NX");
        MapData(publicFile, module, CreativeImageSize);

        g_exeBase = reinterpret_cast<std::uintptr_t>(game);
        g_exeImageSize = gameSize;
        g_client10 = true;
        modulePath = publicModuleDataPath;

        const auto warmIdentity = [&](unsigned kind) {
            Known(kind);
            PublicCreativeCompat::ForgetIdentity();
            Proof proof{};
            Require(PublicCreativeCompat::Inspect(A(), proof), "warm pinned ownership proof");
            Require(proof.kind == kind, "warm proof detour kind");
            hashCreateCalls = 0;
            return proof;
        };

        std::array<Digest, 2> expectedDigests{};
        for (unsigned kind : {1u, 2u})
        {
            const Proof previous = warmIdentity(kind);
            Proof measured{};
            const bool accepted = PublicCreativeCompat::Inspect(A(), measured);
            const char* detour = kind == 1 ? "near" : "absolute";
            Check(accepted, kind == 1 ? "warmed near proof remains accepted" :
                "warmed absolute proof remains accepted");
            Check(measured.kind == kind, kind == 1 ? "near proof retains detour kind" :
                "absolute proof retains detour kind");
            Check(measured.digest == previous.digest, kind == 1 ?
                "near proof digest is unchanged across warm sampling" :
                "absolute proof digest is unchanged across warm sampling");
            const std::string budgetName = std::string(detour) + " detour uses five warmed CNG hash contexts";
            CheckHashCount(hashCreateCalls.load(), 5, budgetName.c_str());
            expectedDigests[kind - 1] = measured.digest;
            PrintProofDigest(kind, measured.digest);
        }

        warmIdentity(1);
        const auto changedBlockByte = *reinterpret_cast<std::uint8_t*>(B() + 0x40);
        onHashCreate = [changedBlockByte](unsigned call) {
            if (call == 4) *reinterpret_cast<std::uint8_t*>(B() + 0x40) = changedBlockByte ^ 1;
        };
        Proof changedChain{};
        const bool changedChainAccepted = PublicCreativeCompat::Inspect(A(), changedChain);
        onHashCreate = {};
        *reinterpret_cast<std::uint8_t*>(B() + 0x40) = changedBlockByte;
        Check(!changedChainAccepted, "second chain snapshot mutation rejects proof");
        Check(changedChain.digest == Digest{}, "second chain mutation clears proof digest");
        std::cout << "second_chain_mutation_contexts=" << hashCreateCalls.load() << '\n';

        warmIdentity(1);
        const auto changedBodyByte = module[CallbackBody + 0x20];
        onHashCreate = [changedBodyByte](unsigned call) {
            if (call == 4) module[CallbackBody + 0x20] = changedBodyByte ^ 1;
        };
        Proof changedImage{};
        const bool changedImageAccepted = PublicCreativeCompat::Inspect(A(), changedImage);
        onHashCreate = {};
        module[CallbackBody + 0x20] = changedBodyByte;
        Check(!changedImageAccepted, "second image snapshot mutation rejects proof");
        Check(changedImage.digest == Digest{}, "second image mutation clears proof digest");
        std::cout << "second_image_mutation_contexts=" << hashCreateCalls.load() << '\n';

        warmIdentity(1);
        onHashCreate = [](unsigned call) { if (call == 4) denyBlock = true; };
        Proof unreadableSecondSnapshot{};
        const bool unreadableAccepted = PublicCreativeCompat::Inspect(A(), unreadableSecondSnapshot);
        onHashCreate = {};
        denyBlock = false;
        Check(!unreadableAccepted, "second snapshot read failure rejects proof");
        Check(unreadableSecondSnapshot.digest == Digest{}, "second snapshot read failure clears proof digest");
        std::cout << "second_snapshot_read_failure_contexts=" << hashCreateCalls.load() << '\n';

        warmIdentity(1);
        hashCreateCalls = 0;
        failHashCreateAt = 4;
        Proof failedRequiredHash{};
        const bool acceptedWithHashFailure = PublicCreativeCompat::Inspect(A(), failedRequiredHash);
        failHashCreateAt = 0;
        Check(!acceptedWithHashFailure, "required image hash failure rejects proof");
        Check(failedRequiredHash.digest == Digest{}, "required hash failure clears proof digest");
        Proof recovered{};
        const bool recoveredAfterHashFailure = PublicCreativeCompat::Inspect(A(), recovered);
        Check(recoveredAfterHashFailure && recovered.digest == expectedDigests[0],
            "independent stable proof recovers after required hash failure");

        Known(1);
        auto blockByte = *reinterpret_cast<std::uint8_t*>(B() + 0x40);
        *reinterpret_cast<std::uint8_t*>(B() + 0x40) = blockByte ^ 1;
        PublicCreativeCompat::Chain chain{};
        Check(!PublicCreativeCompat::ReadChain(A(), chain), "full ReadChain wrapper rejects changed pinned template");
        *reinterpret_cast<std::uint8_t*>(B() + 0x40) = blockByte;

        ImageContent image{};
        const auto patternByte = module[TemplateRva + 0x10];
        module[TemplateRva + 0x10] = patternByte ^ 1;
        Check(!PublicCreativeCompat::ReadImage(reinterpret_cast<std::uintptr_t>(module),
            reinterpret_cast<std::uintptr_t>(module) + CallbackThunk, image,
            PublicCreativeCompat::Creative214), "full ReadImage wrapper rejects changed pinned template");
        module[TemplateRva + 0x10] = patternByte;
        const auto bodyByte = module[CallbackBody + 0x20];
        module[CallbackBody + 0x20] = bodyByte ^ 1;
        Check(!PublicCreativeCompat::ReadImage(reinterpret_cast<std::uintptr_t>(module),
            reinterpret_cast<std::uintptr_t>(module) + CallbackThunk, image,
            PublicCreativeCompat::Creative214), "full ReadImage wrapper rejects changed pinned body");
        module[CallbackBody + 0x20] = bodyByte;

        const Proof beforeCallback = warmIdentity(1);
        Proof pre{};
        const bool preAccepted = PublicCreativeCompat::Inspect(A(), pre,
            PublicCreativeCompat::ProofOrigin::CallbackPre);
        Check(preAccepted && pre.digest == beforeCallback.digest,
            "callback pre-sample independently verifies current proof");
        const auto callbackBodyByte = module[CallbackBody + 0x20];
        duringOriginal = [callbackBodyByte] { module[CallbackBody + 0x20] = callbackBodyByte ^ 1; };
        nativeCalls = 0;
        Dispatch(reinterpret_cast<void(*)()>(A()));
        duringOriginal = {};
        Check(nativeCalls == 1, "synthetic renderer boundary forwards exactly once");
        Proof post{};
        const bool postAccepted = PublicCreativeCompat::Inspect(A(), post,
            PublicCreativeCompat::ProofOrigin::CallbackPost);
        Check(!postAccepted && post.digest == Digest{},
            "fresh callback post-sample rejects intervening mutation");
        module[CallbackBody + 0x20] = callbackBodyByte;
        Proof callbackRecovery{};
        Check(PublicCreativeCompat::Inspect(A(), callbackRecovery,
            PublicCreativeCompat::ProofOrigin::CallbackPost) &&
            callbackRecovery.digest == beforeCallback.digest,
            "callback proof recovers on a later stable sample");

        PublicCreativeCompat::ForgetIdentity();
        ::VirtualFree(game, 0, MEM_RELEASE);
        ::VirtualFree(module, 0, MEM_RELEASE);
        game = nullptr; module = nullptr; gameSize = 0;
        g_exeBase = 0; g_exeImageSize = 0; g_client10 = false;
        std::cout << "cases=" << cases << " failed=" << failed << '\n';
    }
    void FindChainChecks(const wchar_t* gamePath, const wchar_t* creativePath, const wchar_t* findPath)
    {
        CreativeImageSize = 0x5e3000; CallbackThunk = 0x4ff2; CallbackBody = 0x1d7470;
        TemplateRva = 0x44f690; SlotRva = 0x591450;
        g_clientProfile = MinimapClient10::IdentifyFile(gamePath);
        Require(g_clientProfile == MinimapClient10::Profile::Build26082781, "current game fixture for Find chain");
        const auto gameFile = ReadFile(gamePath), creativeFile = ReadFile(creativePath), findFile = ReadFile(findPath);
        gameSize = ImageSize(gameFile);
        Require(ImageSize(creativeFile) == CreativeImageSize && ImageSize(findFile) == FindImageSize,
            "authentic Creative 3 and Find 1.1.2 fixture image sizes");
        game = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, gameSize + 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
        module = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, CreativeImageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
        findModule = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, FindImageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
        Require(game && module && findModule, "three private NX images for Find chain");
        MapData(gameFile, game, gameSize); MapData(creativeFile, module, CreativeImageSize);
        MapData(findFile, findModule, FindImageSize);
        g_exeBase = reinterpret_cast<uintptr_t>(game); g_exeImageSize = gameSize; g_client10 = true;
        modulePath = creativePath; findModulePath = findPath;
        const auto relay = Arena() + 0x9000, continuation = Arena() + 0xa000;
        const auto findBase = reinterpret_cast<uintptr_t>(findModule);
        auto known = [&] {
            Known(1);
            std::uint8_t underlying[18]; std::memset(underlying, 0x90, sizeof(underlying));
            underlying[0]=0x48; underlying[1]=0xb8;
            std::memcpy(underlying+2, &relay, 8); underlying[10]=0xff; underlying[11]=0xe0;
            std::memcpy(reinterpret_cast<void*>(A()), underlying, 18);
            std::memset(reinterpret_cast<void*>(A()), 0x90, 10);
            Rel32(A(), 0xe9, T()+15);
            std::memset(reinterpret_cast<void*>(T()), 0, 32);
            std::memcpy(reinterpret_cast<void*>(T()), underlying, 10);
            Rel32(T()+10, 0xe9, A()+10);
            const std::uint8_t indirect[6]={0xff,0x25,0,0,0,0};
            std::memcpy(reinterpret_cast<void*>(T()+15), indirect, 6); Put(T()+21,B());
            std::memcpy(reinterpret_cast<void*>(Backup()), underlying, 10);
            Put(H()+0x28,uintptr_t{29}); Put(H()+0x38,Backup()+10);
            Put(H()+0x40,Backup()+10); Put(H()+0x48,uintptr_t{29});
            std::memset(reinterpret_cast<void*>(relay), 0, 24);
            const std::uint8_t findJump[6]={0xff,0x25,10,0,0,0};
            std::memcpy(reinterpret_cast<void*>(relay),findJump,6); Put(relay+16,findBase+0xaf40);
            std::memcpy(reinterpret_cast<void*>(continuation),Prefix,18);
            std::memcpy(reinterpret_cast<void*>(continuation+18),indirect,6); Put(continuation+24,A()+18);
            Put(findBase+0x6f680,continuation);
        };
        known(); PublicCreativeCompat::Proof proof{};
        Check(PublicCreativeCompat::Inspect(A(),proof), "Creative-over-Find exact observed ten-byte hook admits verified chain");
        const auto coldOpens=backingFileOpenCalls;
        denyBackingFileOpens=true;
        Check(PublicCreativeCompat::Inspect(A(),proof) && backingFileOpenCalls==coldOpens,
            "warm combined chain verifies fresh mapped bytes without reopening either backing file");
        denyBackingFileOpens=false;
        findModuleAvailable=false;
        Check(!PublicCreativeCompat::Inspect(A(),proof), "observed loss of the reached Find module rejects admission");
        findModuleAvailable=true; denyBackingFileOpens=true;
        Check(!PublicCreativeCompat::Inspect(A(),proof), "Find reappearance requires cold backing-file verification after observed module loss");
        denyBackingFileOpens=false;
        Check(PublicCreativeCompat::Inspect(A(),proof), "Find reappearance recovers only after exact cold file verification");
        known(); Put(T()+2,relay+1);
        Check(!PublicCreativeCompat::Inspect(A(),proof), "Creative trampoline must copy the same Find jump as its saved backup");
        known(); Put(relay+16,findBase+0xaf41);
        Check(!PublicCreativeCompat::Inspect(A(),proof), "Find relay must reach exact pinned callback");
        known(); findModule[0xaf40]^=1;
        Check(!PublicCreativeCompat::Inspect(A(),proof), "modified Find callback rejected"); findModule[0xaf40]^=1;
        known(); Put(continuation+24,A()+17);
        Check(!PublicCreativeCompat::Inspect(A(),proof), "Find trampoline must resume after original full prologue");
        known(); Rel32(continuation+18,0xe9,A()+18);
        Check(PublicCreativeCompat::Inspect(A(),proof), "Find trampoline may use an exact bounded relative return");
        known(); Rel32(continuation+18,0xe9,A()+17);
        Check(!PublicCreativeCompat::Inspect(A(),proof), "Find relative return cannot resume inside the stolen prologue");
        known(); Put(continuation+1,std::uint8_t{0x90});
        Check(!PublicCreativeCompat::Inspect(A(),proof), "changed Find original renderer prefix rejected");
        known(); Put(relay+8,std::uint8_t{1});
        Check(!PublicCreativeCompat::Inspect(A(),proof), "unrecognized Find relay template rejected");
        known(); findModulePath=creativePath;
        Check(!PublicCreativeCompat::Inspect(A(),proof), "wrong Find backing file rejected"); findModulePath=findPath;
        known(); Check(PublicCreativeCompat::Inspect(A(),proof), "Find chain recovers on exact stable sample");
        Check(moduleAcquires==moduleReleases,"Find and Creative temporary module references balanced");
        PublicCreativeCompat::ForgetIdentity();
        ::VirtualFree(game,0,MEM_RELEASE); ::VirtualFree(module,0,MEM_RELEASE); ::VirtualFree(findModule,0,MEM_RELEASE);
        game=nullptr; module=nullptr; findModule=nullptr; g_exeBase=0; g_exeImageSize=0;
        std::cout<<"cases="<<cases<<" failed="<<failed<<'\n';
    }
}

int wmain(int argc, wchar_t** argv)
{
    using namespace PublicCreativeTest;
    if(argc==5 && std::wstring(argv[1])==L"--find-chain") {
        FindChainChecks(argv[2],argv[3],argv[4]); return failed ? 1 : 0;
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--hash-provider") {
        HashProviderChecks();
        return failed ? 1 : 0;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--proof-optimization") {
        ProofOptimizationChecks(argv[2]);
        return failed ? 1 : 0;
    }
    if (argc < 4 || argc > 6) { std::cerr << "usage: test GAME_EXE GOOD_MODULE_DATA WRONG_MODULE_DATA [creative214|creative300 [OTHER_GOOD_MODULE_DATA]]\n"; return 2; }
    if (argc >= 5) Require(std::wstring(argv[4]) == L"creative214" ||
        std::wstring(argv[4]) == L"creative300", "explicit fixture profile");
    if (argc >= 5 && std::wstring(argv[4]) == L"creative300") {
        CreativeImageSize = 0x5e3000; CallbackThunk = 0x4ff2; CallbackBody = 0x1d7470;
        TemplateRva = 0x44f690; SlotRva = 0x591450;
    }
    g_clientProfile = MinimapClient10::IdentifyFile(argv[1]);
    Require(g_clientProfile != MinimapClient10::Profile::Unsupported, "pinned offline game fixture identity");
    const auto gameFile = ReadFile(argv[1]); const auto publicFile = ReadFile(argv[2]);
    gameSize = ImageSize(gameFile); Require(ImageSize(publicFile) == CreativeImageSize, "public image size");
    game = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, gameSize + 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    module = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, CreativeImageSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    Require(game && module, "private NX image allocations");
    MapData(gameFile, game, gameSize); MapData(publicFile, module, CreativeImageSize);
    g_exeBase = reinterpret_cast<uintptr_t>(game); g_exeImageSize = gameSize; g_client10 = true;
    modulePath = argv[2];
    Raw(); Check(EvaluateCurrentMenuAdmission() == MenuAdmission::raw, "native standalone admission");
    Known(1); modulePath = argv[3];
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong public whole-file identity rejected");
    modulePath = std::wstring(argv[2]) + L".missing";
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "missing module file rejected");
    modulePath = argv[2];
    // Independent literals above derive from pinned emitter proof -07, not production helpers.
    Known(1); Check(IsAdmitted(EvaluateCurrentMenuAdmission()), "complete public near form admitted");
    Known(2); Check(IsAdmitted(EvaluateCurrentMenuAdmission()), "complete public absolute form admitted");
    if (argc == 6) {
        // Both files are authentic, but only one belongs to this mapped image.
        // A cached file identity must carry the same version's mapped guards.
        Known(1); modulePath = argv[5];
        Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "other authentic version cannot authenticate mapped image cold");
        Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "other authentic version cannot authenticate mapped image warm");
        modulePath = argv[2];
        Check(IsAdmitted(EvaluateCurrentMenuAdmission()), "correct version recovers after mismatched authentic file");
    }
    Known(1); moduleAvailable = false;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "unavailable reached module rejected"); moduleAvailable = true;
    Known(1); Put(B() + 0x177, B());
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "callback outside reached module rejected");
    Known(1); Put(H() + 0xa0, std::uint8_t{0});
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "disabled hook rejected");
    Known(1); Put(reinterpret_cast<uintptr_t>(module) + SlotRva, uintptr_t{0});
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "missing early-published object rejected");
    Known(1); std::memset(reinterpret_cast<void*>(H()), 0, 0xd8);
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "early publication without complete fields rejected");
    Known(1); Put(H() + 0x38, Backup() + 10);
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "short original-byte backup rejected");
    Known(1); Put(T() + 22, B() + 1);
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong near detour edge rejected");
    Known(2); Put(T() + 24, A() + 17);
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "wrong absolute continuation rejected");
    Known(2); Put(Backup() + 17, std::uint8_t{1});
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "last original18 byte mutation rejected");
    Known(1); Put(B() + 0x10, std::uint8_t{0});
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "immutable callback template mutation rejected");
    // Same pinned FILE/path, independently corrupted mapped-image bytes. A
    // comparison of B to the mapped template alone cannot establish identity.
    const auto templateByte = module[TemplateRva + 0x10];
    Known(1); module[TemplateRva + 0x10] ^= 1;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped module template rejected with pinned disk identity");
    module[TemplateRva + 0x10] = templateByte;
    Known(1); module[TemplateRva + 0x10] ^= 1;
    *reinterpret_cast<std::uint8_t*>(B() + 0x10) ^= 1;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "identically corrupted mapped and copied template rejected");
    module[TemplateRva + 0x10] = templateByte;
    const auto thunkByte = module[CallbackThunk + 1];
    Known(1); module[CallbackThunk + 1] ^= 1;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped callback thunk rejected with pinned disk identity");
    module[CallbackThunk + 1] = thunkByte;
    const auto bodyByte = module[CallbackBody];
    Known(1); module[CallbackBody] ^= 1;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "changed mapped callback body rejected with pinned disk identity");
    module[CallbackBody] = bodyByte;
    Known(1); denyBlock = true;
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "unreadable block protection rejected"); denyBlock = false;
    Known(1); Put(B() + 0x17f, UINTPTR_MAX - uintptr_t{4});
    Check(!IsAdmitted(EvaluateCurrentMenuAdmission()), "overflowing continuation pointer rejected");
    Raw(); Put(A(), std::uint8_t{0xe9});
    Check(EvaluateCurrentMenuAdmission() == MenuAdmission::rejected,
        "unknown renderer owner rejected without a private provider");
    Raw();
    // Authoritative native UI chain resides entirely in private inert buffers.
    Put(g_exeBase + ClientRva(MinimapMenu10::UiContextRva), Arena() + 0x6000);
    Put(Arena() + 0x6000 + MinimapMenu10::HolderOffset, Arena() + 0x7000);
    Put(Arena() + 0x7000, Arena() + 0x7800);
    Put(Arena() + 0x7800 + MinimapMenu10::PlayStateOffset, Arena() + 0x8000);
    g_originalMenuRenderer.store(A());
    auto* retained = RetainCall();
    Require(IsRetainedCurrentMenuCallsiteOwned(retained), "real retained relay/native target proof");
    if (GetEnvironmentVariableW(L"MM_COMPAT_COST_PROBE", nullptr, 0)) {
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        Known(1); ResetCapture(); MenuOwnershipSample warm{};
        Require(SampleCurrentMenuOwnership(warm,true), "warm exact creative ownership");
        auto profile = [&](const char* label, auto operation) {
            queryTicks=queryCount=queryMaxTicks=0; measureQuery=true;
            const auto start=Clock(); bool ok=true;
            for(int i=0;i<200;++i) ok=operation() && ok;
            const auto elapsed=Clock()-start; measureQuery=false;
            std::cout << "COST " << label << " ok=" << ok << " iterations=200 total_ms=" << double(elapsed)*1000/frequency.QuadPart
                      << " query_ms=" << double(queryTicks)*1000/frequency.QuadPart << " queries=" << queryCount
                      << " query_max_ms=" << double(queryMaxTicks)*1000/frequency.QuadPart << '\n';
            Require(ok,"profile operation success");
        };
        profile("ownership",[&] { MenuOwnershipSample x{}; return SampleCurrentMenuOwnership(x,true); });
        profile("hud-chain",[&] { std::uint8_t screen=0; return TryReadCurrentHudScreenType(screen); });
        profile("retained-call",[&] { return IsRetainedCurrentMenuCallsiteOwned(retained); });
        profile("wrapper",[&] { CaptureMenuVisibilityCallsiteHook(); return g_menuVisibilitySnapshot.AllowsGameplay(GetTickCount64()); });
        DestroyCall(retained); VirtualFree(game,0,MEM_RELEASE); VirtualFree(module,0,MEM_RELEASE); return 0;
    }
    AggregateTimingCallbackCase();
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
    Raw(); CaptureCase("failed post context read retires candidate", false, [] { Put(g_exeBase + ClientRva(MinimapMenu10::UiContextRva), uintptr_t{0}); });
    Put(g_exeBase + ClientRva(MinimapMenu10::UiContextRva), Arena() + 0x6000);
    PriorGameplayOverlapCase("prior gameplay stays visible during raw forwarding without premature publication", false);
    PriorGameplayOverlapCase("prior gameplay stays visible during public forwarding without premature publication", true);
    InvalidPreAdmissionClearsPriorCase();
    NonGameplayPreScreenClearsPriorCase();
    FailedAfterSampleClearsPriorCase();
    RealSehCopyCase();
    Raw(); const auto tail = g_exeBase + ClientRva(RVA_MENU_RENDERER_CALL_1_0) + 5;
    const std::uint8_t originalTail = *reinterpret_cast<std::uint8_t*>(tail);
    Put(tail, std::uint8_t{0x90}); CaptureCase("following native CALL remains guarded", false); Put(tail, originalTail);
    Raw(); const auto caller = g_exeBase + ClientRva(0xbc0446);
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
