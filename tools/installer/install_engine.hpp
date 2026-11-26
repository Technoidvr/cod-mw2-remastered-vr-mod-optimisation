#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shellapi.h>

#include "game_paths.hpp"

#include <zlib.h>
#include <unzip.h>
#include <ioapi.h>
#include <iowin32.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace optilord_installer
{
    using progress_fn = std::function<void(int percent, const std::wstring& status)>;
    using log_fn = std::function<void(const std::wstring& line)>;

    struct install_options
    {
        std::filesystem::path payload_zip;
        bool no_registry{};
        bool no_shortcut{};
    };

    struct uninstall_options
    {
        bool no_registry{};
        bool remove_diagnostics{};
    };

    namespace detail
    {
        struct unique_handle
        {
            HANDLE handle{INVALID_HANDLE_VALUE};

            unique_handle() = default;
            explicit unique_handle(HANDLE h) noexcept : handle(h) {}
            ~unique_handle() noexcept
            {
                if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }

            unique_handle(const unique_handle&) = delete;
            unique_handle& operator=(const unique_handle&) = delete;

            [[nodiscard]] bool valid() const noexcept
            {
                return handle != nullptr && handle != INVALID_HANDLE_VALUE;
            }
        };

        struct unique_unz_file
        {
            unzFile uf{nullptr};
            bool current_open{false};

            unique_unz_file() = default;
            explicit unique_unz_file(unzFile f) noexcept : uf(f) {}
            ~unique_unz_file() noexcept
            {
                if (uf != nullptr)
                {
                    if (current_open)
                    {
                        unzCloseCurrentFile(uf);
                    }
                    unzClose(uf);
                }
            }

            unique_unz_file(const unique_unz_file&) = delete;
            unique_unz_file& operator=(const unique_unz_file&) = delete;

            int close_current() noexcept
            {
                if (!current_open || uf == nullptr)
                {
                    return UNZ_OK;
                }
                current_open = false;
                return unzCloseCurrentFile(uf);
            }
        };

        struct com_initializer
        {
            HRESULT hr{E_FAIL};

            explicit com_initializer(DWORD coinit_flags) noexcept
                : hr(CoInitializeEx(nullptr, coinit_flags))
            {
            }

            ~com_initializer() noexcept
            {
                if (hr == S_OK || hr == S_FALSE)
                {
                    CoUninitialize();
                }
            }

            com_initializer(const com_initializer&) = delete;
            com_initializer& operator=(const com_initializer&) = delete;

            [[nodiscard]] bool ok() const noexcept
            {
                return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
            }
        };

        template <typename T>
        struct com_ptr
        {
            T* ptr{nullptr};

            com_ptr() = default;
            ~com_ptr() noexcept
            {
                if (ptr != nullptr)
                {
                    ptr->Release();
                }
            }

            com_ptr(const com_ptr&) = delete;
            com_ptr& operator=(const com_ptr&) = delete;

            T** put() noexcept
            {
                return &ptr;
            }

            T* operator->() const noexcept
            {
                return ptr;
            }
        };

        inline void emit_log(const log_fn& log, const std::wstring& line)
        {
            if (log)
            {
                log(line);
            }
        }

        inline void emit_progress(const progress_fn& progress, int percent, const std::wstring& status)
        {
            if (progress)
            {
                progress(percent, status);
            }
        }

        inline bool is_reparse_point(const std::filesystem::path& p) noexcept
        {
            const DWORD attrs = GetFileAttributesW(p.c_str());
            return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }

        inline bool has_reparse_component(const std::filesystem::path& base, const std::filesystem::path& rel)
        {
            std::filesystem::path cursor = base;
            for (const auto& segment : rel)
            {
                cursor /= segment;
                if (is_reparse_point(cursor))
                {
                    return true;
                }
            }
            return false;
        }

        inline void clear_readonly_attribute(const std::filesystem::path& p) noexcept
        {
            const DWORD attrs = GetFileAttributesW(p.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0)
            {
                SetFileAttributesW(p.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }

        inline bool remove_file_safe(const std::filesystem::path& p, const log_fn& log, std::wstring_view display_rel)
        {
            std::error_code ec;
            if (!std::filesystem::exists(p, ec) && !is_reparse_point(p))
            {
                return false;
            }
            ec.clear();
            if (std::filesystem::is_directory(p, ec) && !is_reparse_point(p))
            {
                return false;
            }

            clear_readonly_attribute(p);
            ec.clear();
            if (std::filesystem::remove(p, ec) && !ec)
            {
                emit_log(log, L"Removed: " + std::wstring(display_rel));
                return true;
            }
            return false;
        }

        inline void remove_tree_safe(const std::filesystem::path& dir, const log_fn& log, std::wstring_view display_rel)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) && !is_reparse_point(dir))
            {
                return;
            }

            if (is_reparse_point(dir))
            {
                clear_readonly_attribute(dir);
                if (RemoveDirectoryW(dir.c_str()) || DeleteFileW(dir.c_str()))
                {
                    emit_log(log, L"Removed link: " + std::wstring(display_rel));
                }
                return;
            }

            ec.clear();
            if (!std::filesystem::is_directory(dir, ec) || ec)
            {
                return;
            }

            std::vector<std::filesystem::path> dirs_to_remove;
            dirs_to_remove.push_back(dir);

            std::size_t scan_idx = 0;
            while (scan_idx < dirs_to_remove.size())
            {
                const std::filesystem::path current = dirs_to_remove[scan_idx++];
                ec.clear();
                for (std::filesystem::directory_iterator it(
                         current, std::filesystem::directory_options::skip_permission_denied, ec);
                     !ec && it != std::filesystem::directory_iterator{};
                     it.increment(ec))
                {
                    const std::filesystem::path child = it->path();
                    if (is_reparse_point(child))
                    {
                        clear_readonly_attribute(child);
                        if (!RemoveDirectoryW(child.c_str()))
                        {
                            DeleteFileW(child.c_str());
                        }
                        continue;
                    }

                    std::error_code child_ec;
                    if (it->is_directory(child_ec) && !child_ec)
                    {
                        dirs_to_remove.push_back(child);
                    }
                    else
                    {
                        clear_readonly_attribute(child);
                        std::filesystem::remove(child, child_ec);
                    }
                }
            }

            for (auto it = dirs_to_remove.rbegin(); it != dirs_to_remove.rend(); ++it)
            {
                clear_readonly_attribute(*it);
                std::error_code rm_ec;
                std::filesystem::remove(*it, rm_ec);
            }
            emit_log(log, L"Removed directory: " + std::wstring(display_rel));
        }

        inline bool is_protected_top_dir_name(std::wstring_view lower_segment) noexcept
        {
            return lower_segment == L"data" ||
                   lower_segment == L"main" ||
                   lower_segment == L"players2" ||
                   lower_segment == L".battle.net" ||
                   lower_segment == L"mods";
        }

        inline bool is_protected_game_path(const std::filesystem::path& rel)
        {
            if (rel.empty())
            {
                return true;
            }

            auto it = rel.begin();
            const std::wstring first = to_lower_wide(it->wstring());
            if (is_protected_top_dir_name(first))
            {
                return true;
            }

            ++it;
            if (it == rel.end())
            {
                if (first == L"mw2cr.exe" ||
                    first == L"h2_sp64_bnet_ship.exe" ||
                    first == L"mw2cr launcher.exe")
                {
                    return true;
                }
            }
            return false;
        }

        inline bool is_windows_reserved_name(std::string_view segment) noexcept
        {
            const auto dot_pos = segment.find('.');
            const std::string_view stem = segment.substr(0, dot_pos);
            if (stem.empty() || stem.size() > 4)
            {
                return false;
            }

            char upper[5]{};
            for (std::size_t i = 0; i < stem.size(); ++i)
            {
                const unsigned char ch = static_cast<unsigned char>(stem[i]);
                upper[i] = (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A')) : static_cast<char>(ch);
            }
            const std::string_view key(upper, stem.size());
            if (key == "CON" || key == "PRN" || key == "AUX" || key == "NUL")
            {
                return true;
            }
            if (key.size() == 4 &&
                ((key[0] == 'C' && key[1] == 'O' && key[2] == 'M') ||
                 (key[0] == 'L' && key[1] == 'P' && key[2] == 'T')) &&
                key[3] >= '1' && key[3] <= '9')
            {
                return true;
            }
            return false;
        }

        inline std::string current_utc_iso8601()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
            std::tm utc_tm{};
            if (gmtime_s(&utc_tm, &now_time) != 0)
            {
                return {};
            }
            std::ostringstream oss;
            oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
            return oss.str();
        }

        inline bool write_reg_sz(HKEY root, const wchar_t* subkey, const wchar_t* value_name, const std::wstring& value)
        {
            HKEY hkey = nullptr;
            const LONG create_res = RegCreateKeyExW(
                root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hkey, nullptr);
            if (create_res != ERROR_SUCCESS)
            {
                return false;
            }
            const DWORD byte_len = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            const LONG set_res = RegSetValueExW(
                hkey,
                value_name,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                byte_len);
            RegCloseKey(hkey);
            return set_res == ERROR_SUCCESS;
        }

        inline bool write_reg_dword(HKEY root, const wchar_t* subkey, const wchar_t* value_name, DWORD value)
        {
            HKEY hkey = nullptr;
            const LONG create_res = RegCreateKeyExW(
                root, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hkey, nullptr);
            if (create_res != ERROR_SUCCESS)
            {
                return false;
            }
            const LONG set_res = RegSetValueExW(
                hkey,
                value_name,
                0,
                REG_DWORD,
                reinterpret_cast<const BYTE*>(&value),
                sizeof(value));
            RegCloseKey(hkey);
            return set_res == ERROR_SUCCESS;
        }

        inline std::filesystem::path desktop_shortcut_path()
        {
            std::filesystem::path desktop = known_folder_path(FOLDERID_Desktop, nullptr);
            if (desktop.empty())
            {
                const DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
                if (needed > 1)
                {
                    std::wstring buf(needed, L'\0');
                    const DWORD written = GetEnvironmentVariableW(L"USERPROFILE", buf.data(), needed);
                    if (written > 0 && written < needed)
                    {
                        buf.resize(written);
                        desktop = std::filesystem::path(buf) / L"Desktop";
                    }
                }
            }
            if (desktop.empty())
            {
                return {};
            }
            return desktop / L"OptiLord VR.lnk";
        }

        inline bool create_desktop_shortcut(const std::filesystem::path& game_dir, const log_fn& log)
        {
            const std::filesystem::path link_path = desktop_shortcut_path();
            if (link_path.empty())
            {
                emit_log(log, L"Warning: Could not resolve Desktop path for shortcut.");
                return false;
            }

            std::error_code ec;
            if (!std::filesystem::is_directory(link_path.parent_path(), ec) || ec)
            {
                emit_log(log, L"Warning: Desktop folder not found; skipping shortcut.");
                return false;
            }

            com_initializer com(COINIT_APARTMENTTHREADED);
            if (!com.ok())
            {
                emit_log(log, L"Warning: Failed to initialize COM for Desktop shortcut.");
                return false;
            }

            com_ptr<IShellLinkW> shell_link;
            if (FAILED(CoCreateInstance(
                    CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(shell_link.put()))))
            {
                emit_log(log, L"Warning: Failed to create IShellLinkW instance.");
                return false;
            }

            const std::filesystem::path target_exe = game_dir / L"overlord.exe";
            shell_link->SetPath(target_exe.c_str());
            shell_link->SetWorkingDirectory(game_dir.c_str());
            shell_link->SetDescription(L"OptiLord VR (Overlord Mod)");
            shell_link->SetIconLocation(target_exe.c_str(), 0);

            com_ptr<IPersistFile> persist_file;
            if (FAILED(shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persist_file.put()))))
            {
                emit_log(log, L"Warning: Failed to query IPersistFile for shortcut.");
                return false;
            }

            if (FAILED(persist_file->Save(link_path.c_str(), TRUE)))
            {
                emit_log(log, L"Warning: Failed to save Desktop shortcut.");
                return false;
            }

            emit_log(log, L"Created Desktop shortcut: " + link_path.wstring());
            return true;
        }
    } // namespace detail

    inline std::vector<std::wstring> find_running_game_processes()
    {
        std::vector<std::wstring> found;
        std::unordered_set<std::wstring> seen;

        detail::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot.valid())
        {
            return found;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.handle, &entry))
        {
            return found;
        }

        do
        {
            const std::wstring exe_name(entry.szExeFile);
            const std::wstring lower = detail::to_lower_wide(exe_name);
            if (lower == L"overlord.exe" ||
                lower == L"overlord-debug.exe" ||
                lower == L"mw2cr.exe" ||
                lower == L"h2_sp64_bnet_ship.exe")
            {
                if (seen.insert(lower).second)
                {
                    found.push_back(exe_name);
                }
            }
        } while (Process32NextW(snapshot.handle, &entry));

        return found;
    }

    inline std::uint64_t compute_pe_image_file_size(const std::filesystem::path& exe_path)
    {
        std::ifstream file(exe_path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return 0;
        }

        const std::streampos raw_size = file.tellg();
        if (raw_size <= 0)
        {
            return 0;
        }
        const std::uint64_t file_size = static_cast<std::uint64_t>(raw_size);

        if (file_size < sizeof(IMAGE_DOS_HEADER))
        {
            return 0;
        }

        file.seekg(0, std::ios::beg);
        IMAGE_DOS_HEADER dos_hdr{};
        file.read(reinterpret_cast<char*>(&dos_hdr), sizeof(dos_hdr));
        if (!file || dos_hdr.e_magic != IMAGE_DOS_SIGNATURE || dos_hdr.e_lfanew <= 0)
        {
            return 0;
        }

        const std::uint64_t nt_offset = static_cast<std::uint64_t>(dos_hdr.e_lfanew);
        const std::uint64_t nt_min_bytes = sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
        if (nt_offset > file_size || file_size - nt_offset < nt_min_bytes)
        {
            return 0;
        }

        file.seekg(static_cast<std::streamoff>(nt_offset), std::ios::beg);
        DWORD nt_signature = 0;
        IMAGE_FILE_HEADER file_hdr{};
        file.read(reinterpret_cast<char*>(&nt_signature), sizeof(nt_signature));
        file.read(reinterpret_cast<char*>(&file_hdr), sizeof(file_hdr));
        if (!file || nt_signature != IMAGE_NT_SIGNATURE)
        {
            return 0;
        }

        const std::uint64_t opt_size = static_cast<std::uint64_t>(file_hdr.SizeOfOptionalHeader);
        const std::uint64_t sec_offset = nt_offset + nt_min_bytes + opt_size;
        const std::uint64_t sec_bytes =
            static_cast<std::uint64_t>(file_hdr.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
        if (sec_offset > file_size || file_size - sec_offset < sec_bytes)
        {
            return 0;
        }

        std::uint64_t max_end = sec_offset + sec_bytes;
        if (opt_size >= sizeof(IMAGE_OPTIONAL_HEADER32))
        {
            IMAGE_OPTIONAL_HEADER32 opt32{};
            file.read(reinterpret_cast<char*>(&opt32), sizeof(opt32));
            if (file && (opt32.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC || opt32.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC))
            {
                max_end = (std::max)(max_end, static_cast<std::uint64_t>(opt32.SizeOfHeaders));
            }
        }

        file.seekg(static_cast<std::streamoff>(sec_offset), std::ios::beg);
        for (WORD i = 0; i < file_hdr.NumberOfSections; ++i)
        {
            IMAGE_SECTION_HEADER sec_hdr{};
            file.read(reinterpret_cast<char*>(&sec_hdr), sizeof(sec_hdr));
            if (!file)
            {
                return 0;
            }
            if (sec_hdr.SizeOfRawData > 0)
            {
                const std::uint64_t sec_end =
                    static_cast<std::uint64_t>(sec_hdr.PointerToRawData) +
                    static_cast<std::uint64_t>(sec_hdr.SizeOfRawData);
                max_end = (std::max)(max_end, sec_end);
            }
        }

        return (std::min)(max_end, file_size);
    }

    inline bool safe_relative_zip_path(const std::string& raw, std::filesystem::path& out)
    {
        out.clear();
        if (raw.empty())
        {
            return false;
        }

        if (raw.front() == '/' || raw.front() == '\\' || raw.back() == '/' || raw.back() == '\\')
        {
            return false;
        }

        for (const char ch : raw)
        {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 0x20 || uch == 0x7f)
            {
                return false;
            }
            if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*')
            {
                return false;
            }
        }

        std::filesystem::path built;
        std::size_t pos = 0;
        while (pos < raw.size())
        {
            const std::size_t next_sep = raw.find_first_of("/\\", pos);
            const std::string segment =
                (next_sep == std::string::npos) ? raw.substr(pos) : raw.substr(pos, next_sep - pos);

            if (segment.empty() || segment == "." || segment == "..")
            {
                return false;
            }
            if (segment.back() == '.' || segment.back() == ' ')
            {
                return false;
            }
            if (detail::is_windows_reserved_name(segment))
            {
                return false;
            }

            const std::wstring wide_seg = to_wide(segment);
            if (wide_seg.empty())
            {
                return false;
            }
            built /= wide_seg;

            if (next_sep == std::string::npos)
            {
                break;
            }
            pos = next_sep + 1;
        }

        if (built.empty() || !built.is_relative() || built.has_root_name() || built.has_root_directory())
        {
            return false;
        }

        out = std::move(built);
        return true;
    }

    inline bool has_embedded_zip_payload(const std::filesystem::path& exe_path) noexcept
    {
        try
        {
            if (exe_path.empty())
            {
                return false;
            }

            std::error_code ec;
            if (!std::filesystem::is_regular_file(exe_path, ec) || ec)
            {
                return false;
            }

            zlib_filefunc64_def ffunc{};
            fill_win32_filefunc64W(&ffunc);
            detail::unique_unz_file archive(unzOpen2_64(exe_path.c_str(), &ffunc));
            if (archive.uf == nullptr)
            {
                return false;
            }

            const bool has_exe = (unzLocateFile(archive.uf, "overlord.exe", 0) == UNZ_OK);
            const bool has_ver = (unzLocateFile(archive.uf, "version.json", 0) == UNZ_OK);
            return has_exe && has_ver;
        }
        catch (...)
        {
            return false;
        }
    }

    inline void clean_mod_files(const std::filesystem::path& game_dir, bool remove_diagnostics, const log_fn& log)
    {
        if (!is_valid_game_dir(game_dir))
        {
            return;
        }

        std::vector<std::filesystem::path> manifest_dirs;

        // 1. Remove tracked files listed in an existing installation manifest.
        const std::filesystem::path manifest_path = game_dir / L"optilord-install-manifest.json";
        std::error_code ec;
        if (std::filesystem::is_regular_file(manifest_path, ec) && !ec && !detail::is_reparse_point(manifest_path))
        {
            try
            {
                std::ifstream in(manifest_path, std::ios::binary);
                if (in.is_open())
                {
                    const auto parsed = nlohmann::json::parse(in, nullptr, false);
                    if (parsed.is_object() && parsed.contains("files") && parsed["files"].is_array())
                    {
                        for (const auto& item : parsed["files"])
                        {
                            if (!item.is_string())
                            {
                                continue;
                            }
                            std::filesystem::path rel;
                            if (!safe_relative_zip_path(item.get<std::string>(), rel))
                            {
                                continue;
                            }
                            if (detail::is_protected_game_path(rel))
                            {
                                continue;
                            }
                            if (detail::has_reparse_component(game_dir, rel.parent_path()))
                            {
                                continue;
                            }
                            if (!rel.parent_path().empty())
                            {
                                manifest_dirs.push_back(rel.parent_path());
                            }
                            detail::remove_file_safe(game_dir / rel, log, rel.generic_wstring());
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }

        // 2. Remove known legacy/mod root files.
        constexpr const wchar_t* known_root_files[] = {
            L"overlord.exe",
            L"overlord-debug.exe",
            L"overlord.pdb",
            L"overlord-debug.pdb",
            L"openxr_loader.dll",
            L"openxr_loader.pdb",
            L"overlord.vrmanifest",
            L"overlord-debug.vrmanifest",
            L"release-info.json",
            L"version.json",
            L"LICENSE",
            L"THIRD_PARTY_NOTICES.md",
            L"README.md",
            L"CHANGELOG.md",
            L"OPTIMIZATIONS.md",
            L"OPTIMIZATIONS_FR.md",
            L"optilord-install-manifest.json",
        };
        for (const wchar_t* filename : known_root_files)
        {
            detail::remove_file_safe(game_dir / filename, log, filename);
        }

        // 3. Remove mod-only subdirectories.
        constexpr const wchar_t* mod_subdirs[] = {
            L"h2-mod",
            L"steamvr",
            L"vr_input",
            L"licenses",
            L"docs",
        };
        for (const wchar_t* subdir : mod_subdirs)
        {
            detail::remove_tree_safe(game_dir / subdir, log, subdir);
        }

        // 4. Remove diagnostics only when explicitly requested.
        if (remove_diagnostics)
        {
            constexpr const wchar_t* diagnostic_dirs[] = {
                L"diagnose",
                L"minidumps",
            };
            for (const wchar_t* subdir : diagnostic_dirs)
            {
                detail::remove_tree_safe(game_dir / subdir, log, subdir);
            }
        }

        // 5. Prune empty directories left behind by manifest removal.
        std::sort(
            manifest_dirs.begin(),
            manifest_dirs.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b)
            {
                return a.wstring().size() > b.wstring().size();
            });

        for (const auto& rel_dir : manifest_dirs)
        {
            std::filesystem::path cursor = rel_dir;
            while (!cursor.empty())
            {
                auto first_it = cursor.begin();
                const std::wstring top_name = detail::to_lower_wide(first_it->wstring());
                if (detail::is_protected_top_dir_name(top_name))
                {
                    break;
                }

                const std::filesystem::path full_dir = game_dir / cursor;
                if (detail::is_reparse_point(full_dir))
                {
                    break;
                }

                std::error_code dir_ec;
                if (std::filesystem::is_directory(full_dir, dir_ec) && !dir_ec)
                {
                    dir_ec.clear();
                    if (std::filesystem::is_empty(full_dir, dir_ec) && !dir_ec)
                    {
                        detail::clear_readonly_attribute(full_dir);
                        std::filesystem::remove(full_dir, dir_ec);
                    }
                    else
                    {
                        break;
                    }
                }
                cursor = cursor.parent_path();
            }
        }
    }

    inline bool install_mod(
        const std::filesystem::path& raw_game_dir,
        const install_options& opts,
        const progress_fn& progress,
        const log_fn& log,
        std::string& error)
    {
        error.clear();

        if (!is_valid_game_dir(raw_game_dir))
        {
            error = "Target directory is not a valid MW2CR installation (MW2CR.exe or h2_sp64_bnet_ship.exe missing).";
            return false;
        }
        const std::filesystem::path game_dir = detail::normalize_candidate_path(raw_game_dir);

        const auto running = find_running_game_processes();
        if (!running.empty())
        {
            std::string proc_list;
            for (std::size_t i = 0; i < running.size(); ++i)
            {
                if (i > 0)
                {
                    proc_list += ", ";
                }
                proc_list += to_utf8(running[i]);
            }
            error = "Close running game/mod processes before installing: " + proc_list;
            return false;
        }

        const std::filesystem::path zip_source = opts.payload_zip.empty() ? current_exe_path() : opts.payload_zip;
        if (zip_source.empty() || !has_embedded_zip_payload(zip_source))
        {
            error = "Embedded or external ZIP payload is missing or invalid (requires overlord.exe and version.json).";
            return false;
        }

        detail::emit_progress(progress, 5, L"Removing stale Overlord / OptiLord files...");
        detail::emit_log(log, L"Target directory: " + game_dir.wstring());
        clean_mod_files(game_dir, false, log);

        zlib_filefunc64_def ffunc{};
        fill_win32_filefunc64W(&ffunc);
        detail::unique_unz_file archive(unzOpen2_64(zip_source.c_str(), &ffunc));
        if (archive.uf == nullptr)
        {
            error = "Failed to open installer ZIP payload.";
            return false;
        }

        unz_global_info64 global_info{};
        if (unzGetGlobalInfo64(archive.uf, &global_info) != UNZ_OK || global_info.number_entry == 0)
        {
            error = "Installer ZIP payload contains no entries.";
            return false;
        }

        if (unzGoToFirstFile(archive.uf) != UNZ_OK)
        {
            error = "Failed to iterate installer ZIP entries.";
            return false;
        }

        std::vector<std::string> manifest_files;
        manifest_files.reserve(static_cast<std::size_t>(global_info.number_entry));
        std::array<char, 65536> copy_buf{};

        for (ZPOS64_T entry_idx = 0; entry_idx < global_info.number_entry; ++entry_idx)
        {
            unz_file_info64 file_info{};
            if (unzGetCurrentFileInfo64(archive.uf, &file_info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
            {
                error = "Failed to read ZIP entry metadata.";
                return false;
            }
            if (file_info.size_filename == 0 || file_info.size_filename > 32768)
            {
                error = "Invalid ZIP entry filename length.";
                return false;
            }

            std::string raw_name(static_cast<std::size_t>(file_info.size_filename) + 1, '\0');
            if (unzGetCurrentFileInfo64(
                    archive.uf,
                    nullptr,
                    raw_name.data(),
                    static_cast<uLong>(raw_name.size()),
                    nullptr,
                    0,
                    nullptr,
                    0) != UNZ_OK)
            {
                error = "Failed to read ZIP entry filename.";
                return false;
            }
            raw_name.resize(static_cast<std::size_t>(file_info.size_filename));

            if (raw_name.back() != '/' && raw_name.back() != '\\')
            {
                std::filesystem::path rel_path;
                if (!safe_relative_zip_path(raw_name, rel_path))
                {
                    error = "Unsafe path rejected in ZIP payload: " + raw_name;
                    return false;
                }
                if (detail::is_protected_game_path(rel_path))
                {
                    error = "ZIP payload attempted to overwrite protected game path: " + raw_name;
                    return false;
                }

                const std::wstring lower_rel = detail::to_lower_wide(rel_path.generic_wstring());
                if (lower_rel != L"optilord-uninstall.exe")
                {
                    if (detail::has_reparse_component(game_dir, rel_path))
                    {
                        error = "Refusing to extract across symbolic link / junction: " + raw_name;
                        return false;
                    }

                    const std::filesystem::path target_path = game_dir / rel_path;
                    std::error_code dir_ec;
                    std::filesystem::create_directories(target_path.parent_path(), dir_ec);
                    if (dir_ec)
                    {
                        error = "Failed to create directory for: " + raw_name;
                        return false;
                    }

                    if (unzOpenCurrentFile(archive.uf) != UNZ_OK)
                    {
                        error = "Failed to open compressed entry: " + raw_name;
                        return false;
                    }
                    archive.current_open = true;

                    const std::filesystem::path tmp_path(target_path.wstring() + L".tmp");
                    detail::clear_readonly_attribute(tmp_path);

                    bool write_ok = true;
                    {
                        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
                        if (!out.is_open())
                        {
                            write_ok = false;
                        }
                        else
                        {
                            for (;;)
                            {
                                const int bytes_read = unzReadCurrentFile(
                                    archive.uf, copy_buf.data(), static_cast<unsigned>(copy_buf.size()));
                                if (bytes_read < 0)
                                {
                                    write_ok = false;
                                    break;
                                }
                                if (bytes_read == 0)
                                {
                                    break;
                                }
                                out.write(copy_buf.data(), static_cast<std::streamsize>(bytes_read));
                                if (!out.good())
                                {
                                    write_ok = false;
                                    break;
                                }
                            }
                            out.flush();
                            if (!out.good())
                            {
                                write_ok = false;
                            }
                        }
                    }

                    const int close_rc = archive.close_current();
                    if (!write_ok || close_rc != UNZ_OK)
                    {
                        std::error_code rm_ec;
                        std::filesystem::remove(tmp_path, rm_ec);
                        error = (close_rc == UNZ_CRCERROR)
                                    ? ("CRC error extracting entry: " + raw_name)
                                    : ("Failed to write extracted entry: " + raw_name);
                        return false;
                    }

                    detail::clear_readonly_attribute(target_path);
                    std::error_code move_ec;
                    std::filesystem::rename(tmp_path, target_path, move_ec);
                    if (move_ec)
                    {
                        std::error_code rm_ec;
                        std::filesystem::remove(tmp_path, rm_ec);
                        error = "Failed to finalize extracted file: " + raw_name;
                        return false;
                    }

                    manifest_files.push_back(to_utf8(rel_path.generic_wstring()));
                    detail::emit_log(log, L"Extracted: " + rel_path.generic_wstring());
                }
            }

            const int pct = 10 + static_cast<int>((75ULL * (entry_idx + 1)) / global_info.number_entry);
            detail::emit_progress(progress, pct, L"Extracting OptiLord files...");

            if (entry_idx + 1 < global_info.number_entry)
            {
                if (unzGoToNextFile(archive.uf) != UNZ_OK)
                {
                    error = "Unexpected end of ZIP central directory.";
                    return false;
                }
            }
        }

        // Verify extracted core payload files.
        std::error_code verify_ec;
        const std::filesystem::path installed_exe = game_dir / L"overlord.exe";
        if (!std::filesystem::is_regular_file(installed_exe, verify_ec) || verify_ec)
        {
            error = "Extracted payload is missing overlord.exe.";
            return false;
        }
        verify_ec.clear();
        const std::uintmax_t exe_size = std::filesystem::file_size(installed_exe, verify_ec);
        if (verify_ec || exe_size <= 1048576ULL)
        {
            error = "Extracted overlord.exe failed size validation (> 1 MB required).";
            return false;
        }

        std::string installed_version;
        const std::filesystem::path version_json_path = game_dir / L"version.json";
        verify_ec.clear();
        if (!std::filesystem::is_regular_file(version_json_path, verify_ec) || verify_ec)
        {
            error = "Extracted payload is missing version.json.";
            return false;
        }
        try
        {
            std::ifstream ver_in(version_json_path, std::ios::binary);
            if (!ver_in.is_open())
            {
                error = "Failed to read extracted version.json.";
                return false;
            }
            const auto ver_json = nlohmann::json::parse(ver_in, nullptr, false);
            if (!ver_json.is_object() || !ver_json.contains("name") || !ver_json["name"].is_string())
            {
                error = "Extracted version.json does not contain a valid \"name\" string.";
                return false;
            }
            installed_version = ver_json["name"].get<std::string>();
            if (installed_version.empty())
            {
                error = "Extracted version.json \"name\" field is empty.";
                return false;
            }
        }
        catch (...)
        {
            error = "Failed to parse extracted version.json.";
            return false;
        }

        // Write installation manifest.
        std::sort(manifest_files.begin(), manifest_files.end());
        manifest_files.erase(std::unique(manifest_files.begin(), manifest_files.end()), manifest_files.end());

        {
            nlohmann::json manifest_json = {
                {"product", "OptiLord VR"},
                {"version", installed_version},
                {"installed_utc", detail::current_utc_iso8601()},
                {"files", manifest_files},
            };
            const std::filesystem::path manifest_out_path = game_dir / L"optilord-install-manifest.json";
            std::ofstream manifest_out(manifest_out_path, std::ios::binary | std::ios::trunc);
            if (!manifest_out.is_open())
            {
                error = "Failed to write optilord-install-manifest.json.";
                return false;
            }
            manifest_out << manifest_json.dump(2) << '\n';
            manifest_out.flush();
            if (!manifest_out.good())
            {
                error = "Failed while flushing optilord-install-manifest.json.";
                return false;
            }
            detail::emit_log(log, L"Wrote optilord-install-manifest.json");
        }

        // Extract clean PE stub (without appended ZIP payload) as OptiLord-Uninstall.exe.
        detail::emit_progress(progress, 90, L"Creating OptiLord-Uninstall.exe...");
        const std::filesystem::path self_exe = current_exe_path();
        const std::filesystem::path uninstall_exe = game_dir / L"OptiLord-Uninstall.exe";
        if (self_exe.empty())
        {
            error = "Failed to locate current installer executable path.";
            return false;
        }

        std::error_code eq_ec;
        const bool same_file = std::filesystem::equivalent(self_exe, uninstall_exe, eq_ec) && !eq_ec;
        if (!same_file)
        {
            const std::uint64_t pe_size = compute_pe_image_file_size(self_exe);
            if (pe_size == 0)
            {
                error = "Failed to compute PE image size for OptiLord-Uninstall.exe.";
                return false;
            }

            std::ifstream src_exe(self_exe, std::ios::binary);
            if (!src_exe.is_open())
            {
                error = "Failed to open current installer executable for stub extraction.";
                return false;
            }

            const std::filesystem::path tmp_uninst(uninstall_exe.wstring() + L".tmp");
            detail::clear_readonly_attribute(tmp_uninst);

            bool stub_ok = true;
            {
                std::ofstream dst_exe(tmp_uninst, std::ios::binary | std::ios::trunc);
                if (!dst_exe.is_open())
                {
                    stub_ok = false;
                }
                else
                {
                    std::uint64_t remaining = pe_size;
                    while (remaining > 0)
                    {
                        const std::size_t chunk =
                            static_cast<std::size_t>((std::min)(remaining, static_cast<std::uint64_t>(copy_buf.size())));
                        src_exe.read(copy_buf.data(), static_cast<std::streamsize>(chunk));
                        if (!src_exe || static_cast<std::size_t>(src_exe.gcount()) != chunk)
                        {
                            stub_ok = false;
                            break;
                        }
                        dst_exe.write(copy_buf.data(), static_cast<std::streamsize>(chunk));
                        if (!dst_exe.good())
                        {
                            stub_ok = false;
                            break;
                        }
                        remaining -= chunk;
                    }
                    dst_exe.flush();
                    if (!dst_exe.good())
                    {
                        stub_ok = false;
                    }
                }
            }

            if (!stub_ok)
            {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_uninst, rm_ec);
                error = "Failed to write OptiLord-Uninstall.exe stub.";
                return false;
            }

            detail::clear_readonly_attribute(uninstall_exe);
            std::error_code rename_ec;
            std::filesystem::rename(tmp_uninst, uninstall_exe, rename_ec);
            if (rename_ec)
            {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_uninst, rm_ec);
                error = "Failed to finalize OptiLord-Uninstall.exe.";
                return false;
            }
            detail::emit_log(log, L"Created OptiLord-Uninstall.exe");
        }

        if (!opts.no_registry)
        {
            detail::emit_progress(progress, 95, L"Registering Windows uninstall metadata...");
            const std::wstring game_dir_w = game_dir.wstring();
            const std::wstring quoted_game_dir =
                (!game_dir_w.empty() && game_dir_w.back() == L'\\') ? (game_dir_w + L'\\') : game_dir_w;
            const std::wstring uninstall_cmd =
                L"\"" + uninstall_exe.wstring() + L"\" --uninstall-ui \"" + quoted_game_dir + L"\"";
            const std::wstring display_icon = installed_exe.wstring() + L",0";
            constexpr const wchar_t* uninstall_key =
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OptiLordVR";

            const bool reg_ok =
                detail::write_reg_sz(HKEY_CURRENT_USER, L"Software\\OptiLord", L"InstallDir", game_dir_w) &&
                detail::write_reg_sz(HKEY_CURRENT_USER, uninstall_key, L"DisplayName", L"OptiLord VR (Overlord Mod)") &&
                detail::write_reg_sz(HKEY_CURRENT_USER, uninstall_key, L"DisplayVersion", to_wide(installed_version)) &&
                detail::write_reg_sz(
                    HKEY_CURRENT_USER, uninstall_key, L"Publisher", L"TechnoidVR (based on Overlord VR by Aeka0)") &&
                detail::write_reg_sz(HKEY_CURRENT_USER, uninstall_key, L"InstallLocation", game_dir_w) &&
                detail::write_reg_sz(HKEY_CURRENT_USER, uninstall_key, L"UninstallString", uninstall_cmd) &&
                detail::write_reg_sz(HKEY_CURRENT_USER, uninstall_key, L"DisplayIcon", display_icon) &&
                detail::write_reg_dword(HKEY_CURRENT_USER, uninstall_key, L"NoModify", 1) &&
                detail::write_reg_dword(HKEY_CURRENT_USER, uninstall_key, L"NoRepair", 1);

            if (!reg_ok)
            {
                error = "Failed to write HKCU Windows registration keys.";
                return false;
            }
            detail::emit_log(log, L"Registered HKCU uninstall entry.");
        }

        if (!opts.no_shortcut)
        {
            detail::emit_progress(progress, 98, L"Creating Desktop shortcut...");
            detail::create_desktop_shortcut(game_dir, log);
        }

        detail::emit_progress(progress, 100, L"Installation complete.");
        return true;
    }

    inline bool uninstall_mod(
        const std::filesystem::path& raw_game_dir,
        const uninstall_options& opts,
        const progress_fn& progress,
        const log_fn& log,
        std::string& error)
    {
        error.clear();

        if (!is_valid_game_dir(raw_game_dir))
        {
            error = "Target directory is not a valid MW2CR installation (MW2CR.exe or h2_sp64_bnet_ship.exe missing).";
            return false;
        }
        const std::filesystem::path game_dir = detail::normalize_candidate_path(raw_game_dir);

        const auto running = find_running_game_processes();
        if (!running.empty())
        {
            std::string proc_list;
            for (std::size_t i = 0; i < running.size(); ++i)
            {
                if (i > 0)
                {
                    proc_list += ", ";
                }
                proc_list += to_utf8(running[i]);
            }
            error = "Close running game/mod processes before uninstalling: " + proc_list;
            return false;
        }

        detail::emit_progress(progress, 20, L"Removing OptiLord / Overlord mod files...");
        detail::emit_log(log, L"Uninstalling from: " + game_dir.wstring());
        clean_mod_files(game_dir, opts.remove_diagnostics, log);

        if (!opts.no_registry)
        {
            detail::emit_progress(progress, 60, L"Removing Windows registry entries...");
            RegDeleteTreeW(
                HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OptiLordVR");
            RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\OptiLord");
            detail::emit_log(log, L"Removed HKCU registry entries.");
        }

        {
            detail::emit_progress(progress, 75, L"Removing Desktop shortcut...");
            const std::filesystem::path shortcut = detail::desktop_shortcut_path();
            if (!shortcut.empty())
            {
                std::error_code sc_ec;
                if (std::filesystem::exists(shortcut, sc_ec) && !sc_ec)
                {
                    detail::clear_readonly_attribute(shortcut);
                    std::filesystem::remove(shortcut, sc_ec);
                    if (!sc_ec)
                    {
                        detail::emit_log(log, L"Removed Desktop shortcut.");
                    }
                }
            }
        }

        detail::emit_progress(progress, 90, L"Removing OptiLord-Uninstall.exe...");
        const std::filesystem::path uninstall_exe = game_dir / L"OptiLord-Uninstall.exe";
        std::error_code uninst_ec;
        if (std::filesystem::exists(uninstall_exe, uninst_ec) && !uninst_ec)
        {
            const std::filesystem::path self_exe = current_exe_path();
            std::error_code eq_ec;
            bool running_from_uninstall =
                !self_exe.empty() && std::filesystem::equivalent(self_exe, uninstall_exe, eq_ec) && !eq_ec;
            if (!running_from_uninstall && !self_exe.empty())
            {
                const auto norm_self = detail::normalize_candidate_path(self_exe);
                const auto norm_uninst = detail::normalize_candidate_path(uninstall_exe);
                running_from_uninstall =
                    detail::to_lower_wide(norm_self.wstring()) == detail::to_lower_wide(norm_uninst.wstring());
            }

            if (!running_from_uninstall)
            {
                detail::clear_readonly_attribute(uninstall_exe);
                std::filesystem::remove(uninstall_exe, uninst_ec);
                if (!uninst_ec)
                {
                    detail::emit_log(log, L"Removed OptiLord-Uninstall.exe");
                }
            }
            else
            {
                wchar_t sys_dir[MAX_PATH]{};
                const UINT sys_len = GetSystemDirectoryW(sys_dir, static_cast<UINT>(std::size(sys_dir)));
                const std::filesystem::path system_dir =
                    (sys_len > 0 && sys_len < std::size(sys_dir))
                        ? std::filesystem::path(sys_dir)
                        : std::filesystem::path(L"C:\\Windows\\System32");
                const std::filesystem::path cmd_exe = system_dir / L"cmd.exe";
                std::wstring cmd_line =
                    L"\"" + cmd_exe.wstring() + L"\" /C ping 127.0.0.1 -n 2 > nul & del /F /Q \"" +
                    uninstall_exe.wstring() + L"\"";

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(
                        cmd_exe.c_str(),
                        cmd_line.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        system_dir.c_str(),
                        &si,
                        &pi))
                {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    detail::emit_log(log, L"Scheduled post-exit removal of OptiLord-Uninstall.exe");
                }
            }
        }

        detail::emit_progress(progress, 100, L"Uninstallation complete.");
        return true;
    }
} // namespace optilord_installer
