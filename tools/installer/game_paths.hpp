#pragma once

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace optilord_installer
{
    struct install_status
    {
        bool valid_game_dir{};
        bool installed{};
        bool legacy_or_stale{};
        std::string version;
        bool has_manifest{};
        bool has_pdb{};
    };

    namespace detail
    {
        inline std::wstring to_lower_wide(std::wstring_view text)
        {
            std::wstring out;
            out.reserve(text.size());
            for (const wchar_t ch : text)
            {
                out.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))));
            }
            return out;
        }

        inline bool contains_icase(std::wstring_view haystack, std::wstring_view needle)
        {
            if (needle.empty())
            {
                return true;
            }
            const auto lower_haystack = to_lower_wide(haystack);
            const auto lower_needle = to_lower_wide(needle);
            return lower_haystack.find(lower_needle) != std::wstring::npos;
        }

        inline std::wstring trim_wide(std::wstring_view text)
        {
            std::size_t start = 0;
            while (start < text.size() && std::iswspace(static_cast<wint_t>(text[start])))
            {
                ++start;
            }
            std::size_t end = text.size();
            while (end > start && std::iswspace(static_cast<wint_t>(text[end - 1])))
            {
                --end;
            }
            return std::wstring(text.substr(start, end - start));
        }

        inline std::wstring strip_outer_quotes(std::wstring_view text)
        {
            std::wstring trimmed = trim_wide(text);
            if (trimmed.size() >= 2 && trimmed.front() == L'"' && trimmed.back() == L'"')
            {
                return trimmed.substr(1, trimmed.size() - 2);
            }
            return trimmed;
        }

        inline std::wstring strip_trailing_separators(std::wstring path_str)
        {
            while (path_str.size() > 3 && (path_str.back() == L'\\' || path_str.back() == L'/'))
            {
                path_str.pop_back();
            }
            // Also trim trailing separator from UNC/relative paths longer than a drive root.
            if (path_str.size() > 1 && !(path_str.size() == 3 && path_str[1] == L':'))
            {
                while (path_str.size() > 1 && (path_str.back() == L'\\' || path_str.back() == L'/'))
                {
                    if (path_str.size() == 3 && path_str[1] == L':')
                    {
                        break;
                    }
                    path_str.pop_back();
                }
            }
            return path_str;
        }

        inline std::filesystem::path normalize_candidate_path(const std::filesystem::path& raw_path)
        {
            if (raw_path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path cleaned(strip_trailing_separators(raw_path.wstring()));
            const auto abs_path = std::filesystem::absolute(cleaned, ec);
            if (!ec && !abs_path.empty())
            {
                cleaned = abs_path;
            }

            ec.clear();
            const auto canonical_path = std::filesystem::weakly_canonical(cleaned, ec);
            if (!ec && !canonical_path.empty())
            {
                cleaned = canonical_path;
            }
            else
            {
                cleaned = cleaned.lexically_normal();
            }

            return std::filesystem::path(strip_trailing_separators(cleaned.wstring()));
        }

        inline std::wstring read_reg_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name)
        {
            DWORD type = 0;
            DWORD bytes = 0;
            const LONG probe = RegGetValueW(
                root, subkey, value_name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &bytes);
            if (probe != ERROR_SUCCESS || bytes < sizeof(wchar_t))
            {
                return {};
            }

            std::wstring buf(bytes / sizeof(wchar_t), L'\0');
            const LONG res = RegGetValueW(
                root, subkey, value_name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buf.data(), &bytes);
            if (res != ERROR_SUCCESS)
            {
                return {};
            }

            const auto nul = buf.find(L'\0');
            if (nul != std::wstring::npos)
            {
                buf.resize(nul);
            }
            return buf;
        }

        inline std::filesystem::path known_folder_path(REFKNOWNFOLDERID folder_id, const wchar_t* env_fallback)
        {
            PWSTR raw = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(folder_id, 0, nullptr, &raw)) && raw != nullptr)
            {
                std::filesystem::path result(raw);
                CoTaskMemFree(raw);
                return result;
            }

            if (env_fallback != nullptr)
            {
                const DWORD needed = GetEnvironmentVariableW(env_fallback, nullptr, 0);
                if (needed > 1)
                {
                    std::wstring buf(needed, L'\0');
                    const DWORD written = GetEnvironmentVariableW(env_fallback, buf.data(), needed);
                    if (written > 0 && written < needed)
                    {
                        buf.resize(written);
                        return std::filesystem::path(buf);
                    }
                }
            }
            return {};
        }

        inline std::vector<std::filesystem::path> extract_dirs_from_display_icon(std::wstring_view raw_icon)
        {
            std::vector<std::filesystem::path> out;
            std::wstring cleaned = trim_wide(raw_icon);
            if (cleaned.empty())
            {
                return out;
            }

            if (cleaned.front() == L'"')
            {
                const auto closing = cleaned.find(L'"', 1);
                if (closing != std::wstring::npos)
                {
                    cleaned = cleaned.substr(1, closing - 1);
                }
                else
                {
                    cleaned.erase(cleaned.begin());
                }
            }
            else
            {
                const auto last_sep = cleaned.find_last_of(L"\\/");
                const auto comma = cleaned.find_last_of(L',');
                if (comma != std::wstring::npos && (last_sep == std::wstring::npos || comma > last_sep))
                {
                    bool numeric_suffix = true;
                    for (std::size_t i = comma + 1; i < cleaned.size(); ++i)
                    {
                        const wchar_t ch = cleaned[i];
                        if (!std::iswdigit(static_cast<wint_t>(ch)) && !std::iswspace(static_cast<wint_t>(ch)) && ch != L'-')
                        {
                            numeric_suffix = false;
                            break;
                        }
                    }
                    if (numeric_suffix)
                    {
                        cleaned.resize(comma);
                    }
                }
            }

            cleaned = strip_outer_quotes(cleaned);
            if (cleaned.empty())
            {
                return out;
            }

            const std::filesystem::path icon_path(cleaned);
            if (!icon_path.parent_path().empty())
            {
                out.push_back(icon_path.parent_path());
            }
            out.push_back(icon_path);
            return out;
        }
    } // namespace detail

    inline std::wstring to_wide(std::string_view utf8)
    {
        if (utf8.empty())
        {
            return {};
        }
        if (utf8.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return {};
        }

        const int src_len = static_cast<int>(utf8.size());
        const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), src_len, nullptr, 0);
        if (needed <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<std::size_t>(needed), L'\0');
        const int written = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), src_len, result.data(), needed);
        if (written <= 0)
        {
            return {};
        }
        return result;
    }

    inline std::string to_utf8(std::wstring_view wide)
    {
        if (wide.empty())
        {
            return {};
        }
        if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return {};
        }

        const int src_len = static_cast<int>(wide.size());
        const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), src_len, nullptr, 0, nullptr, nullptr);
        if (needed <= 0)
        {
            return {};
        }

        std::string result(static_cast<std::size_t>(needed), '\0');
        const int written = WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), src_len, result.data(), needed, nullptr, nullptr);
        if (written <= 0)
        {
            return {};
        }
        return result;
    }

    inline std::filesystem::path current_exe_path()
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD copied = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (copied == 0)
            {
                return {};
            }
            if (copied < static_cast<DWORD>(buf.size()))
            {
                buf.resize(static_cast<std::size_t>(copied));
                return std::filesystem::path(buf);
            }
            if (buf.size() >= 32768)
            {
                return {};
            }
            buf.resize(buf.size() * 2);
        }
    }

    inline bool is_valid_game_dir(const std::filesystem::path& dir) noexcept
    {
        try
        {
            if (dir.empty())
            {
                return false;
            }

            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec)
            {
                return false;
            }

            ec.clear();
            if (std::filesystem::is_regular_file(dir / L"MW2CR.exe", ec) && !ec)
            {
                return true;
            }

            ec.clear();
            if (std::filesystem::is_regular_file(dir / L"h2_sp64_bnet_ship.exe", ec) && !ec)
            {
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    inline std::vector<std::filesystem::path> detect_game_directories()
    {
        std::vector<std::filesystem::path> results;
        std::unordered_set<std::wstring> seen_keys;

        auto try_add = [&](const std::filesystem::path& raw_candidate)
        {
            if (raw_candidate.empty())
            {
                return;
            }
            const auto normalized = detail::normalize_candidate_path(raw_candidate);
            if (!is_valid_game_dir(normalized))
            {
                return;
            }
            const std::wstring key = detail::to_lower_wide(normalized.wstring());
            if (seen_keys.insert(key).second)
            {
                results.push_back(normalized);
            }
        };

        // 1. Existing OptiLord install directory in HKCU.
        {
            const std::wstring reg_dir = detail::strip_outer_quotes(
                detail::read_reg_string(HKEY_CURRENT_USER, L"Software\\OptiLord", L"InstallDir"));
            if (!reg_dir.empty())
            {
                try_add(std::filesystem::path(reg_dir));
            }
        }

        // 2. Windows Uninstall registry entries.
        {
            struct reg_root
            {
                HKEY root;
                const wchar_t* path;
            };
            const reg_root uninstall_roots[] = {
                {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
                {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
                {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
            };

            for (const auto& entry : uninstall_roots)
            {
                HKEY uninstall_key = nullptr;
                if (RegOpenKeyExW(entry.root, entry.path, 0, KEY_READ, &uninstall_key) != ERROR_SUCCESS)
                {
                    continue;
                }

                for (DWORD idx = 0;; ++idx)
                {
                    wchar_t subkey_name[260]{};
                    DWORD subkey_len = static_cast<DWORD>(std::size(subkey_name));
                    const LONG enum_res = RegEnumKeyExW(
                        uninstall_key, idx, subkey_name, &subkey_len, nullptr, nullptr, nullptr, nullptr);
                    if (enum_res == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    if (enum_res != ERROR_SUCCESS)
                    {
                        continue;
                    }

                    HKEY item_key = nullptr;
                    if (RegOpenKeyExW(uninstall_key, subkey_name, 0, KEY_READ, &item_key) != ERROR_SUCCESS)
                    {
                        continue;
                    }

                    const std::wstring display_name = detail::read_reg_string(item_key, nullptr, L"DisplayName");
                    const bool matches_mw2cr =
                        detail::contains_icase(display_name, L"modern warfare 2") ||
                        detail::contains_icase(display_name, L"mw2cr") ||
                        ((detail::contains_icase(display_name, L"call of duty") ||
                          detail::contains_icase(display_name, L"modern warfare")) &&
                         detail::contains_icase(display_name, L"campaign remastered"));

                    if (matches_mw2cr)
                    {
                        const std::wstring install_loc = detail::strip_outer_quotes(
                            detail::read_reg_string(item_key, nullptr, L"InstallLocation"));
                        if (!install_loc.empty())
                        {
                            try_add(std::filesystem::path(install_loc));
                        }

                        const std::wstring display_icon = detail::read_reg_string(item_key, nullptr, L"DisplayIcon");
                        for (const auto& candidate : detail::extract_dirs_from_display_icon(display_icon))
                        {
                            try_add(candidate);
                        }
                    }

                    RegCloseKey(item_key);
                }

                RegCloseKey(uninstall_key);
            }
        }

        // 3. Battle.net client configuration JSON.
        try
        {
            const auto appdata = detail::known_folder_path(FOLDERID_RoamingAppData, L"APPDATA");
            if (!appdata.empty())
            {
                const auto bnet_cfg = appdata / L"Battle.net" / L"Battle.net.config";
                std::error_code ec;
                if (std::filesystem::is_regular_file(bnet_cfg, ec) && !ec)
                {
                    std::ifstream stream(bnet_cfg, std::ios::binary);
                    if (stream.is_open())
                    {
                        const auto parsed = nlohmann::json::parse(stream, nullptr, false);
                        if (parsed.is_object() && parsed.contains("Client") && parsed["Client"].is_object())
                        {
                            const auto& client = parsed["Client"];
                            if (client.contains("Install") && client["Install"].is_object())
                            {
                                const auto& install_obj = client["Install"];
                                if (install_obj.contains("DefaultInstallPath") &&
                                    install_obj["DefaultInstallPath"].is_string())
                                {
                                    const std::wstring wide_base =
                                        to_wide(install_obj["DefaultInstallPath"].get<std::string>());
                                    if (!wide_base.empty())
                                    {
                                        const std::filesystem::path base_dir(wide_base);
                                        try_add(base_dir / L"Call of Duty Modern Warfare 2 Campaign Remastered");
                                        try_add(base_dir);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (...)
        {
        }

        // 4. All fixed logical drives across common library locations.
        {
            const DWORD needed = GetLogicalDriveStringsW(0, nullptr);
            if (needed > 0)
            {
                std::wstring drive_buf(static_cast<std::size_t>(needed) + 1, L'\0');
                const DWORD len = GetLogicalDriveStringsW(needed, drive_buf.data());
                if (len > 0 && len <= needed)
                {
                    constexpr const wchar_t* common_subdirs[] = {
                        L"Program Files (x86)\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Program Files\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Games\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Battle.net\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Blizzard\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Epic\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Epic Games\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"SteamLibrary\\steamapps\\common\\Call of Duty Modern Warfare 2 Campaign Remastered",
                        L"Call of Duty Modern Warfare 2 Campaign Remastered",
                    };

                    const wchar_t* cursor = drive_buf.c_str();
                    while (*cursor != L'\0')
                    {
                        const std::size_t drive_len = std::wcslen(cursor);
                        if (GetDriveTypeW(cursor) == DRIVE_FIXED)
                        {
                            const std::filesystem::path root(cursor);
                            for (const wchar_t* subdir : common_subdirs)
                            {
                                try_add(root / subdir);
                            }
                        }
                        cursor += drive_len + 1;
                    }
                }
            }
        }

        // 5. Current working directory and current executable parent directory.
        {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec && !cwd.empty())
            {
                try_add(cwd);
            }

            const auto self_exe = current_exe_path();
            if (!self_exe.empty() && !self_exe.parent_path().empty())
            {
                try_add(self_exe.parent_path());
            }
        }

        return results;
    }

    inline install_status inspect_installation(const std::filesystem::path& dir)
    {
        install_status status{};
        status.valid_game_dir = is_valid_game_dir(dir);
        if (!status.valid_game_dir)
        {
            return status;
        }

        std::error_code ec;
        auto has_file = [&](const wchar_t* filename) -> bool
        {
            ec.clear();
            return std::filesystem::is_regular_file(dir / filename, ec) && !ec;
        };

        const bool has_overlord_exe = has_file(L"overlord.exe");
        const bool has_debug_exe = has_file(L"overlord-debug.exe");
        status.installed = has_overlord_exe;
        status.has_pdb = has_file(L"overlord.pdb") || has_file(L"overlord-debug.pdb");
        status.has_manifest = has_file(L"optilord-install-manifest.json");

        if (has_file(L"version.json"))
        {
            try
            {
                std::ifstream stream(dir / L"version.json", std::ios::binary);
                if (stream.is_open())
                {
                    const auto parsed = nlohmann::json::parse(stream, nullptr, false);
                    if (parsed.is_object() && parsed.contains("name") && parsed["name"].is_string())
                    {
                        status.version = parsed["name"].get<std::string>();
                    }
                }
            }
            catch (...)
            {
                status.version.clear();
            }
        }

        const bool valid_version_prefix =
            !status.version.empty() &&
            (status.version.front() == '0' || status.version.front() == '1') &&
            status.version != "Beta 8";

        if (status.installed)
        {
            if (status.has_pdb || has_debug_exe || !status.has_manifest || !valid_version_prefix)
            {
                status.legacy_or_stale = true;
            }
        }
        else if (status.has_pdb || has_debug_exe)
        {
            status.legacy_or_stale = true;
        }

        return status;
    }
} // namespace optilord_installer
