#pragma once
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace MinimapClient10
{
    // Steam client build 25988036. Metadata-only variants retain identical code and data.
    constexpr const char* Sha256 = "82e5020a8736348cd0374f004b2f5e5ef456b5bd27aa5420df34c7dd241cfeeb";
    constexpr std::uint32_t DeathMapArrayOffset = 0x3E6A78;
    inline bool MatchesExactFile(const std::filesystem::path& path)
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
            ok = Sha256[i*2]==hex[digest[i]>>4] && Sha256[i*2+1]==hex[digest[i]&15];
        return ok;
    }
    // Deliberately narrow fallback: every file byte except the COFF timestamp and
    // PE checksum must remain identical. No wildcard code matches or guessed RVAs.
    inline bool MatchesMetadataVariant(const std::filesystem::path& path)
    {
        constexpr size_t ExpectedSize = 35327560;
        constexpr const char* Expected = "9d193a090f5af3a89d55006a82bd8477abc226bfd503d233f679f438000a402c";
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
    inline bool MatchesFile(const std::filesystem::path& path)
    {
        return MatchesExactFile(path) || MatchesMetadataVariant(path);
    }
}
