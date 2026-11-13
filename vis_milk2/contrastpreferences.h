/*
 * contrastpreferences.h - Content identity and local contrast choices.
 * Language: en-GB
 * SPDX-License-Identifier: MPL-2.0
 */
#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace PresetContrast
{
inline bool ValidDigest(std::string_view digest) noexcept
{
    if (digest.size() != 64) return false;
    for (char c : digest)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline std::string ContentDigest(FILE* file)
{
    if (!file) return {};
    const auto originalPosition = _ftelli64(file);
    if (originalPosition < 0 || _fseeki64(file, 0, SEEK_END) != 0) return {};
    const auto length = _ftelli64(file);
    if (_fseeki64(file, originalPosition, SEEK_SET) != 0) return {};
    if (length < 0 || length > 16 * 1024 * 1024 || _fseeki64(file, 0, SEEK_SET) != 0) return {};
    struct Resources
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        FILE* file;
        __int64 position;
        ~Resources()
        {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            _fseeki64(file, position, SEEK_SET);
        }
    } resources{nullptr, nullptr, file, originalPosition};
    if (BCryptOpenAlgorithmProvider(&resources.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(resources.algorithm, &resources.hash, nullptr, 0, nullptr, 0, 0) < 0)
        return {};
    std::array<unsigned char, 8192> bytes{};
    auto remaining = length;
    while (remaining > 0)
    {
        const size_t requested = static_cast<size_t>((std::min)(remaining, static_cast<__int64>(bytes.size())));
        const size_t count = fread(bytes.data(), 1, requested, file);
        if (count != requested || BCryptHashData(resources.hash, bytes.data(), static_cast<ULONG>(count), 0) < 0)
            return {};
        remaining -= count;
    }
    if (fgetc(file) != EOF || ferror(file)) return {};
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(resources.hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned char value : digest) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}

inline bool ConfirmedLowContrast(std::string_view digest) noexcept
{
    // Validated rendering definitions. Identity follows the exact bytes across
    // renamed/moved copies; unrelated or edited presets are never enrolled.
    return digest == "0ca9b4631cb941efaef1c17e6839c6c016e102cc2080f97252d066924b491e3d" ||
           digest == "fabd164c65bd756604026b9ce8db289f1909c7b80c0f901d114dce61955020fa";
}

inline bool AbsoluteProfilePath(const wchar_t* path) noexcept
{
    if (!path || !path[0] || !path[1]) return false;
    return (path[0] == L'\\' && path[1] == L'\\') ||
        (path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'));
}

inline bool Enabled(const wchar_t* ini, std::string_view digest)
{
    if (!ValidDigest(digest)) return false;
    const std::wstring key(digest.begin(), digest.end());
    const UINT choice = AbsoluteProfilePath(ini) ? GetPrivateProfileIntW(L"AutomaticContrast", key.c_str(), 2, ini) : 2;
    return choice != 0;
}

inline bool UseDiscovery(std::string_view digest)
{
    return ValidDigest(digest) && !ConfirmedLowContrast(digest);
}

inline bool SaveChoice(const wchar_t* ini, std::string_view digest, bool enabled)
{
    if (!AbsoluteProfilePath(ini) || !ValidDigest(digest)) return false;
    const std::wstring key(digest.begin(), digest.end());
    return WritePrivateProfileStringW(L"AutomaticContrast", key.c_str(), enabled ? L"1" : L"0", ini) != FALSE;
}
}
