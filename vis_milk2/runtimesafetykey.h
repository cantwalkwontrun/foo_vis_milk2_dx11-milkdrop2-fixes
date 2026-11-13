/*
 * runtimesafetykey.h - Identity for disposable preset runtime failure records.
 * Language: en-GB
 * SPDX-License-Identifier: MPL-2.0
 */

#pragma once

#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>
#include <utility>

namespace RuntimeSafetyKey
{
// Increment when compatibility/recovery changes warrant retrying old failures.
inline constexpr unsigned CompatibilityRevision = 9;

inline std::wstring Prefix(unsigned revision)
{
    return L"md2-runtime-v2/" + std::to_wstring(revision) + L"/";
}

inline std::wstring NormaliseAbsolutePath(std::wstring path)
{
    for (wchar_t& ch : path)
    {
        if (ch == L'\t' || ch == L'\r' || ch == L'\n')
            return {};
        ch = ch == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(ch));
    }
    const bool drive = path.size() >= 3 && path[0] >= L'a' && path[0] <= L'z' &&
        path[1] == L':' && path[2] == L'\\';
    const bool unc = path.size() > 2 && path[0] == L'\\' && path[1] == L'\\';
    return drive || unc ? path : std::wstring{};
}

// The caller resolves '.'/'..' to a full Windows path and supplies file metadata.
inline std::wstring Make(std::wstring canonicalPath, uint64_t size, uint64_t writeTime,
                         unsigned revision = CompatibilityRevision)
{
    canonicalPath = NormaliseAbsolutePath(std::move(canonicalPath));
    if (canonicalPath.empty())
        return {};
    return Prefix(revision) + std::to_wstring(size) + L"/" + std::to_wstring(writeTime) + L"/" + canonicalPath;
}

inline bool IsCurrent(std::wstring_view key, unsigned revision = CompatibilityRevision)
{
    const std::wstring prefix = Prefix(revision);
    if (key.substr(0, prefix.size()) != prefix)
        return false;
    size_t cursor = prefix.size();
    for (int field = 0; field < 2; ++field)
    {
        const size_t start = cursor;
        while (cursor < key.size() && key[cursor] >= L'0' && key[cursor] <= L'9')
            ++cursor;
        if (cursor == start || cursor == key.size() || key[cursor++] != L'/')
            return false;
    }
    const std::wstring path(key.substr(cursor));
    return !path.empty() && NormaliseAbsolutePath(path) == path;
}
} // namespace RuntimeSafetyKey
