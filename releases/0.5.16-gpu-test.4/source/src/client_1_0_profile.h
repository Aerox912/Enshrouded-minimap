#pragma once
#include <bcrypt.h>
#include <cstring>
#include <cstdint>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace MinimapClient10
{
    enum class Profile { Unsupported, Build25988036, Build26082781 };

    // Steam client build 25988036. Metadata-only variants retain identical code and data.
    constexpr const char* Sha256 = "82e5020a8736348cd0374f004b2f5e5ef456b5bd27aa5420df34c7dd241cfeeb";
    constexpr std::uintptr_t Rva(Profile profile, std::uintptr_t oldRva)
    {
        if (profile == Profile::Build25988036) return oldRva;
        if (profile != Profile::Build26082781) return 0;
        switch (oldRva) {
        case 0x2b29c0: return 0x2b2b60;
        case 0x2b31a0: return 0x2b3340;
        case 0x2fd340: return 0x2fd4e0;
        case 0x2fd800: return 0x2fd9a0;
        case 0x2fdb4e: return 0x2fdcee;
        case 0x2fdb78: return 0x2fdd18;
        case 0x2fee40: return 0x2fefe0;
        case 0x302610: return 0x3027b0;
        case 0x3031c0: return 0x303360;
        case 0x303207: return 0x3033a7;
        case 0x307040: return 0x3071e0;
        case 0x3070a2: return 0x307242;
        case 0x3070e0: return 0x307280;
        case 0x307104: return 0x3072a4;
        case 0x30e970: return 0x30eb10;
        case 0x30e9f0: return 0x30eb90;
        case 0x310b10: return 0x310cb0;
        case 0x317ce0: return 0x317e80;
        case 0x318335: return 0x3184d5;
        case 0x39b860: return 0x39ba00;
        case 0x39c290: return 0x39c430;
        case 0x39c2d4: return 0x39c474;
        case 0x39c430: return 0x39c5d0;
        case 0x3b1af0: return 0x3b1c90;
        case 0x3b4e40: return 0x3b4fe0;
        case 0x414dc0: return 0x415140;
        case 0x414e14: return 0x415194;
        case 0x415022: return 0x4153a2;
        case 0x9a5e20: return 0x9a7200;
        case 0x9b2e40: return 0x9b4230;
        case 0x9b2fe0: return 0x9b43d0;
        case 0x9b5a50: return 0x9b6e60;
        case 0x9bab60: return 0x9bc1a0;
        case 0xa9b9e0: return 0xa9d530;
        case 0xa9bf1b: return 0xa9da6b;
        case 0xbc0446: return 0xbc1fc6;
        case 0xbc078b: return 0xbc230b;
        case 0xbc093a: return 0xbc24ba;
        case 0xbc0969: return 0xbc24e9;
        case 0xbc09d4: return 0xbc2554;
        case 0xc4e0c0: return 0xc4fc40;
        case 0xc4e0d2: return 0xc4fc52;
        case 0xc4e108: return 0xc4fc88;
        case 0xd6ade0: return 0xd6c860;
        case 0xe7172e: return 0xe742ee;
        case 0xe89080: return 0xe8bc40;
        case 0xea7df2: return 0xeaa9b2;
        case 0xfd1a10: return 0xfd4620;
        case 0xfd2663: return 0xfd5273;
        case 0xfddfa0: return 0xfe0be0;
        case 0xfe0fb3: return 0xfe3bf3;
        case 0xfe1d54: return 0xfe4994;
        case 0x13b7edc: return 0x13b9edc;
        case 0x1bbb4d0: return 0x1bbd820;
        case 0x1de8520: return 0x1dea720;
        case 0x1e02290: return 0x1e04490;
        case 0x1e02460: return 0x1e04660;
        case 0x1e03360: return 0x1e05560;
        case 0x1e041d0: return 0x1e063d0;
        case 0x1e06420: return 0x1e08620;
        case 0x1e07210: return 0x1e09410;
        case 0x1e08760: return 0x1e0a960;
        case 0x1e0c200: return 0x1e0e400;
        case 0x1e0d770: return 0x1e0f970;
        case 0x1e11c00: return 0x1e13e00;
        case 0x281b498: return 0x281e498;
        default: return 0;
        }
    }
    constexpr const char* CurrentSha256 = "d64966e23b1a458ee6f147bcbceac0b577fa7e39a212f371287d8cc119817882";
    inline bool MatchesExactFile(const std::filesystem::path& path, const char* expected = Sha256)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); return false;
        }
        std::array<char,65536> buffer{};
        bool ok = true;
        while (input) {
            input.read(buffer.data(), buffer.size());
            if (input.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0) { ok=false;break; }
        }
        ok = ok && input.eof() && !input.bad();
        std::array<unsigned char,32> digest{};
        ok = ok && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
        BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
        constexpr char hex[]="0123456789abcdef";
        for (size_t i=0;ok && i<digest.size();++i)
            ok = expected[i*2]==hex[digest[i]>>4] && expected[i*2+1]==hex[digest[i]&15];
        return ok;
    }
    // Deliberately narrow fallback: every file byte except the COFF timestamp and
    // PE checksum must remain identical. No wildcard code matches or guessed RVAs.
    inline bool MatchesMetadataVariant(const std::filesystem::path& path,
        size_t ExpectedSize = 35327560,
        const char* Expected = "9d193a090f5af3a89d55006a82bd8477abc226bfd503d233f679f438000a402c")
    {
        if (ExpectedSize < 264) return false;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() != static_cast<std::streamoff>(ExpectedSize)) return false;
        std::vector<unsigned char> bytes(ExpectedSize);
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
        if (bytes[0] != 'M' || bytes[1] != 'Z') return false;
        unsigned long pe = 0;
        std::memcpy(&pe, bytes.data()+60, sizeof(pe));
        if (pe > bytes.size()-264 || std::memcmp(bytes.data()+pe,"PE\0\0",4)!=0) return false;
        if (bytes[pe+4]!=0x64 || bytes[pe+5]!=0x86 || bytes[pe+24]!=0x0b || bytes[pe+25]!=2) return false;
        std::memset(bytes.data()+pe+8,0,4);
        std::memset(bytes.data()+pe+24+64,0,4);
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_HASH_HANDLE hash=nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
        bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
        std::array<unsigned char,32> digest{};
        if(ok) ok=BCryptHashData(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)>=0 &&
            BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
        if(hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm,0);
        constexpr char hex[]="0123456789abcdef";
        for(size_t i=0;ok && i<digest.size();++i)
            ok=Expected[i*2]==hex[digest[i]>>4] && Expected[i*2+1]==hex[digest[i]&15];
        return ok;
    }
    inline Profile IdentifyFile(const std::filesystem::path& path)
    {
        if (MatchesExactFile(path) || MatchesMetadataVariant(path))
            return Profile::Build25988036;
        if (MatchesExactFile(path, CurrentSha256) || MatchesMetadataVariant(path, 35338312,
            "42a9c848e662094b0177075db851c611af816fd9cc8d9b3ad4168f109bd28c62"))
            return Profile::Build26082781;
        return Profile::Unsupported;
    }
    inline bool MatchesFile(const std::filesystem::path& path)
    {
        return IdentifyFile(path) != Profile::Unsupported;
    }
}
