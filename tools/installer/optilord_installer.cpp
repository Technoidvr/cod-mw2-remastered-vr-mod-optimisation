#include "game_paths.hpp"
#include "install_engine.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
constexpr UINT kIconId = 101;

constexpr int kCtrlTitleLabel = 1001;
constexpr int kCtrlSubtitleLabel = 1002;
constexpr int kCtrlDirLabel = 1003;
constexpr int kCtrlDirCombo = 1004;
constexpr int kCtrlBrowseBtn = 1005;
constexpr int kCtrlStatusLabel = 1006;
constexpr int kCtrlShortcutCheck = 1007;
constexpr int kCtrlDiagCheck = 1008;
constexpr int kCtrlProgress = 1009;
constexpr int kCtrlLogEdit = 1010;
constexpr int kCtrlInstallBtn = 1011;
constexpr int kCtrlUninstallBtn = 1012;
constexpr int kCtrlLaunchBtn = 1013;
constexpr int kCtrlCloseBtn = 1014;

constexpr UINT kMsgProgress = WM_APP + 1;
constexpr UINT kMsgLog = WM_APP + 2;
constexpr UINT kMsgDone = WM_APP + 3;

enum class status_tone
{
    neutral,
    valid_uninstalled,
    legacy_upgrade,
    installed_clean,
    invalid_dir
};

struct worker_result
{
    bool is_install{};
    std::wstring error;
};

struct ui_strings
{
    bool french{};
    const wchar_t* window_title{};
    const wchar_t* banner_title{};
    const wchar_t* subtitle{};
    const wchar_t* dir_label{};
    const wchar_t* browse_btn{};
    const wchar_t* browse_title{};
    const wchar_t* shortcut_check{};
    const wchar_t* diag_check{};
    const wchar_t* install_btn{};
    const wchar_t* uninstall_btn{};
    const wchar_t* launch_btn{};
    const wchar_t* close_btn{};
    const wchar_t* status_invalid{};
    const wchar_t* status_legacy{};
    const wchar_t* status_not_installed{};
    const wchar_t* status_installed_prefix{};
    const wchar_t* status_installed_plain{};
    const wchar_t* status_no_payload{};
    const wchar_t* initial_log{};
    const wchar_t* confirm_uninstall_title{};
    const wchar_t* confirm_uninstall_body{};
    const wchar_t* done_install_title{};
    const wchar_t* done_install_body{};
    const wchar_t* done_uninstall_title{};
    const wchar_t* done_uninstall_body{};
    const wchar_t* err_title{};
    const wchar_t* launch_err{};
};

ui_strings make_ui_strings() noexcept
{
    const bool fr = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH);
    if (fr)
    {
        return ui_strings{
            true,
            L"OptiLord v0.7.0 \u2014 Installateur & D\u00E9sinstallateur Propre",
            L"OptiLord v0.7.0 \u2014 Installateur & D\u00E9sinstallateur Propre",
            L"Bas\u00E9 sur Overlord VR Beta 8 par Aeka0 \u2014 Supprime automatiquement les anciens binaires avant mise \u00E0 jour",
            L"Dossier du jeu Call of Duty: MW2 Campaign Remastered (MW2CR) :",
            L"Parcourir...",
            L"S\u00E9lectionnez le dossier d'installation de MW2CR (contenant MW2CR.exe)",
            L"Cr\u00E9er un raccourci sur le Bureau (OptiLord VR)",
            L"Supprimer les journaux de diagnostic lors de la d\u00E9sinstallation (diagnose/, minidumps/)",
            L"Installer / Mise \u00E0 jour",
            L"D\u00E9sinstaller",
            L"Lancer OptiLord",
            L"Fermer",
            L"Statut : Dossier invalide \u2014 MW2CR.exe introuvable",
            L"Statut : Ancien Overlord / binaire obsol\u00E8te d\u00E9tect\u00E9 \u2014 mise \u00E0 jour propre pr\u00EAte",
            L"Statut : Non install\u00E9 (MW2CR d\u00E9tect\u00E9 \u2014 pr\u00EAt pour l'installation)",
            L"Statut : OptiLord v",
            L"Statut : OptiLord install\u00E9",
            L" (aucune archive int\u00E9gr\u00E9e \u2014 mode d\u00E9sinstallateur)",
            L"Pr\u00EAt. Les sauvegardes (players2/) et les fichiers du jeu sont toujours pr\u00E9serv\u00E9s.",
            L"Confirmer la d\u00E9sinstallation",
            L"Supprimer tous les fichiers OptiLord / Overlord de ce dossier ?\nLes fichiers du jeu et les sauvegardes players2/ seront conserv\u00E9s.",
            L"Installation termin\u00E9e",
            L"OptiLord v0.7.0 a \u00E9t\u00E9 install\u00E9 proprement.",
            L"D\u00E9sinstallation termin\u00E9e",
            L"OptiLord a \u00E9t\u00E9 supprim\u00E9 proprement. Vos fichiers MW2CR et sauvegardes players2/ sont intacts.",
            L"Erreur",
            L"Impossible de lancer overlord.exe."
        };
    }

    return ui_strings{
        false,
        L"OptiLord v0.7.0 \u2014 Clean Installer & Uninstaller",
        L"OptiLord v0.7.0 \u2014 Clean Installer & Uninstaller",
        L"Based on Overlord VR Beta 8 by Aeka0 \u2014 Automatically removes stale Overlord binaries before updating",
        L"Call of Duty: MW2 Campaign Remastered (MW2CR) folder:",
        L"Browse...",
        L"Select your MW2CR game directory (containing MW2CR.exe)",
        L"Create Desktop shortcut (OptiLord VR)",
        L"Remove diagnostic logs on uninstall (diagnose/, minidumps/) \u2014 players2/ saves always kept",
        L"Install / Clean Update",
        L"Uninstall",
        L"Launch OptiLord",
        L"Close",
        L"Status: Invalid folder \u2014 MW2CR.exe not found",
        L"Status: Legacy Overlord / stale binary detected \u2014 clean upgrade ready",
        L"Status: Not installed (MW2CR detected \u2014 ready to install)",
        L"Status: OptiLord v",
        L"Status: OptiLord installed",
        L" (no embedded package \u2014 uninstaller mode)",
        L"Ready. Game files and campaign saves (players2/) are always preserved.",
        L"Confirm Uninstall",
        L"Remove all OptiLord / Overlord mod files from this folder?\nGame files and players2/ campaign saves will be kept.",
        L"Installation Complete",
        L"OptiLord v0.7.0 has been cleanly installed.",
        L"Uninstall Complete",
        L"OptiLord has been cleanly removed. Your MW2CR files and players2/ saves remain intact.",
        L"Error",
        L"Failed to launch overlord.exe."
    };
}

