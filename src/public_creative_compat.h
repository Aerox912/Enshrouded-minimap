#pragma once
// Narrow content sampling for the unchanged public Creative146 v2.1.4 image.
// No Creative function is called and no foreign pointer survives a sample.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace PublicCreativeCompat
{
    using Digest = std::array<unsigned char, 32>;
    constexpr std::size_t ImageSize = 0x555000;
    constexpr std::size_t FileSize = 5558784;
    constexpr std::uintptr_t Callback = 0x4877, Body = 0x18b350, Template = 0x3eb7e0, Slot = 0x514ba8;
    constexpr std::size_t BodySize = 0x13e, BlockSize = 0x187;
    constexpr char FileHash[] = "dbc26a853675f741d6cd0d4676c8f0424c916d9a18c7ed2c262d38716d98b136";
    constexpr char TemplateHash[] = "194e3705b88d175140ae903807da6a5955b43c0ced7388172ab7d6e0be06c69c";
    constexpr char BodyHash[] = "78b589001b205f97d5ecd73174deccdfd392c12aa2d75eb96a2958e30b3b5d9d";
    constexpr unsigned char Original[18] = {0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfd,0xff,0xff,0x48,0x81,0xec,0x10,0x03,0,0};

    inline bool Range(std::uintptr_t address, std::size_t size)
    {
        constexpr std::uintptr_t last = 0x00007fffffffffff;
        return address >= 0x10000 && size && address <= last && size - 1 <= last - address;
    }
    // POD-only SEH leaf: no object unwinding inside __try. Protection checks do
    // not reserve a foreign allocation; the copy must still contain faults.
    inline bool Copy(std::uintptr_t address, void* output, std::size_t size, bool code = false)
    {
        if (!output || size > 4096 || !Range(address, size)) return false;
        std::uintptr_t current = address, end = address + size;
        while (current < end) {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const DWORD protection = info.Protect & 0xff;
            const bool executable = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                protection == PAGE_EXECUTE_WRITECOPY;
            if ((!executable && protection != PAGE_READONLY && protection != PAGE_READWRITE &&
                protection != PAGE_WRITECOPY) || (code && !executable)) return false;
            const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if (begin > current || !info.RegionSize || begin > UINTPTR_MAX - info.RegionSize ||
                begin + info.RegionSize <= current) return false;
            current = (begin + info.RegionSize < end) ? begin + info.RegionSize : end;
        }
        __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    inline bool GuardCopy(const std::uint8_t* from, void* to, std::size_t size)
    { return Copy(reinterpret_cast<std::uintptr_t>(from), to, size, true); }

    class Hash
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        bool valid = false;
    public:
        Hash() { valid = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0; }
        ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
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
    };
    inline bool HexEquals(const Digest& digest, const char* hex)
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
    };
    inline FileCache fileCache;
    inline void ForgetIdentity()
    {
        std::lock_guard<std::mutex> lock(fileCache.mutex);
        fileCache.trusted = false; fileCache.path.clear(); fileCache.imageIdentity = {};
    }
    inline bool SameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b)
    {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
            a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh &&
            a.nFileSizeLow == b.nFileSizeLow && a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime &&
            a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime;
    }
    inline bool FileIdentity(HMODULE module, Digest& imageIdentity)
    {
        // This local address is hashed only as an opaque cache discriminator.
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        Hash identity;
        if (!identity.Add(&base, sizeof(base)) || !identity.Finish(imageIdentity)) return false;
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        if (!length || length >= std::size(path)) return false;
        std::lock_guard<std::mutex> lock(fileCache.mutex);
        // Reuse only this observed image identity/path. Image loss, a different
        // reached image or session invalidation forces cold verification. The
        // mapped content is independently checked on EVERY sample. Unobserved
        // same-base unload/reload ABA is outside the stable-startup contract.
        if (fileCache.trusted && fileCache.path == path && fileCache.imageIdentity == imageIdentity) return true;
        fileCache.trusted = false;
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        struct FileReference { HANDLE value; ~FileReference() { CloseHandle(value); } } reference{file};
        BY_HANDLE_FILE_INFORMATION before{}, after{};
        if (!GetFileInformationByHandle(file, &before) || before.nFileSizeHigh || before.nFileSizeLow != FileSize)
            return false;
        Hash hash;
        std::array<unsigned char, 65536> bytes{};
        std::size_t total = 0;
        while (total < FileSize) {
            DWORD read = 0;
            const DWORD count = static_cast<DWORD>((FileSize - total < bytes.size()) ? FileSize - total : bytes.size());
            if (!ReadFile(file, bytes.data(), count, &read, nullptr) || read != count || !hash.Add(bytes.data(), read)) return false;
            total += read;
        }
        Digest digest{};
        if (!hash.Finish(digest) || !HexEquals(digest, FileHash) ||
            !GetFileInformationByHandle(file, &after) || !SameFile(before, after)) return false;
        fileCache.path = path; fileCache.info = after; fileCache.imageIdentity = imageIdentity; fileCache.trusted = true;
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
    };
    inline bool ReadChain(std::uintptr_t entry, Chain& value)
    {
        value = {}; value.target = entry;
        if (!Copy(entry, value.entry.data(), value.entry.size(), true)) return false;
        constexpr unsigned char indirect[6] = {0xff,0x25,0,0,0,0};
        if (value.entry[0] == 0xe9) {
            std::uintptr_t relay = 0;
            if (!Relative(entry, Word<std::int32_t>(value.entry.data(), 1), relay) || relay < 0x10010) return false;
            value.kind = 1; value.stolen = 11; value.length = 30; value.trampoline = relay - 16;
            if (!Copy(value.trampoline, value.trampolineBytes.data(), value.length, true)) return false;
            value.block = Word<std::uintptr_t>(value.trampolineBytes.data(), 22);
            for (std::size_t i = 5; i < 11; ++i) if (value.entry[i] != 0x90) return false;
            if (std::memcmp(value.entry.data() + 11, Original + 11, 7) ||
                std::memcmp(value.trampolineBytes.data() + 16, indirect, 6)) return false;
            std::uintptr_t resume = 0;
            if (value.trampolineBytes[11] != 0xe9 || !Relative(value.trampoline + 11,
                Word<std::int32_t>(value.trampolineBytes.data(), 12), resume) || resume != entry + 11) return false;
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
            Word<std::uintptr_t>(value.blockBytes.data(), 0x17f) != value.trampoline ||
            std::memcmp(value.trampolineBytes.data(), Original, value.stolen)) return false;
        value.callback = Word<std::uintptr_t>(value.blockBytes.data(), 0x177);
        auto normalized = value.blockBytes;
        std::memset(normalized.data() + 0x177, 0, 16);
        return HasHash(normalized.data(), normalized.size(), TemplateHash);
    }
    inline bool ReadModel(std::uintptr_t module, Chain& value)
    {
        if (!Range(module, ImageSize) || !Copy(module + Slot, &value.object, sizeof(value.object)) ||
            (value.object & 7) || !Range(value.object, 0x158b) ||
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
        unsigned char full[18]{};
        std::memcpy(full, value.backup.data(), value.stolen);
        if (value.stolen < 18) std::memcpy(full + value.stolen, value.entry.data() + value.stolen, 18 - value.stolen);
        return !std::memcmp(full, Original, 18);
    }
    struct ImageContent
    {
        std::array<unsigned char,BlockSize> pattern{};
        std::array<unsigned char,BodySize> body{};
        std::array<unsigned char,5> thunk{};
    };
    inline bool ReadImage(std::uintptr_t base, std::uintptr_t callback, ImageContent& value)
    {
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        if (!Range(base, ImageSize) || callback != base + Callback ||
            !Copy(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || static_cast<std::size_t>(dos.e_lfanew) > 4096 - sizeof(nt) ||
            !Copy(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.OptionalHeader.SizeOfImage != ImageSize ||
            !Copy(base + Template, value.pattern.data(), value.pattern.size()) ||
            !Copy(base + Body, value.body.data(), value.body.size(), true) ||
            !Copy(base + Callback, value.thunk.data(), value.thunk.size(), true)) return false;
        constexpr unsigned char thunk[5] = {0xe9,0xd4,0x6a,0x18,0};
        // These pinned ranges have no PE base relocations; no mutable mapped
        // template is allowed to serve as its own identity anchor.
        return !std::memcmp(value.thunk.data(), thunk, 5) &&
            HasHash(value.pattern.data(), value.pattern.size(), TemplateHash) &&
            HasHash(value.body.data(), value.body.size(), BodyHash);
    }
    struct Proof { Digest digest{}; unsigned kind = 0; };
    inline bool Inspect(std::uintptr_t entry, Proof& proof)
    {
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
            if (!FileIdentity(module, identity) || !ReadImage(base, first.callback, imageBefore) ||
                !ReadModel(base, first) || !ReadChain(entry, last) || !ReadModel(base, last) ||
                !ReadImage(base, last.callback, imageAfter) ||
                first.kind != last.kind || first.object != last.object || first.block != last.block ||
                first.trampoline != last.trampoline || first.callback != last.callback ||
                first.entry != last.entry || first.backup != last.backup || first.model != last.model ||
                first.trampolineBytes != last.trampolineBytes || first.blockBytes != last.blockBytes ||
                imageBefore.pattern != imageAfter.pattern || imageBefore.body != imageAfter.body ||
                imageBefore.thunk != imageAfter.thunk) return false;
            Hash hash;
            if (!hash.Add(identity.data(), identity.size()) || !hash.Add(&first.object, sizeof(first.object)) ||
                !hash.Add(first.entry.data(), first.entry.size()) || !hash.Add(first.model.data(), first.model.size()) ||
                !hash.Add(first.backup.data(), first.backup.size()) ||
                !hash.Add(first.trampolineBytes.data(), first.trampolineBytes.size()) ||
                !hash.Add(first.blockBytes.data(), first.blockBytes.size()) || !hash.Finish(proof.digest)) return false;
            proof.kind = first.kind; return true;
        } catch (...) { proof = {}; return false; }
    }
}
