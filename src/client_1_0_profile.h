#pragma once
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <filesystem>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace MinimapClient10
{
    // Steam client build 25966634. Unknown executables never use this ABI.
    constexpr const char* Sha256 = "4d6d43ea4a91e71c82d60b045dde932179c99e32e40657efe374688b2933589a";
    inline bool MatchesFile(const std::filesystem::path& path)
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
}
