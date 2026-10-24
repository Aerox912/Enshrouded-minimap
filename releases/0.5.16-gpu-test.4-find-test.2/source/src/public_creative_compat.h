#pragma once
// Narrow content sampling for exact public Creative146 v2.1.4 and v3.0.0 images.
// No Creative function is called and no foreign pointer survives a sample.
#include <windows.h>
#include <psapi.h>
#include <bcrypt.h>
#include "aggregate_timing.h"
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <memory>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace PublicCreativeCompat
{
    using Digest = std::array<unsigned char, 32>;
    enum class ProofOrigin : std::uint8_t { CallbackPre, CallbackPost, AdmissionUpdate, Other };

    inline MinimapAggregateTiming::Metric ProofMetric(ProofOrigin origin) noexcept
    {
        using MinimapAggregateTiming::Metric;
        switch (origin)
        {
        case ProofOrigin::CallbackPre: return Metric::CreativeProofCallbackPre;
        case ProofOrigin::CallbackPost: return Metric::CreativeProofCallbackPost;
        case ProofOrigin::AdmissionUpdate: return Metric::CreativeProofAdmissionUpdate;
        default: return Metric::CreativeProofOther;
        }
    }

    inline bool ReadProofQpc(std::uint64_t& ticks) noexcept
    {
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value) || value.QuadPart < 0) return false;
        ticks = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }
    struct Profile
    {
        std::size_t imageSize, fileSize;
        std::uintptr_t callback, body, pattern, slot;
        std::size_t bodySize, objectSize;
        const char* fileHash;
        const char* bodyHash;
        std::array<unsigned char, 5> thunk;
    };
    // Owned immutable descriptors. Never choose a profile from mutable mapped
    // bytes or a version label; the backing file must match its entire hash.
    constexpr Profile Creative214{
        0x555000, 5558784, 0x4877, 0x18b350, 0x3eb7e0, 0x514ba8, 0x13e, 0x158b,
        "dbc26a853675f741d6cd0d4676c8f0424c916d9a18c7ed2c262d38716d98b136",
        "78b589001b205f97d5ecd73174deccdfd392c12aa2d75eb96a2958e30b3b5d9d",
        {0xe9,0xd4,0x6a,0x18,0}};
    constexpr Profile Creative300{
        0x5e3000, 6097920, 0x4ff2, 0x1d7470, 0x44f690, 0x591450, 0x103, 0x160,
        "c860dae6b1b44728dff09771ab5b60fc16ade7f7075d9f6411cd24b56fdc88f8",
        "d6cc956a4deea1bbd9f8f32485eb4abcd0da500e4da53bbd0455b1f3acbfb42f",
        {0xe9,0x79,0x24,0x1d,0}};
    // Original Find 1.1.2, supplied through the existing Greeneye loader.
    // Only its reached HUD callback and original-renderer continuation are used.
    constexpr Profile Find112{
        0x106000, 472576, 0xaf40, 0xaf40, 0, 0x6f680, 0x5e, 0,
        "82ba715080195ac8aeceac80f341cb4466991278e21b8a24ebb836a3f2be18c5",
        "f21f58b078a534fd0488d4386293329754d9175345505866829d6c3fd78cb028",
        {}};
    constexpr std::size_t BodySize = 0x13e, BlockSize = 0x187;
    static_assert(Creative214.bodySize <= BodySize && Creative300.bodySize <= BodySize);
    constexpr char TemplateHash[] = "194e3705b88d175140ae903807da6a5955b43c0ced7388172ab7d6e0be06c69c";
    constexpr unsigned char Original[18] = {0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec,0x10,0x03,0,0};

    inline bool Range(std::uintptr_t address, std::size_t size)
    {
        constexpr std::uintptr_t last = 0x00007fffffffffff;
        return address >= 0x10000 && size && address <= last && size - 1 <= last - address;
    }
    inline bool ReadableProtection(DWORD value, bool code)
    {
        if (value & (PAGE_GUARD | PAGE_NOACCESS)) return false;
        const DWORD protection = value & 0xff;
        const bool executable = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
        return (executable || protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY) && (!code || executable);
    }

    enum class PageReadability { Unknown, Readable, Rejected };
    inline DWORD SystemPageSize()
    {
        // Only immutable OS geometry is cached, never foreign memory metadata.
        static const DWORD size = []() noexcept {
            SYSTEM_INFO info{}; GetSystemInfo(&info); return info.dwPageSize;
        }();
        return size;
    }
    inline PageReadability ResidentPageReadability(std::uintptr_t address, std::size_t size, bool code)
    {
        const auto pageSize = SystemPageSize();
        // Copy is bounded to 4096 bytes, so supported page sizes need at most two entries.
        if (pageSize < 4096 || (pageSize & (pageSize - 1))) return PageReadability::Unknown;
        const auto mask = static_cast<std::uintptr_t>(pageSize) - 1;
        const auto first = address & ~mask;
        const auto last = (address + size - 1) & ~mask;
        PSAPI_WORKING_SET_EX_INFORMATION pages[2]{};
        pages[0].VirtualAddress = reinterpret_cast<void*>(first);
        pages[1].VirtualAddress = reinterpret_cast<void*>(last);
        const DWORD count = first == last ? 1u : 2u;
        if (!K32QueryWorkingSetEx(GetCurrentProcess(), pages, count * sizeof(pages[0])))
            return PageReadability::Unknown;
        // Invalid/nonresident entries have no usable protection fields. Fall back
        // for the ENTIRE copy rather than accepting a partially described span.
        for (DWORD i = 0; i < count; ++i)
            if (!pages[i].VirtualAttributes.Valid) return PageReadability::Unknown;
        for (DWORD i = 0; i < count; ++i)
            if (!ReadableProtection(static_cast<DWORD>(pages[i].VirtualAttributes.Win32Protection), code))
                return PageReadability::Rejected;
        return PageReadability::Readable;
    }

    // POD-only SEH leaf: fresh page protections avoid VirtualQuery's scan to the
    // end of large resident regions. Nonresident/failed page queries retain the
    // original region validation. Neither API reserves the allocation; every
    // copy still contains faults and no result survives this call.
    inline bool Copy(std::uintptr_t address, void* output, std::size_t size, bool code = false)
    {
        if (!output || size > 4096 || !Range(address, size)) return false;
        const auto resident = ResidentPageReadability(address, size, code);
        if (resident == PageReadability::Rejected) return false;
        if (resident == PageReadability::Unknown) {
            std::uintptr_t current = address, end = address + size;
            while (current < end) {
                MEMORY_BASIC_INFORMATION info{};
                if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) != sizeof(info) ||
                    info.State != MEM_COMMIT || !ReadableProtection(info.Protect, code)) return false;
                const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                if (begin > current || !info.RegionSize || begin > UINTPTR_MAX - info.RegionSize ||
                    begin + info.RegionSize <= current) return false;
                current = (begin + info.RegionSize < end) ? begin + info.RegionSize : end;
            }
        }
        __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool GuardCopy(const std::uint8_t* from, void* to, std::size_t size)
    { return Copy(reinterpret_cast<std::uintptr_t>(from), to, size, true); }

    class Sha256Provider final
    {
        std::mutex mutex;
        BCRYPT_ALG_HANDLE algorithm = nullptr;
    public:
        Sha256Provider() = default;
        Sha256Provider(const Sha256Provider&) = delete;
        Sha256Provider& operator=(const Sha256Provider&) = delete;

        ~Sha256Provider()
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }

        bool CreateHash(BCRYPT_HASH_HANDLE* hash)
        {
            if (!hash) return false;
            *hash = nullptr;
            std::lock_guard<std::mutex> lock(mutex);
            if (!algorithm)
            {
                BCRYPT_ALG_HANDLE candidate = nullptr;
                const NTSTATUS opened = BCryptOpenAlgorithmProvider(
                    &candidate, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
                if (opened < 0 || !candidate)
                {
                    if (candidate)
                        BCryptCloseAlgorithmProvider(candidate, 0);
                    return false; // A later call may retry a transient open failure.
                }
                algorithm = candidate;
            }

            const NTSTATUS created = BCryptCreateHash(algorithm, hash, nullptr, 0, nullptr, 0, 0);
            if (created < 0 || !*hash)
            {
                if (*hash)
                    BCryptDestroyHash(*hash);
                *hash = nullptr;
                return false;
            }
            return true;
        }
    };

    inline std::shared_ptr<Sha256Provider> OwnedSha256Provider()
    {
        static const std::shared_ptr<Sha256Provider> provider = std::make_shared<Sha256Provider>();
        return provider;
    }

    class Hash
    {
        std::shared_ptr<Sha256Provider> provider;
        BCRYPT_HASH_HANDLE hash = nullptr;
        bool valid = false;
    public:
        Hash()
        {
            try
            {
                provider = OwnedSha256Provider();
                valid = provider && provider->CreateHash(&hash);
            }
            catch (...)
            {
                valid = false;
            }
        }
        ~Hash() { if (hash) BCryptDestroyHash(hash); }
        Hash(const Hash&) = delete;
        Hash& operator=(const Hash&) = delete;
        bool Add(const void* data, std::size_t size)
        {
            if (!valid || !data || size > ULONG_MAX) return valid = false;
            return valid = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
                static_cast<ULONG>(size), 0) >= 0;
        }
        bool Finish(Digest& value)
        { return valid && BCryptFinishHash(hash, value.data(), static_cast<ULONG>(value.size()), 0) >= 0; }
    };    inline bool HexEquals(const Digest& digest, const char* hex)
    {
        constexpr char alphabet[] = "0123456789abcdef";
        for (std::size_t i = 0; i < digest.size(); ++i)
            if (hex[i * 2] != alphabet[digest[i] >> 4] || hex[i * 2 + 1] != alphabet[digest[i] & 15]) return false;
        return true;
    }
    inline bool HasHash(const void* bytes, std::size_t size, const char* expected)
    { Digest digest{}; Hash hash; return hash.Add(bytes, size) && hash.Finish(digest) && HexEquals(digest, expected); }

    // Cache only owned file metadata/path and an opaque digest of the image
    // identity. No HMODULE, callable, object, allocation or raw foreign address
    // is retained. File bytes are hashed on identity change, never per frame.
    struct FileCache
    {
        std::mutex mutex;
        std::wstring path;
        BY_HANDLE_FILE_INFORMATION info{};
        Digest imageIdentity{};
        bool trusted = false;
        const Profile* profile = nullptr; // points only to an owned constexpr descriptor
    };
    inline FileCache fileCache;
    inline FileCache findFileCache;
    inline void ForgetFindIdentity()
    {
        std::lock_guard<std::mutex> lock(findFileCache.mutex);
        findFileCache.trusted = false; findFileCache.profile = nullptr;
        findFileCache.path.clear(); findFileCache.imageIdentity = {};
    }
    inline void ForgetIdentity()
    {
        auto& collector = MinimapAggregateTiming::DiagnosticCollector();
        MinimapAggregateTiming::ScopedTimer acquisition(collector, MinimapAggregateTiming::Metric::FileCacheMutexAcquire);
        std::lock_guard<std::mutex> lock(fileCache.mutex);
        acquisition.Stop();
        MinimapAggregateTiming::ScopedTimer hold(collector, MinimapAggregateTiming::Metric::FileCacheMutexHold);
        fileCache.trusted = false; fileCache.profile = nullptr; fileCache.path.clear(); fileCache.imageIdentity = {};
        ForgetFindIdentity();
    }
    inline bool SameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
    {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
            a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh &&
            a.nFileSizeLow == b.nFileSizeLow && a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime &&
            a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime;
    }
    inline bool FileIdentity(HMODULE module, Digest& imageIdentity, const Profile*& profile,
        FileCache& identityCache = fileCache, const Profile* requiredProfile = nullptr)
    {
        auto& fileCache = identityCache;
        profile = nullptr;
        // This local address is hashed only as an opaque cache discriminator.
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        Hash identity;
        if (!identity.Add(&base, sizeof(base)) || !identity.Finish(imageIdentity)) return false;
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        if (!length || length >= std::size(path)) return false;
        auto& collector = MinimapAggregateTiming::DiagnosticCollector();
        MinimapAggregateTiming::ScopedTimer acquisition(collector, MinimapAggregateTiming::Metric::FileCacheMutexAcquire);
        std::lock_guard<std::mutex> lock(fileCache.mutex);
        acquisition.Stop();
        MinimapAggregateTiming::ScopedTimer hold(collector, MinimapAggregateTiming::Metric::FileCacheMutexHold);
        struct ColdOutcome
        {
            MinimapAggregateTiming::Collector& collector;
            bool started = false, success = false;
            ~ColdOutcome() noexcept
            {
                if (!started) return;
                collector.Count(success ? MinimapAggregateTiming::Metric::ProofColdSuccess :
                    MinimapAggregateTiming::Metric::ProofColdFailure);
            }
        } outcome{collector};
        // Reuse only this observed image identity/path. Image loss, a different
        // reached image or session invalidation forces cold verification. The
        // mapped content is independently checked on EVERY sample. Unobserved
        // same-base unload/reload ABA is outside the stable-startup contract.
        if (fileCache.trusted && fileCache.path == path && fileCache.imageIdentity == imageIdentity)
        {
            collector.Count(MinimapAggregateTiming::Metric::ProofCacheWarmHit);
            profile = fileCache.profile;
            return profile != nullptr && (!requiredProfile || profile == requiredProfile);
        }
        collector.Count(MinimapAggregateTiming::Metric::ProofCacheColdAttempt);
        outcome.started = true;
        fileCache.trusted = false; fileCache.profile = nullptr;
        collector.Count(MinimapAggregateTiming::Metric::BackingFileOpenAttempt);
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            collector.Count(MinimapAggregateTiming::Metric::BackingFileOpenFailure);
            return false;
        }
        collector.Count(MinimapAggregateTiming::Metric::BackingFileOpenSuccess);
        struct FileReference { HANDLE value; ~FileReference() { CloseHandle(value); } } reference{file};
        BY_HANDLE_FILE_INFORMATION before{}, after{};
        if (!GetFileInformationByHandle(file, &before) || before.nFileSizeHigh) return false;
        // Size chooses only a bounded read budget. The full digest below is
        // mandatory before this descriptor is published to the caller/cache.
        const Profile* candidate = requiredProfile ?
            (before.nFileSizeLow == requiredProfile->fileSize ? requiredProfile : nullptr) :
            before.nFileSizeLow == Creative214.fileSize ? &Creative214 :
            before.nFileSizeLow == Creative300.fileSize ? &Creative300 : nullptr;
        if (!candidate) return false;
        const auto fileSize = candidate->fileSize;
        Hash hash;
        std::array<unsigned char, 65536> bytes{};
        std::size_t total = 0;
        while (total < fileSize) {
            DWORD read = 0;
            const DWORD count = static_cast<DWORD>((fileSize - total < bytes.size()) ? fileSize - total : bytes.size());
            std::uint64_t readStart = 0, readEnd = 0;
            const bool startValid = ReadProofQpc(readStart);
            const BOOL readOk = ReadFile(file, bytes.data(), count, &read, nullptr);
            const bool endValid = ReadProofQpc(readEnd);
            if (startValid && endValid && readEnd >= readStart)
                collector.Record(MinimapAggregateTiming::Metric::BackingFileRead, readEnd - readStart, read);
            else
                collector.RecordTimingFailure(MinimapAggregateTiming::Metric::BackingFileRead);
            if (!readOk || read != count || !hash.Add(bytes.data(), read)) return false;
            total += read;
        }
        Digest digest{};
        if (!hash.Finish(digest) || !HexEquals(digest, candidate->fileHash) ||
            !GetFileInformationByHandle(file, &after) || !SameFile(before, after)) return false;
        fileCache.path = path; fileCache.info = after; fileCache.imageIdentity = imageIdentity;
        fileCache.profile = candidate; fileCache.trusted = true; profile = candidate;
        outcome.success = true;
        return true;
    }
    template<class T> inline T Word(const unsigned char* bytes, std::size_t offset)
    { T value{}; std::memcpy(&value, bytes + offset, sizeof(value)); return value; }
    inline bool Relative(std::uintptr_t at, std::int32_t displacement, std::uintptr_t& result)
    {
        if (!Range(at, 5)) return false;
        const auto value = static_cast<std::int64_t>(at + 5) + displacement;
        if (value < 0x10000 || value > 0x00007fffffffffff) return false;
        result = static_cast<std::uintptr_t>(value); return true;
    }
    struct Chain
    {
        std::uintptr_t target = 0, block = 0, trampoline = 0, callback = 0;
        unsigned kind = 0;
        std::size_t stolen = 0, length = 0;
        std::array<unsigned char,18> entry{}, backup{};
        std::array<unsigned char,32> trampolineBytes{};
        std::array<unsigned char,BlockSize> blockBytes{};
        std::array<unsigned char,0xd8> model{};
        std::uintptr_t object = 0;
        bool findOwned = false;
        Digest underlyingDigest{};
    };
    namespace Detail
    {
        inline bool ReadChainSnapshot(std::uintptr_t entry, Chain& value)
        {
            value = {}; value.target = entry;
            if (!Copy(entry, value.entry.data(), value.entry.size(), true)) return false;
            constexpr unsigned char indirect[6] = {0xff,0x25,0,0,0,0};
            if (value.entry[0] == 0xe9) {
                std::uintptr_t relay = 0;
                if (!Relative(entry, Word<std::int32_t>(value.entry.data(), 1), relay) || relay < 0x10010) return false;
                // Creative steals the complete first instruction of the renderer
                // it encounters. Find's MOV RAX,imm64 is ten bytes; the original
                // renderer's first complete stolen sequence is eleven bytes.
                const bool overFind = value.entry[10] == 0xff && value.entry[11] == 0xe0;
                value.kind = 1; value.stolen = overFind ? 10 : 11;
                value.length = value.stolen + 19;
                if (relay < value.stolen + 5) return false;
                value.trampoline = relay - (value.stolen + 5);
                if (!Copy(value.trampoline, value.trampolineBytes.data(), value.length, true)) return false;
                value.block = Word<std::uintptr_t>(value.trampolineBytes.data(), value.stolen + 11);
                for (std::size_t i = 5; i < value.stolen; ++i) if (value.entry[i] != 0x90) return false;
                if (overFind) {
                    for (std::size_t i = 12; i < 18; ++i) if (value.entry[i] != 0x90) return false;
                } else if (std::memcmp(value.entry.data() + 11, Original + 11, 7)) return false;
                if (std::memcmp(value.trampolineBytes.data() + value.stolen + 5, indirect, 6)) return false;
                std::uintptr_t resume = 0;
                if (value.trampolineBytes[value.stolen] != 0xe9 || !Relative(value.trampoline + value.stolen,
                    Word<std::int32_t>(value.trampolineBytes.data(), value.stolen + 1), resume) ||
                    resume != entry + value.stolen) return false;
            } else if (!std::memcmp(value.entry.data(), indirect, 6)) {
                value.kind = 2; value.stolen = 18; value.length = 32;
                value.block = Word<std::uintptr_t>(value.entry.data(), 6);
                for (std::size_t i = 14; i < 18; ++i) if (value.entry[i] != 0x90) return false;
                if (!Copy(value.block, value.blockBytes.data(), BlockSize, true)) return false;
                value.trampoline = Word<std::uintptr_t>(value.blockBytes.data(), 0x17f);
                if (!Copy(value.trampoline, value.trampolineBytes.data(), value.length, true) ||
                    std::memcmp(value.trampolineBytes.data() + 18, indirect, 6) ||
                    Word<std::uintptr_t>(value.trampolineBytes.data(), 24) != entry + 18) return false;
            } else return false;
            if (!Copy(value.block, value.blockBytes.data(), BlockSize, true) ||
                Word<std::uintptr_t>(value.blockBytes.data(), 0x17f) != value.trampoline) return false;
            // Ten-byte continuations are admitted only by the separate complete
            // Find proof below. All existing raw forms retain their exact prefix.
            if (value.stolen != 10 && std::memcmp(value.trampolineBytes.data(), Original, value.stolen)) return false;
            value.callback = Word<std::uintptr_t>(value.blockBytes.data(), 0x177);
            return true;
        }
    }
    inline bool ReadChain(std::uintptr_t entry, Chain& value)
    {
        if (!Detail::ReadChainSnapshot(entry, value)) return false;
        auto normalized = value.blockBytes;
        std::memset(normalized.data() + 0x177, 0, 16);
        return HasHash(normalized.data(), normalized.size(), TemplateHash);
    }
    inline bool VerifyFindPrefix(const unsigned char (&prefix)[18], std::uintptr_t renderer, Digest& proof)
    {
        proof = {};
        if (prefix[0] != 0x48 || prefix[1] != 0xb8 || prefix[10] != 0xff || prefix[11] != 0xe0)
            return false;
        for (std::size_t i=12; i<18; ++i) if (prefix[i] != 0x90) return false;
        const auto relayAddress = Word<std::uintptr_t>(prefix,2);
        std::array<unsigned char,24> relay{};
        constexpr unsigned char indirect[6]={0xff,0x25,10,0,0,0};
        if (!Copy(relayAddress,relay.data(),relay.size(),true) ||
            std::memcmp(relay.data(),indirect,sizeof(indirect))) return false;
        for (std::size_t i=6; i<16; ++i) if (relay[i] != 0) return false;
        const auto callback = Word<std::uintptr_t>(relay.data(),16);
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(callback),&module)) { ForgetFindIdentity(); return false; }
        struct ModuleReference { HMODULE value; ~ModuleReference(){FreeLibrary(value);} } reference{module};
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        Digest identity{}; const Profile* profile = nullptr;
        if (!Range(base,Find112.imageSize) || callback != base+Find112.callback ||
            !FileIdentity(module,identity,profile,findFileCache,&Find112) || profile != &Find112) return false;
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        if (!Copy(base,&dos,sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 ||
            static_cast<std::size_t>(dos.e_lfanew)>4096-sizeof(nt) || !Copy(base+dos.e_lfanew,&nt,sizeof(nt)) ||
            nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage!=Find112.imageSize)
            return false;
        std::array<unsigned char,0x5e> body{};
        std::uintptr_t continuation=0;
        std::array<unsigned char,32> original{};
        if (!Copy(callback,body.data(),body.size(),true) || !HasHash(body.data(),body.size(),Find112.bodyHash) ||
            !Copy(base+Find112.slot,&continuation,sizeof(continuation)) ||
            !Copy(continuation,original.data(),original.size(),true) ||
            std::memcmp(original.data(),Original,sizeof(Original))) return false;
        constexpr unsigned char returnJump[6]={0xff,0x25,0,0,0,0};
        std::uintptr_t resume=0;
        if (!std::memcmp(original.data()+18,returnJump,sizeof(returnJump)))
            resume=Word<std::uintptr_t>(original.data(),24);
        else if (original[18]==0xe9) {
            if (!Relative(continuation+18,Word<std::int32_t>(original.data(),19),resume)) return false;
        } else return false;
        if (resume != renderer+18) return false;
        Hash hash;
        return hash.Add(identity.data(),identity.size()) && hash.Add(prefix,sizeof(prefix)) &&
            hash.Add(relay.data(),relay.size()) && hash.Add(body.data(),body.size()) &&
            hash.Add(&continuation,sizeof(continuation)) && hash.Add(original.data(),original.size()) && hash.Finish(proof);
    }
    inline bool ReadModel(std::uintptr_t module, Chain& value, const Profile& profile)
    {
        if (!Range(module, profile.imageSize) || !Copy(module + profile.slot, &value.object, sizeof(value.object)) ||
            (value.object & 7) || !Range(value.object, profile.objectSize) ||
            !Copy(value.object + 0x80, value.model.data(), value.model.size())) return false;
        const auto* m = value.model.data();
        const auto get = [m](std::size_t at) { return Word<std::uintptr_t>(m, at); };
        const auto backup = get(0x30);
        if (get(0) != value.target || get(8) != value.block || !Range(get(0x10), 1) || !Range(get(0x18), 1) ||
            get(0x20) != value.trampoline || get(0x28) != value.length ||
            !Range(backup, value.stolen) || get(0x38) != backup + value.stolen || get(0x40) < get(0x38) ||
            get(0x40) - backup > 64 || get(0x48) != value.length || m[0xa0] != 1 ||
            Word<unsigned>(m, 0xa4) != value.kind || get(0xa8) != value.target ||
            !Range(get(0xb0), 1) || !Range(get(0xb8), 1) || get(0xc0) != value.block ||
            get(0xc8) != BlockSize || get(0xd0) != value.callback ||
            !Copy(backup, value.backup.data(), value.stolen)) return false;
        if (std::memcmp(value.trampolineBytes.data(),value.backup.data(),value.stolen)) return false;
        unsigned char full[18]{};
        std::memcpy(full, value.backup.data(), value.stolen);
        if (value.stolen < 18) std::memcpy(full + value.stolen, value.entry.data() + value.stolen, 18 - value.stolen);
        value.findOwned = false; value.underlyingDigest = {};
        if (!std::memcmp(full, Original, 18)) return true;
        if (!VerifyFindPrefix(full,value.target,value.underlyingDigest)) return false;
        value.findOwned = true;
        return true;
    }
    struct ImageContent
    {
        std::array<unsigned char,BlockSize> pattern{};
        std::array<unsigned char,BodySize> body{};
        std::array<unsigned char,5> thunk{};
    };
    namespace Detail
    {
        inline bool ReadImageSnapshot(std::uintptr_t base, std::uintptr_t callback,
            ImageContent& value, const Profile& profile)
        {
            IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
            if (!Range(base, profile.imageSize) || callback != base + profile.callback ||
                !Copy(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
                dos.e_lfanew <= 0 || static_cast<std::size_t>(dos.e_lfanew) > 4096 - sizeof(nt) ||
                !Copy(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
                nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
                nt.OptionalHeader.SizeOfImage != profile.imageSize ||
                !Copy(base + profile.pattern, value.pattern.data(), value.pattern.size()) ||
                !Copy(base + profile.body, value.body.data(), profile.bodySize, true) ||
                !Copy(base + profile.callback, value.thunk.data(), value.thunk.size(), true)) return false;
            return !std::memcmp(value.thunk.data(), profile.thunk.data(), profile.thunk.size());
        }
    }
    inline bool ReadImage(std::uintptr_t base, std::uintptr_t callback, ImageContent& value, const Profile& profile)
    {
        if (!Detail::ReadImageSnapshot(base, callback, value, profile)) return false;
        // These pinned ranges have no PE base relocations; no mutable mapped
        // template is allowed to serve as its own identity anchor.
        return HasHash(value.pattern.data(), value.pattern.size(), TemplateHash) &&
            HasHash(value.body.data(), profile.bodySize, profile.bodyHash);
    }
    struct Proof { Digest digest{}; unsigned kind = 0; };
    inline bool Inspect(std::uintptr_t entry, Proof& proof, ProofOrigin origin = ProofOrigin::Other)
    {
        MinimapAggregateTiming::ScopedTimer proofTimer(
            MinimapAggregateTiming::DiagnosticCollector(), ProofMetric(origin));
        proof = {};
        try {
            Chain first{}, last{};
            if (!ReadChain(entry, first)) return false;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(first.callback), &module)) { ForgetIdentity(); return false; }
            struct ModuleReference { HMODULE value; ~ModuleReference() { FreeLibrary(value); } } reference{module};
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            Digest identity{}; ImageContent imageBefore{}, imageAfter{};
            const Profile* profile = nullptr;
            if (!FileIdentity(module, identity, profile) || !profile || !ReadImage(base, first.callback, imageBefore, *profile) ||
                !ReadModel(base, first, *profile) || !Detail::ReadChainSnapshot(entry, last) ||
                !ReadModel(base, last, *profile) || !Detail::ReadImageSnapshot(base, last.callback, imageAfter, *profile) ||
                first.kind != last.kind || first.object != last.object || first.block != last.block ||
                first.trampoline != last.trampoline || first.callback != last.callback ||
                first.entry != last.entry || first.backup != last.backup || first.model != last.model ||
                first.trampolineBytes != last.trampolineBytes || first.blockBytes != last.blockBytes ||
                first.findOwned != last.findOwned || first.underlyingDigest != last.underlyingDigest ||
                imageBefore.pattern != imageAfter.pattern || imageBefore.body != imageAfter.body ||
                imageBefore.thunk != imageAfter.thunk) return false;
            Hash hash;
            if (!hash.Add(identity.data(), identity.size()) || !hash.Add(&first.object, sizeof(first.object)) ||
                !hash.Add(first.entry.data(), first.entry.size()) || !hash.Add(first.model.data(), first.model.size()) ||
                !hash.Add(first.backup.data(), first.backup.size()) ||
                !hash.Add(first.trampolineBytes.data(), first.trampolineBytes.size()) ||
                !hash.Add(first.blockBytes.data(), first.blockBytes.size()) ||
                (first.findOwned && !hash.Add(first.underlyingDigest.data(),first.underlyingDigest.size())) ||
                !hash.Finish(proof.digest)) return false;
            proof.kind = first.kind; return true;
        } catch (...) { proof = {}; return false; }
    }
}