std::wstring utf8_to_wide(std::string_view input)
{
    if (input.empty())
    {
        return {};
    }
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0)
    {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), needed);
    return out;
}

std::string wide_to_utf8(std::wstring_view input)
{
    if (input.empty())
    {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::filesystem::path get_self_exe_path()
{
    std::wstring buffer(512, L'\0');
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0)
        {
            return {};
        }
        if (len < buffer.size() - 1)
        {
            buffer.resize(len);
            return std::filesystem::path(buffer);
        }
        if (buffer.size() >= 32768)
        {
            buffer.resize(len);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path normalize_dir_input(std::wstring text)
{
    auto is_trim = [](wchar_t ch) noexcept
    {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!text.empty() && is_trim(text.front()))
    {
        text.erase(text.begin());
    }
    while (!text.empty() && is_trim(text.back()))
    {
        text.pop_back();
    }
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
    {
        text = text.substr(1, text.size() - 2);
    }
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/'))
    {
        text.pop_back();
    }
    if (text.size() == 2 && text[1] == L':')
    {
        text.push_back(L'\\');
    }
    return std::filesystem::path(text);
}

void attach_console_if_needed() noexcept
{
    const DWORD out_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    const DWORD err_type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
    if (out_type != FILE_TYPE_UNKNOWN && err_type != FILE_TYPE_UNKNOWN)
    {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        return;
    }
    FILE* stream = nullptr;
    if (out_type == FILE_TYPE_UNKNOWN)
    {
        freopen_s(&stream, "CONOUT$", "w", stdout);
    }
    if (err_type == FILE_TYPE_UNKNOWN)
    {
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

void write_std_handle(DWORD std_handle_id, std::wstring_view text) noexcept
{
    const HANDLE handle = GetStdHandle(std_handle_id);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD mode = 0;
    if (GetFileType(handle) == FILE_TYPE_CHAR && GetConsoleMode(handle, &mode) != FALSE)
    {
        DWORD written = 0;
        WriteConsoleW(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        return;
    }
    const std::string utf8 = wide_to_utf8(text);
    std::size_t offset = 0;
    while (offset < utf8.size())
    {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(utf8.size() - offset, 65536));
        DWORD written = 0;
        if (WriteFile(handle, utf8.data() + offset, chunk, &written, nullptr) == FALSE || written == 0)
        {
            break;
        }
        offset += written;
    }
}

void out_line(std::wstring_view line)
{
    std::wstring buffered(line);
    buffered.push_back(L'\n');
    write_std_handle(STD_OUTPUT_HANDLE, buffered);
}

void err_line(std::wstring_view line)
{
    std::wstring buffered(line);
    buffered.push_back(L'\n');
    write_std_handle(STD_ERROR_HANDLE, buffered);
}

void print_cli_help()
{
    out_line(L"OptiLord v0.7.0 Installer & Uninstaller");
    out_line(L"Usage:");
    out_line(L"  OptiLord-Setup.exe                                         Open installer GUI");
    out_line(L"  OptiLord-Setup.exe --detect                                List detected MW2CR directories");
    out_line(L"  OptiLord-Setup.exe --status <dir>                          Inspect installation status");
    out_line(L"  OptiLord-Setup.exe --install <dir> [--payload <zip>] [--no-registry] [--no-shortcut]");
    out_line(L"  OptiLord-Setup.exe --uninstall <dir> [--no-registry] [--remove-diagnostics]");
    out_line(L"  OptiLord-Setup.exe --uninstall-ui [<dir>]                  Open GUI focused on uninstall");
    out_line(L"  OptiLord-Setup.exe --help                                  Show this help");
}

int run_cli(const std::vector<std::wstring>& args)
{
    attach_console_if_needed();

    const std::wstring& cmd = args[1];
    if (cmd == L"--help" || cmd == L"-h" || cmd == L"/?")
    {
        print_cli_help();
        return 0;
    }

    if (cmd == L"--detect")
    {
        const auto dirs = optilord_installer::detect_game_directories();
        for (const auto& dir : dirs)
        {
            out_line(dir.wstring());
        }
        return 0;
    }

    if (cmd == L"--status")
    {
        if (args.size() < 3)
        {
            err_line(L"Missing directory argument for --status.");
            return 1;
        }
        const auto dir = normalize_dir_input(args[2]);
        const auto status = optilord_installer::inspect_installation(dir);
        const std::wstring ver = status.version.empty() ? L"none" : utf8_to_wide(status.version);
        const std::wstring line =
            L"valid=" + std::to_wstring(status.valid_game_dir ? 1 : 0) +
            L" installed=" + std::to_wstring(status.installed ? 1 : 0) +
            L" legacy_or_stale=" + std::to_wstring(status.legacy_or_stale ? 1 : 0) +
            L" version=" + ver +
            L" manifest=" + std::to_wstring(status.has_manifest ? 1 : 0) +
            L" pdb=" + std::to_wstring(status.has_pdb ? 1 : 0);
        out_line(line);
        return status.valid_game_dir ? 0 : 1;
    }

    if (cmd == L"--install")
    {
        if (args.size() < 3)
        {
            err_line(L"Missing directory argument for --install.");
            return 1;
        }
        const auto dir = normalize_dir_input(args[2]);
        optilord_installer::install_options opts{};
        for (std::size_t i = 3; i < args.size(); ++i)
        {
            if (args[i] == L"--payload")
            {
                if (i + 1 >= args.size())
                {
                    err_line(L"Missing path after --payload.");
                    return 1;
                }
                opts.payload_zip = std::filesystem::path(args[++i]);
            }
            else if (args[i] == L"--no-registry")
            {
                opts.no_registry = true;
            }
            else if (args[i] == L"--no-shortcut")
            {
                opts.no_shortcut = true;
            }
            else
            {
                err_line(L"Unknown --install option: " + args[i]);
                return 1;
            }
        }

        std::wstring last_status;
        const optilord_installer::progress_fn on_progress =
            [&last_status](int percent, const std::wstring& status)
        {
            if (!status.empty() && status != last_status)
            {
                last_status = status;
                out_line(L"[" + std::to_wstring(percent) + L"%] " + status);
            }
        };
        const optilord_installer::log_fn on_log = [](const std::wstring& line)
        {
            out_line(line);
        };

        std::string error;
        if (!optilord_installer::install_mod(dir, opts, on_progress, on_log, error))
        {
            err_line(L"Install failed: " + utf8_to_wide(error));
            return 1;
        }
        out_line(L"Install succeeded.");
        return 0;
    }

    if (cmd == L"--uninstall")
    {
        if (args.size() < 3)
        {
            err_line(L"Missing directory argument for --uninstall.");
            return 1;
        }
        const auto dir = normalize_dir_input(args[2]);
        optilord_installer::uninstall_options opts{};
        for (std::size_t i = 3; i < args.size(); ++i)
        {
            if (args[i] == L"--no-registry")
            {
                opts.no_registry = true;
            }
            else if (args[i] == L"--remove-diagnostics")
            {
                opts.remove_diagnostics = true;
            }
            else
            {
                err_line(L"Unknown --uninstall option: " + args[i]);
                return 1;
            }
        }

        std::wstring last_status;
        const optilord_installer::progress_fn on_progress =
            [&last_status](int percent, const std::wstring& status)
        {
            if (!status.empty() && status != last_status)
            {
                last_status = status;
                out_line(L"[" + std::to_wstring(percent) + L"%] " + status);
            }
        };
        const optilord_installer::log_fn on_log = [](const std::wstring& line)
        {
            out_line(line);
        };

        std::string error;
        if (!optilord_installer::uninstall_mod(dir, opts, on_progress, on_log, error))
        {
            err_line(L"Uninstall failed: " + utf8_to_wide(error));
            return 1;
        }
        out_line(L"Uninstall succeeded.");
        return 0;
    }

    err_line(L"Unknown option: " + cmd);
    print_cli_help();
    return 1;
}

template <typename T>
struct com_release
{
    T* ptr{};
    ~com_release()
    {
        if (ptr != nullptr)
        {
            ptr->Release();
        }
    }
    com_release() = default;
    com_release(const com_release&) = delete;
    com_release& operator=(const com_release&) = delete;
};

struct app_state
{
    HINSTANCE instance{};
    ui_strings strings{};
    std::filesystem::path self_exe;
    std::filesystem::path initial_dir;
    bool uninstall_focus{};
    bool payload_available{};
    bool busy{};
    status_tone tone{status_tone::neutral};

    HFONT title_font{};
    HFONT body_font{};
    HFONT bold_font{};
    HFONT mono_font{};

    HWND hwnd{};
    HWND hTitleLabel{};
    HWND hSubtitleLabel{};
    HWND hDirLabel{};
    HWND hDirCombo{};
    HWND hBrowseBtn{};
    HWND hStatusLabel{};
    HWND hShortcutCheck{};
    HWND hDiagCheck{};
    HWND hProgress{};
    HWND hLogEdit{};
    HWND hInstallBtn{};
    HWND hUninstallBtn{};
    HWND hLaunchBtn{};
    HWND hCloseBtn{};

    std::thread worker;
};

std::wstring get_window_text_string(HWND ctrl)
{
    const int len = GetWindowTextLengthW(ctrl);
    if (len <= 0)
    {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    const int copied = GetWindowTextW(ctrl, text.data(), len + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
    return text;
}

std::wstring get_combo_selected_or_text(HWND hCombo)
{
    const LRESULT idx = SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
    if (idx != CB_ERR)
    {
        const LRESULT len = SendMessageW(hCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(idx), 0);
        if (len > 0)
        {
            std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
            const LRESULT copied = SendMessageW(
                hCombo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(text.data()));
            if (copied > 0)
            {
                text.resize(static_cast<std::size_t>(copied));
                return text;
            }
        }
    }
    return get_window_text_string(hCombo);
}

void append_log_line(HWND hLogEdit, std::wstring_view line)
{
    std::wstring normalized;
    normalized.reserve(line.size() + 4);
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == L'\n' && (i == 0 || line[i - 1] != L'\r'))
        {
            normalized.push_back(L'\r');
        }
        normalized.push_back(line[i]);
    }
    if (!normalized.empty() && normalized.back() != L'\n')
    {
        normalized.append(L"\r\n");
    }

    const int text_len = GetWindowTextLengthW(hLogEdit);
    SendMessageW(hLogEdit, EM_SETSEL, static_cast<WPARAM>(text_len), static_cast<LPARAM>(text_len));
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(normalized.c_str()));
    SendMessageW(hLogEdit, EM_SCROLLCARET, 0, 0);
}

void set_controls_for_busy(app_state* state, bool busy)
{
    state->busy = busy;
    EnableWindow(state->hDirCombo, busy ? FALSE : TRUE);
    EnableWindow(state->hBrowseBtn, busy ? FALSE : TRUE);
    EnableWindow(state->hShortcutCheck, busy ? FALSE : TRUE);
    EnableWindow(state->hDiagCheck, busy ? FALSE : TRUE);
    EnableWindow(state->hCloseBtn, busy ? FALSE : TRUE);
    if (HMENU sys = GetSystemMenu(state->hwnd, FALSE))
    {
        EnableMenuItem(
            sys,
            SC_CLOSE,
            MF_BYCOMMAND | (busy ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
    }
}

void update_status_and_buttons(app_state* state, std::wstring_view override_dir = {})
{
    const std::wstring raw = override_dir.empty()
        ? get_window_text_string(state->hDirCombo)
        : std::wstring(override_dir);
    const std::filesystem::path dir = normalize_dir_input(raw);
    const auto status = optilord_installer::inspect_installation(dir);

    std::wstring label;
    if (!status.valid_game_dir)
    {
        label = state->strings.status_invalid;
        state->tone = status_tone::invalid_dir;
    }
    else if (status.installed && status.legacy_or_stale)
    {
        label = state->strings.status_legacy;
        state->tone = status_tone::legacy_upgrade;
    }
    else if (status.installed)
    {
        if (!status.version.empty())
        {
            label = std::wstring(state->strings.status_installed_prefix) + utf8_to_wide(status.version) +
                (state->strings.french ? L" install\u00E9" : L" installed");
        }
        else
        {
            label = state->strings.status_installed_plain;
        }
        state->tone = status_tone::installed_clean;
    }
    else
    {
        label = state->strings.status_not_installed;
        state->tone = status_tone::valid_uninstalled;
    }

    if (status.valid_game_dir && !state->payload_available)
    {
        label += state->strings.status_no_payload;
    }

    SetWindowTextW(state->hStatusLabel, label.c_str());
    InvalidateRect(state->hStatusLabel, nullptr, TRUE);

    const bool can_install = !state->busy && status.valid_game_dir && state->payload_available;
    const bool can_uninstall = !state->busy && status.valid_game_dir && status.installed;
    const bool can_launch = !state->busy && status.valid_game_dir && status.installed;

    EnableWindow(state->hInstallBtn, can_install ? TRUE : FALSE);
    EnableWindow(state->hUninstallBtn, can_uninstall ? TRUE : FALSE);
    EnableWindow(state->hLaunchBtn, can_launch ? TRUE : FALSE);
}

void browse_for_folder(app_state* state)
{
    com_release<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg.ptr));
    if (FAILED(hr) || dlg.ptr == nullptr)
    {
        return;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dlg.ptr->GetOptions(&opts)))
    {
        dlg.ptr->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dlg.ptr->SetTitle(state->strings.browse_title);

    const std::filesystem::path current = normalize_dir_input(get_window_text_string(state->hDirCombo));
    std::error_code ec;
    if (!current.empty() && std::filesystem::is_directory(current, ec))
    {
        com_release<IShellItem> folder_item;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                current.c_str(), nullptr, IID_PPV_ARGS(&folder_item.ptr))) &&
            folder_item.ptr != nullptr)
        {
            dlg.ptr->SetFolder(folder_item.ptr);
        }
    }

    if (FAILED(dlg.ptr->Show(state->hwnd)))
    {
        return;
    }

    com_release<IShellItem> result;
    if (FAILED(dlg.ptr->GetResult(&result.ptr)) || result.ptr == nullptr)
    {
        return;
    }

    PWSTR raw_path = nullptr;
    if (SUCCEEDED(result.ptr->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) && raw_path != nullptr)
    {
        const std::wstring chosen(raw_path);
        CoTaskMemFree(raw_path);
        SetWindowTextW(state->hDirCombo, chosen.c_str());
        update_status_and_buttons(state, chosen);
    }
}

void post_worker_log(HWND hwnd, const std::wstring& line)
{
    auto msg_ptr = std::make_unique<std::wstring>(line);
    if (PostMessageW(hwnd, kMsgLog, 0, reinterpret_cast<LPARAM>(msg_ptr.get())) != FALSE)
    {
        msg_ptr.release();
    }
}

void post_worker_progress(HWND hwnd, int percent, const std::wstring& status)
{
    auto msg_ptr = std::make_unique<std::wstring>(status);
    const int clamped = (std::max)(0, (std::min)(100, percent));
    if (PostMessageW(
            hwnd, kMsgProgress, static_cast<WPARAM>(clamped), reinterpret_cast<LPARAM>(msg_ptr.get())) != FALSE)
    {
        msg_ptr.release();
    }
}

void start_install_worker(app_state* state)
{
    if (state->busy)
    {
        return;
    }
    if (state->worker.joinable())
    {
        state->worker.join();
    }

    const std::filesystem::path game_dir = normalize_dir_input(get_window_text_string(state->hDirCombo));
    optilord_installer::install_options opts{};
    opts.no_shortcut = (SendMessageW(state->hShortcutCheck, BM_GETCHECK, 0, 0) != BST_CHECKED);

    set_controls_for_busy(state, true);
    update_status_and_buttons(state);
    SendMessageW(state->hProgress, PBM_SETPOS, 0, 0);
    append_log_line(state->hLogEdit, L"---");

    const HWND hwnd = state->hwnd;
    state->worker = std::thread([hwnd, game_dir, opts]()
    {
        const optilord_installer::progress_fn on_progress = [hwnd](int percent, const std::wstring& status)
        {
            post_worker_progress(hwnd, percent, status);
        };
        const optilord_installer::log_fn on_log = [hwnd](const std::wstring& line)
        {
            post_worker_log(hwnd, line);
        };

        std::string error;
        const bool ok = optilord_installer::install_mod(game_dir, opts, on_progress, on_log, error);
        auto res = std::make_unique<worker_result>(worker_result{true, utf8_to_wide(error)});
        if (PostMessageW(hwnd, kMsgDone, ok ? 1 : 0, reinterpret_cast<LPARAM>(res.get())) != FALSE)
        {
            res.release();
        }
    });
}

void start_uninstall_worker(app_state* state)
{
    if (state->busy)
    {
        return;
    }

    if (MessageBoxW(
            state->hwnd,
            state->strings.confirm_uninstall_body,
            state->strings.confirm_uninstall_title,
            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    if (state->worker.joinable())
    {
        state->worker.join();
    }

    const std::filesystem::path game_dir = normalize_dir_input(get_window_text_string(state->hDirCombo));
    optilord_installer::uninstall_options opts{};
    opts.remove_diagnostics = (SendMessageW(state->hDiagCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    set_controls_for_busy(state, true);
    update_status_and_buttons(state);
    SendMessageW(state->hProgress, PBM_SETPOS, 0, 0);
    append_log_line(state->hLogEdit, L"---");

    const HWND hwnd = state->hwnd;
    state->worker = std::thread([hwnd, game_dir, opts]()
    {
        const optilord_installer::progress_fn on_progress = [hwnd](int percent, const std::wstring& status)
        {
            post_worker_progress(hwnd, percent, status);
        };
        const optilord_installer::log_fn on_log = [hwnd](const std::wstring& line)
        {
            post_worker_log(hwnd, line);
        };

        std::string error;
        const bool ok = optilord_installer::uninstall_mod(game_dir, opts, on_progress, on_log, error);
        auto res = std::make_unique<worker_result>(worker_result{false, utf8_to_wide(error)});
        if (PostMessageW(hwnd, kMsgDone, ok ? 1 : 0, reinterpret_cast<LPARAM>(res.get())) != FALSE)
        {
            res.release();
        }
    });
}

void launch_optilord(app_state* state)
{
    const std::filesystem::path game_dir = normalize_dir_input(get_window_text_string(state->hDirCombo));
    const std::filesystem::path exe_path = game_dir / L"overlord.exe";
    const HINSTANCE launched = ShellExecuteW(
        state->hwnd, L"open", exe_path.c_str(), nullptr, game_dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(launched) <= 32)
    {
        MessageBoxW(state->hwnd, state->strings.launch_err, state->strings.err_title, MB_ICONERROR | MB_OK);
    }
}

int scale_px(int px, UINT dpi) noexcept
{
    return MulDiv(px, static_cast<int>(dpi), 96);
}

void create_fonts(app_state* state, UINT dpi)
{
    state->title_font = CreateFontW(
        -MulDiv(13, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state->body_font = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state->bold_font = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state->mono_font = CreateFontW(
        -MulDiv(8, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
}

void destroy_fonts(app_state* state) noexcept
{
    auto free_font = [](HFONT& f) noexcept
    {
        if (f != nullptr)
        {
            DeleteObject(f);
            f = nullptr;
        }
    };
    free_font(state->title_font);
    free_font(state->body_font);
    free_font(state->bold_font);
    free_font(state->mono_font);
}

void create_controls(HWND hwnd, app_state* state)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    create_fonts(state, dpi);

    auto s = [dpi](int px) noexcept { return scale_px(px, dpi); };
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style,
                    int x, int y, int w, int h, int id, HFONT font) -> HWND
    {
        HWND ctrl = CreateWindowExW(
            ex_style, cls, text, WS_CHILD | WS_VISIBLE | style,
            s(x), s(y), s(w), s(h), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state->instance, nullptr);
        if (ctrl != nullptr && font != nullptr)
        {
            SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        return ctrl;
    };

    state->hTitleLabel = make(
        WC_STATICW, state->strings.banner_title, SS_LEFT | SS_NOPREFIX, 0,
        16, 14, 628, 26, kCtrlTitleLabel, state->title_font);

    state->hSubtitleLabel = make(
        WC_STATICW, state->strings.subtitle, SS_LEFT | SS_NOPREFIX, 0,
        16, 44, 628, 20, kCtrlSubtitleLabel, state->body_font);

    make(WC_STATICW, L"", SS_ETCHEDHORZ, 0, 16, 70, 628, 2, 0, nullptr);

    state->hDirLabel = make(
        WC_STATICW, state->strings.dir_label, SS_LEFT | SS_NOPREFIX, 0,
        16, 82, 628, 18, kCtrlDirLabel, state->body_font);

    state->hDirCombo = make(
        WC_COMBOBOXW, L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_TABSTOP | WS_VSCROLL, 0,
        16, 104, 524, 220, kCtrlDirCombo, state->body_font);

    state->hBrowseBtn = make(
        WC_BUTTONW, state->strings.browse_btn, BS_PUSHBUTTON | WS_TABSTOP, 0,
        548, 103, 96, 26, kCtrlBrowseBtn, state->body_font);

    state->hStatusLabel = make(
        WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, 0,
        16, 140, 628, 32, kCtrlStatusLabel, state->bold_font);

    state->hShortcutCheck = make(
        WC_BUTTONW, state->strings.shortcut_check, BS_AUTOCHECKBOX | WS_TABSTOP, 0,
        16, 178, 628, 22, kCtrlShortcutCheck, state->body_font);
    SendMessageW(state->hShortcutCheck, BM_SETCHECK, BST_CHECKED, 0);

    state->hDiagCheck = make(
        WC_BUTTONW, state->strings.diag_check, BS_AUTOCHECKBOX | WS_TABSTOP, 0,
        16, 204, 628, 22, kCtrlDiagCheck, state->body_font);

    state->hProgress = make(
        PROGRESS_CLASSW, L"", PBS_SMOOTH, 0,
        16, 234, 628, 20, kCtrlProgress, nullptr);
    SendMessageW(state->hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    state->hLogEdit = make(
        WC_EDITW, L"",
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
        WS_EX_CLIENTEDGE,
        16, 262, 628, 156, kCtrlLogEdit, state->mono_font);

    const DWORD install_btn_style =
        (state->uninstall_focus ? BS_PUSHBUTTON : BS_DEFPUSHBUTTON) | WS_TABSTOP;
    const DWORD uninstall_btn_style =
        (state->uninstall_focus ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) | WS_TABSTOP;

    state->hInstallBtn = make(
        WC_BUTTONW, state->strings.install_btn, install_btn_style, 0,
        16, 432, 188, 32, kCtrlInstallBtn, state->body_font);

    state->hUninstallBtn = make(
        WC_BUTTONW, state->strings.uninstall_btn, uninstall_btn_style, 0,
        214, 432, 136, 32, kCtrlUninstallBtn, state->body_font);

    state->hLaunchBtn = make(
        WC_BUTTONW, state->strings.launch_btn, BS_PUSHBUTTON | WS_TABSTOP, 0,
        360, 432, 172, 32, kCtrlLaunchBtn, state->body_font);

    state->hCloseBtn = make(
        WC_BUTTONW, state->strings.close_btn, BS_PUSHBUTTON | WS_TABSTOP, 0,
        542, 432, 102, 32, kCtrlCloseBtn, state->body_font);

    const auto detected = optilord_installer::detect_game_directories();
    for (const auto& dir : detected)
    {
        SendMessageW(
            state->hDirCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(dir.c_str()));
    }

    if (!state->initial_dir.empty())
    {
        const LRESULT existing = SendMessageW(
            state->hDirCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
            reinterpret_cast<LPARAM>(state->initial_dir.c_str()));
        if (existing != CB_ERR)
        {
            SendMessageW(state->hDirCombo, CB_SETCURSEL, static_cast<WPARAM>(existing), 0);
        }
        else
        {
            const LRESULT inserted = SendMessageW(
                state->hDirCombo, CB_INSERTSTRING, 0,
                reinterpret_cast<LPARAM>(state->initial_dir.c_str()));
            if (inserted != CB_ERR)
            {
                SendMessageW(state->hDirCombo, CB_SETCURSEL, static_cast<WPARAM>(inserted), 0);
            }
            else
            {
                SetWindowTextW(state->hDirCombo, state->initial_dir.c_str());
            }
        }
    }
    else if (!detected.empty())
    {
        SendMessageW(state->hDirCombo, CB_SETCURSEL, 0, 0);
    }

    append_log_line(state->hLogEdit, state->strings.initial_log);
    update_status_and_buttons(state);
}

void drain_pending_app_messages(HWND hwnd) noexcept
{
    MSG msg{};
    while (PeekMessageW(&msg, hwnd, kMsgProgress, kMsgDone, PM_REMOVE) != FALSE)
    {
        if (msg.message == kMsgProgress || msg.message == kMsgLog)
        {
            delete reinterpret_cast<std::wstring*>(msg.lParam);
        }
        else if (msg.message == kMsgDone)
        {
            delete reinterpret_cast<worker_result*>(msg.lParam);
        }
    }
}

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<app_state*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_NCCREATE:
    {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<app_state*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->hwnd = hwnd;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_CREATE:
        create_controls(hwnd, state);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        if (state == nullptr)
        {
            break;
        }
        const HWND ctrl = reinterpret_cast<HWND>(lParam);
        if (ctrl == state->hLogEdit)
        {
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        const HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        if (ctrl == state->hStatusLabel)
        {
            COLORREF color = RGB(30, 30, 34);
            switch (state->tone)
            {
            case status_tone::invalid_dir:
                color = RGB(180, 35, 24);
                break;
            case status_tone::legacy_upgrade:
                color = RGB(175, 95, 0);
                break;
            case status_tone::installed_clean:
                color = RGB(18, 120, 48);
                break;
            case status_tone::valid_uninstalled:
                color = RGB(24, 82, 150);
                break;
            case status_tone::neutral:
            default:
                break;
            }
            SetTextColor(hdc, color);
        }
        else if (ctrl == state->hSubtitleLabel)
        {
            SetTextColor(hdc, RGB(88, 88, 96));
        }
        else
        {
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        }
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    case WM_COMMAND:
    {
        if (state == nullptr)
        {
            break;
        }
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        if (id == kCtrlDirCombo)
        {
            if (code == CBN_SELCHANGE)
            {
                update_status_and_buttons(state, get_combo_selected_or_text(state->hDirCombo));
                return 0;
            }
            if (code == CBN_EDITCHANGE)
            {
                update_status_and_buttons(state);
                return 0;
            }
        }
        else if (id == kCtrlBrowseBtn && code == BN_CLICKED)
        {
            browse_for_folder(state);
            return 0;
        }
        else if (id == kCtrlInstallBtn && code == BN_CLICKED)
        {
            start_install_worker(state);
            return 0;
        }
        else if (id == kCtrlUninstallBtn && code == BN_CLICKED)
        {
            start_uninstall_worker(state);
            return 0;
        }
        else if (id == kCtrlLaunchBtn && code == BN_CLICKED)
        {
            launch_optilord(state);
            return 0;
        }
        else if ((id == kCtrlCloseBtn && code == BN_CLICKED) || id == IDCANCEL)
        {
            if (!state->busy)
            {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        break;
    }

    case kMsgProgress:
    {
        std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lParam));
        if (state != nullptr)
        {
            SendMessageW(state->hProgress, PBM_SETPOS, wParam, 0);
            if (status && !status->empty())
            {
                SetWindowTextW(state->hStatusLabel, status->c_str());
                state->tone = status_tone::neutral;
                InvalidateRect(state->hStatusLabel, nullptr, TRUE);
            }
        }
        return 0;
    }

    case kMsgLog:
    {
        std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
        if (state != nullptr && line && !line->empty())
        {
            append_log_line(state->hLogEdit, *line);
        }
        return 0;
    }

    case kMsgDone:
    {
        std::unique_ptr<worker_result> res(reinterpret_cast<worker_result*>(lParam));
        if (state != nullptr)
        {
            if (state->worker.joinable())
            {
                state->worker.join();
            }
            set_controls_for_busy(state, false);
            const bool ok = (wParam != 0);
            SendMessageW(state->hProgress, PBM_SETPOS, ok ? 100 : 0, 0);
            update_status_and_buttons(state);

            if (ok)
            {
                if (res && res->is_install)
                {
                    append_log_line(state->hLogEdit, state->strings.done_install_body);
                    MessageBoxW(
                        hwnd, state->strings.done_install_body, state->strings.done_install_title,
                        MB_ICONINFORMATION | MB_OK);
                }
                else
                {
                    append_log_line(state->hLogEdit, state->strings.done_uninstall_body);
                    MessageBoxW(
                        hwnd, state->strings.done_uninstall_body, state->strings.done_uninstall_title,
                        MB_ICONINFORMATION | MB_OK);
                }
            }
            else
            {
                const std::wstring err = (res && !res->error.empty()) ? res->error : L"Operation failed.";
                append_log_line(state->hLogEdit, L"ERROR: " + err);
                MessageBoxW(hwnd, err.c_str(), state->strings.err_title, MB_ICONERROR | MB_OK);
            }
        }
        return 0;
    }

    case WM_CLOSE:
        if (state != nullptr && state->busy)
        {
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state != nullptr)
        {
            if (state->worker.joinable())
            {
                state->worker.join();
            }
            drain_pending_app_messages(hwnd);
            destroy_fonts(state);
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int run_gui(HINSTANCE hInstance, int nCmdShow, const std::filesystem::path& initial_dir, bool uninstall_focus)
{
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool com_ready = SUCCEEDED(com_hr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    app_state state{};
    state.instance = hInstance;
    state.strings = make_ui_strings();
    state.self_exe = get_self_exe_path();
    state.initial_dir = initial_dir;
    state.uninstall_focus = uninstall_focus;
    state.payload_available =
        !state.self_exe.empty() && optilord_installer::has_embedded_zip_payload(state.self_exe);

    constexpr wchar_t kWindowClass[] = L"OptiLordInstallerWindowClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = main_wnd_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(kIconId));
    if (wc.hIcon == nullptr)
    {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(kIconId), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (wc.hIconSm == nullptr)
    {
        wc.hIconSm = wc.hIcon;
    }
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = kWindowClass;

    if (RegisterClassExW(&wc) == 0)
    {
        if (com_ready)
        {
            CoUninitialize();
        }
        return 1;
    }

    constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const UINT sys_dpi = GetDpiForSystem();
    RECT rc{0, 0, scale_px(660, sys_dpi), scale_px(480, sys_dpi)};
    AdjustWindowRectExForDpi(&rc, kWindowStyle, FALSE, 0, sys_dpi);

    const int win_w = rc.right - rc.left;
    const int win_h = rc.bottom - rc.top;
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int pos_x = work_area.left + ((work_area.right - work_area.left) - win_w) / 2;
    const int pos_y = work_area.top + ((work_area.bottom - work_area.top) - win_h) / 2;

    HWND hwnd = CreateWindowExW(
        0, kWindowClass, state.strings.window_title, kWindowStyle,
        pos_x, pos_y, win_w, win_h, nullptr, nullptr, hInstance, &state);
    if (hwnd == nullptr)
    {
        if (com_ready)
        {
            CoUninitialize();
        }
        return 1;
    }

    const UINT win_dpi = GetDpiForWindow(hwnd);
    if (win_dpi != sys_dpi && win_dpi > 0)
    {
        RECT adj{0, 0, scale_px(660, win_dpi), scale_px(480, win_dpi)};
        AdjustWindowRectExForDpi(&adj, kWindowStyle, FALSE, 0, win_dpi);
        SetWindowPos(
            hwnd, nullptr, 0, 0, adj.right - adj.left, adj.bottom - adj.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (state.uninstall_focus && IsWindowEnabled(state.hUninstallBtn))
    {
        SetFocus(state.hUninstallBtn);
    }
    else if (IsWindowEnabled(state.hInstallBtn))
    {
        SetFocus(state.hInstallBtn);
    }
    else
    {
        SetFocus(state.hBrowseBtn);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (com_ready)
    {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    int argc = 0;
    LPWSTR* raw_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (raw_argv != nullptr)
    {
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i)
        {
            args.emplace_back(raw_argv[i]);
        }
        LocalFree(raw_argv);
    }

    if (args.size() >= 2)
    {
        if (args[1] == L"--uninstall-ui")
        {
            std::filesystem::path preselected;
            if (args.size() >= 3)
            {
                preselected = normalize_dir_input(args[2]);
            }
            return run_gui(hInstance, nCmdShow, preselected, true);
        }
        return run_cli(args);
    }

    return run_gui(hInstance, nCmdShow, {}, false);
}
