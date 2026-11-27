# OptiLord — Optimized VR Edition for MW2 Campaign Remastered (Based on Overlord Beta 8)

**OptiLord** is an independently maintained community performance fork (**`v0.6.0`**; release lineage: `v0.1.0` → `v0.2.0` → `v0.3.0` → `v0.4.0` → `v0.5.0` → `v0.6.0`) by **[TechnoidVR](https://github.com/Technoidvr/cod-mw2-remastered-vr-mod-optimisation)** for *Call of Duty: Modern Warfare 2 Campaign Remastered* VR, built on **Overlord VR Beta 8**.

Huge thanks to **[Aeka0](https://github.com/Aeka0)**, original creator of **[Overlord VR](https://github.com/Aeka0/Overlord)**, for the foundational VR mod without which this project would not exist.

> **SteamVR compatibility note:** To preserve existing SteamVR profiles, manifests, and controller bindings, the game client executable remains named **`overlord.exe`** (`application_key: "overlord"`, manifest `overlord.vrmanifest`, launcher/SteamVR display title `OptiLord (Overlord VR)`).

## Highlights & Performance Architecture

OptiLord focuses on maximizing CPU/GPU frame pacing (especially on Ryzen processors and AMD/NVIDIA DX11 drivers), cutting memory/TLS overhead, eliminating texture-streaming stutters, and improving VR gameplay reliability while staying compatible with **Overlord Beta 8** (see [CHANGELOG.md](CHANGELOG.md), [OPTIMIZATIONS.md](OPTIMIZATIONS.md), and [OPTIMIZATIONS_FR.md](OPTIMIZATIONS_FR.md)):

- **Dual Shoulder Holsters, Trigger Zone Fixes & Multi-Zone Hover Haptics (`v0.6.0`)**: Independent left and right shoulder holsters (`left_back` + `right_back`, configurable via **Shoulder holster width** / `vr_holsterBackWidth` across all 9 launcher languages) to carry two two-handed / heavy weapons simultaneously, torso-yaw-aligned body pose for 180° room-scale turns and downward head tilt (`body_pose.hpp`), unblocked body holstering near walls, unified waist/underbarrel distance checks with `vr_supportGripLock` waist auto-unlock, tighter chest/NVG trigger arbitration (Foregrip `16`, NVG `18`, Chest/Abdomen `20`), `+4 cm` Quick Reload and `+0.35 m / +8°` world `use_lease` hysteresis, and distinct hover-readiness haptics across all body zones (`zone_hover_feedback`).
- **Clean 1-Click Installer & Uninstaller (`v0.5.0`)**: Self-extracting Windows Setup & Uninstaller (`OptiLord-v0.6.0-Setup.exe`) that auto-detects default MW2CR install folders (Battle.net config, Steam/Epic/common drive libraries, Windows Uninstall registry, and custom browse), automatically wipes obsolete/stale Overlord & OptiLord binaries (`overlord.exe`, `overlord.pdb`, `h2-mod/`, `steamvr/`, `vr_input/`, etc.) before installing to eliminate stale-binary bugs while preserving game files and campaign saves (`players2/`), and installs a lightweight `OptiLord-Uninstall.exe` (`HKCU` Windows Apps entry + Desktop shortcut toggle) for 1-click removal.
- **CPU Contention & Frame Pacing (`v0.4.0`)**: Telemetry-driven fixes from real Meta Quest 3 / RX 9070 / Ryzen 7 5700X captures: lock-free `requested_enabled()`, `applied_enabled()`, and `poll_capture()` in `runtime_backend.cpp`, `openxr_runtime.cpp`, and `openvr_runtime.cpp` (eliminates measured `1.0–4.5 ms` / max `87.5 ms` Frontend/Renderer stalls waiting on `xrWaitFrame` holding `mutex_`), one-time `THREAD_PRIORITY_ABOVE_NORMAL` pinning on the Present/Backend and Renderer/Frontend threads (`vr_component.cpp`), and a zero-observer fast-path in `engine_stereo_resource_ops.cpp` skipping `_ReturnAddress()` and 80-byte event construction across 500,000+ D3D11 copy/update calls when no observer is active.
- **GPU Render Scale, FidelityFX CAS & OpenXR Hidden Area Mesh (`v0.3.0`)**: Decoupled VR scene render resolution (`vr_renderScale`, 50%–120%) keeping the OpenXR swapchain and VR HUD sharp, 5-tap linear-space AMD FidelityFX CAS sharpening (`vr_sharpening`, 0%–100%) in `texture_blit`, and OpenXR `XR_KHR_visibility_mask` Hidden Area Mesh depth pre-stamping (`vr_hiddenAreaMesh`) in reverse-Z depth to skip invisible lens-corner pixels.
- **Dense-Scene CPU Hot-Path Optimizations (`v0.2.0` — *Operation Wolverines*)**: Removed per-target `d3d11::get_device_snapshot()` lock/COM churn and unused `get_current_binding()` + bypassed post-bootstrap probes via `bootstrap_complete()` (`engine_stereo_renderer.cpp`), avoided `OMGetRenderTargets` on ~95% of non-display targets (`engine_stereo_owner_pass.cpp`), added an 8-entry lock-free `non_matching_views` sun-shadow ring (`engine_stereo_sun_shadow.cpp`), armed HUD `copy_draw` conditionally to short-circuit 99.9% of 3D `DrawIndexed` calls (`native_hud_capture.cpp` / `engine_stereo_draw_indexed.cpp`), and streamlined `active_isolated_eye()` + `rewrite_admitted_view_synced()` (`engine_stereo_eye_resources.cpp`).
- **Dedicated Identity & Streaming Anti-Stutter (`v0.1.0`)**: Lock-free seqlock `watched_resources` filtering so background texture streaming no longer flushes `cache.foreign`, SEH `__try`/`__except` copy + `identity_count` replacing `ReadProcessMemory` syscalls in `texture_diagnostics.cpp`, cached Bnet file sizes + `std::mutex` synchronization in `database.cpp`, and 9-language launcher rebranding.
- **Included Phases 1–4 DX11 / OpenXR / Memory & VR Gameplay Fixes**: Direct `CopyResource` OpenXR eye blit, dirty-checked D3D11 isolation `Set*` calls, merged single-`CommandList` spatial panel rendering, lazy storage (`SizeOfImage` reduced from **282.9 MB to 111.7 MB**, TLS template reduced by **98.2%**), `DXGI_FORMAT_R11G11B10_FLOAT` swapchain priority, single-`Flush()` stereo submission, `1 ms` timer + `ABOVE_NORMAL_PRIORITY_CLASS`, room-scale doorway collision sync, assault-rifle shoulder/back holster + haptics, and widened belt ammo pouch / charging handle grab tolerances.

## Version History

| OptiLord Version | Upstream Base | Key Highlights |
| --- | --- | --- |
| **`v0.6.0`** *(current)* | Overlord Beta 8 | **Dual Shoulder Holsters, Trigger Zone Fixes & Hover Haptics**: independent left/right shoulder holsters (`vr_holsterBackWidth` in all 9 languages) to carry two two-handed weapons, 180°/downward-tilt torso pose tracking, unblocked wall-adjacent holstering, waist/underbarrel/NVG/chest priority arbitration, Quick Reload & `use_lease` exit hysteresis, and distinct multi-zone hover readiness haptics. |
| **`v0.5.0`** | Overlord Beta 8 | **Clean 1-Click Installer & Uninstaller**: self-extracting `OptiLord-v0.5.0-Setup.exe` with MW2CR auto-detection (Battle.net, Steam/Epic/common libraries, Uninstall registry, custom browse), automatic stale-binary purge before upgrade (preserving game files and `players2/` saves), lightweight `OptiLord-Uninstall.exe`, `HKCU` Windows Apps entry, and Desktop shortcut toggle. |
| **`v0.4.0`** | Overlord Beta 8 | **CPU Contention & Frame Pacing**: lock-free `requested_enabled()`, `applied_enabled()`, and `poll_capture()` eliminating Frontend/Renderer stalls waiting behind `xrWaitFrame`, `THREAD_PRIORITY_ABOVE_NORMAL` pinning on Present/Backend & Renderer/Frontend threads, and zero-observer D3D11 copy/update fast-path. |
| **`v0.3.0`** | Overlord Beta 8 | **GPU Render Scale, FidelityFX CAS & Hidden Area Mesh**: decoupled `vr_renderScale` (50%–120%), linear-space AMD FidelityFX CAS `vr_sharpening` (0%–100%), OpenXR `XR_KHR_visibility_mask` (`vr_hiddenAreaMesh`) reverse-Z depth stamping, and English-first documentation/releases. |
| **`v0.2.0`** | Overlord Beta 8 | **Dense-Scene CPU Hot-Path Optimizations (*Operation Wolverines*)**: lock-free target selection & post-bootstrap probe bypass, ~95% fewer non-display `OMGetRenderTargets` calls, 8-entry lock-free sun-shadow negative ring, conditional HUD `copy_draw` short-circuiting 99.9% of 3D `DrawIndexed` calls, streamlined `active_isolated_eye()` + `rewrite_admitted_view_synced()`. |
| **`v0.1.0`** | Overlord Beta 8 | **OptiLord Identity & Streaming Anti-Stutter**: lock-free `watched_resources` seqlock, SEH copy in `texture_diagnostics.cpp`, cached file sizes + mutex in `database.cpp`, Phase 4 DX11/OpenXR/HUD optimizations, Phases 1–3 memory/CPU/OpenXR gains, and VR gameplay fixes. |

## Quick Install Guide

1. Make sure you have a complete, legally owned installation of *Call of Duty: Modern Warfare 2 Campaign Remastered* (exit the game and close **MSI Afterburner** / RivaTuner).
2. Choose an installation option from the **[Releases](https://github.com/Technoidvr/cod-mw2-remastered-vr-mod-optimisation/releases)** page:
   - **Option 1 (Recommended): 1-Click Installer (`OptiLord-v0.6.0-Setup.exe`)** — Run `OptiLord-v0.6.0-Setup.exe`, confirm or browse to your MW2CR root folder (beside `MW2CR.exe`), optionally toggle the Desktop shortcut, and install. Obsolete/stale Overlord and OptiLord binaries are wiped cleanly while game data and `players2/` campaign saves stay untouched, and `OptiLord-Uninstall.exe` is registered in Windows Apps (`HKCU`) for 1-click removal.
   - **Option 2: Manual Archive (`OptiLord-v0.6.0.7z`)** — Extract **all contents** directly into your MW2CR root folder (beside `MW2CR.exe`), keeping the directory structure (`overlord.exe`, `openxr_loader.dll`, `overlord.vrmanifest`, `h2-mod/`, `steamvr/`, `vr_input/`). If upgrading manually over an older build, remove obsolete `overlord.exe`, `overlord.pdb`, `h2-mod/`, `steamvr/`, and `vr_input/` files first. *Reminder: the executable remains named **`overlord.exe`** for SteamVR compatibility.*
3. Power on your VR headset and wake both controllers (**OpenXR** is recommended — e.g., Virtual Desktop/VDXR or Steam Link/SteamVR; **OpenVR** is available as a backup in VR Settings).
4. Launch **`overlord.exe`** (or the Desktop shortcut), disable risky game settings if needed (*Shader Preloading*, *SSAA*, shadow caches — available as a one-click option under *VR Settings > Debug* or prior to launch), and click **Play** / **Singleplayer**.

## Recommended Settings for Dense Missions (e.g., *Wolverines!*)

In the launcher, open **VR Settings → Basics → Performance**:

| Setting (`Dvar`) | Recommended Value | Effect & Behavior |
| --- | --- | --- |
| **Render scale** (`vr_renderScale`) | **`80%` – `90%`** | Scales internal 3D stereo scene resolution (`50%`–`120%`) for major GPU relief in heavy outdoor scenes while keeping the OpenXR swapchain and VR HUD sharp. Applies on restart or `vr_reinit`. |
| **FidelityFX CAS sharpening** (`vr_sharpening`) | **`35%` – `50%`** | Applies 5-tap linear HDR AMD FidelityFX Contrast Adaptive Sharpening (`0%`–`100%`) to restore crisp detail after upscaling. Applies live with no restart. |
| **Hidden area mesh** (`vr_hiddenAreaMesh`) | **Enabled** *(default)* | Pre-stamps the OpenXR headset lens visibility mask (`XR_KHR_visibility_mask`) into reverse-Z depth to skip shading invisible peripheral pixels. |

Documentation: **[CHANGELOG.md](CHANGELOG.md)** · **[OPTIMIZATIONS.md](OPTIMIZATIONS.md)** *(English)* · **[OPTIMIZATIONS_FR.md](OPTIMIZATIONS_FR.md)** *(Français)*

---

# Overlord (Upstream README)

English | [简体中文](README.zh-CN.md)

![Overlord](assets/steamvr/cover.png)

Bilibili/Aeka0: https://space.bilibili.com/10077845

Reposting is welcome; please include a link to this GitHub Repo.

Overlord is a VR mod for *Call of Duty: Modern Warfare 2 Campaign Remastered*. It adds native stereoscopic rendering, 6DOF head and hand tracking, physical weapon handling, and VR interactions for equipment, environments, and story sequences to the original campaign. The adaptations build on the game's existing weapon, ammunition, animation, and mission logic, preserving the original story presentation wherever possible.

### **Overlord is maintained by one person and is still in Beta, with no stable release yet. The project is undergoing intensive development. Many features have only been tested on the author's own machine, substantial compatibility and adaptation work remains, and stability is expected to be poor.**

### **Please keep expectations low and treat the current version as a test rather than a mature product. Please also avoid comparing it with projects that have benefited from a long period of refinement. Crashes and missing visuals have already been reported on many machines, and these problems are still being investigated and fixed.**

### **If you want a version that you can install and start playing with relatively little trouble, please wait for the stable release. If you would like to help improve stability, you are welcome to download, test, and report problems through [Issues](https://github.com/Aeka0/Overlord/issues). Thank you for your interest and support.**

### **You may still encounter adaptation issues in some scenes or story scripts during play. Custom mods made for the non-VR client are not expected to work with Overlord. Large code changes and refactoring may still take place, so developing mods at this stage is not recommended. Version updates may also break game saves and progress.**

## Known issues:

- If you encounter a `Create2DTexture` error, try disabling Shader Preloading in the game's graphics settings.
- MSI Afterburner may prevent Overlord from starting. This issue is still under investigation; for now, exit MSI Afterburner before launching Overlord.

## Main Features

- Stereoscopic rendering and head tracking: look around the game world through your headset and play the campaign in stereo.
- Weapon handling with both hands: hold and aim weapons with your controllers, with support for an off-hand grip, body holsters, and picking up weapons from the world.
- Physical reloading and chambering: operate magazines, slides, charging handles, magazine tubes, cylinders, or feed mechanisms according to each weapon's design. Different weapons retain their own handling procedures.
- Equipment and world interactions: adaptations for throwables, melee equipment, night vision goggles, and selected mission props.
- Campaign and story adaptations: VR controls and view adjustments for vehicles, mounted weapons, climbing, and scripted camera sequences.
- VR interface and settings: the native game interface presented in VR, plus a desktop launcher with English and Chinese interfaces, a first-time setup guide, and control help.
- Comfort and assistance options: adjust turning, controller alignment, selected visual effects, and assistance features, and configure the desktop spectator view.
- Optional features: players can adjust recoil, quick reloading, aim assistance, enemy melee damage, and other options.

## Requirements

- 64-bit Windows 10 or 11.
- A legally owned and installed copy of *Call of Duty: Modern Warfare 2 Campaign Remastered*.
- SteamVR and VR hardware that provides head and hand tracking through SteamVR.

The client uses Direct3D 11 and defaults to OpenXR, with OpenVR retained as a manually selected backup. The validated combination is Meta Quest controllers with SteamVR/OpenXR; other devices and runtimes require separate acceptance. Choose it under the launcher's VR Settings > Basics > VR backend; it applies when entering the game without restarting the launcher. See [runtime and rendering contracts](docs/vr-runtime-rendering.md) for selection and limitations.

Controller poses default to Legacy. The Standard pipeline using OpenXR coordinates remains a manually selected trial and requires separate headset acceptance. Select the pipeline under VR Settings > Calibration > Controller pose pipeline, then restart the game to apply it. Each pipeline retains its own calibration. Existing custom calibration remains in the legacy bank; choose a preset or adjust the standard bank separately. The position and angle descriptions list the selected pipeline's console commands for live in-game calibration.

The repository includes default input bindings for Oculus Touch (Meta Quest), Valve Index, and Vive Controller. Meta Quest controllers are the primary validated devices; Index controllers have also been tested in OpenVR, as described below. Other combinations may require adjustments to runtime bindings and controller alignment settings.

Pirated or cracked copies of the game are not supported. Mod releases do not include the full original game assets or provide download links for the game.

## Installation and First Use

When Beta and stable releases are published, installation packages will be available on the [Releases page](https://github.com/Aeka0/Overlord/releases). Before a release is available, refer to the source build instructions below.

1. Make sure the original game installation is complete.
2. Extract the VR client package into the game's root directory and run `overlord.exe`.
3. Start SteamVR, make sure your headset and controllers are connected, then click "Singleplayer".

When using the OpenVR backup, the client registers Overlord with SteamVR after its first successful connection. The standard and Debug builds register separately, with their own launch entries.

Keep the client and its resources in a layout similar to this:

~~~text
Game directory/
├─ overlord.exe
├─ openxr_loader.dll
├─ overlord.vrmanifest
├─ steamvr/
├─ vr_input/
└─ h2-mod/
~~~

Building the client does not automatically compile ZoneTool assets into fastfiles. Before a release, the source of the base data, package contents, and complete installation procedure still need to be confirmed. Developers can refer to the release preparation documentation.

## Basic Controls

Button names vary by controller and runtime binding. The following uses the common names Grip and Trigger:

| Action | Basic method |
| --- | --- |
| Move | Use the left stick. |
| Turn | Use the right stick. Choose smooth turning or snap turning in settings. |
| Hold a weapon | Move your hand near the appropriate grip point and hold Grip. |
| Fire | Hold the weapon's firing grip and squeeze Trigger with that hand. |
| Handle ammunition and parts | Use Trigger with a free hand to interact with magazines, ammunition, slides, or charging handles. The action depends on the weapon's design. |
| Holster a weapon | Move the weapon to a compatible body holster and release Grip. |

Magazine removal, loading, and chambering differ between weapons. For example, some weapons use a button to release the magazine, while others require you to pull it out manually. Pump-action shotguns, revolvers, and break-action weapons also have their own loading procedures. If the chamber is empty, you may still need to operate the slide, charging handle, or corresponding feed mechanism. Follow the instructions for the specific weapon in the launcher's "Help" page.

### Valve Index controllers (OpenVR)

The default Knuckles binding uses the physical thumbsticks for movement and
turning. Click the left thumbstick to sprint and the right to jump; right-stick
vertical movement uses the configured stance controls. Tap left A to pause/back,
or hold it to recenter. Right A shows the weapon HUD, and B uses the corresponding
hand's secondary weapon action. Squeeze the grip to hold and relax to release.
Firing responds to trigger travel before the mechanical click; capacitive trigger
touch remains a separate input.

The merged Beta 4 build was tested in-game with Index controllers and a Pimax
Dream Air in OpenVR. This does not establish OpenXR or general device acceptance. An existing
custom SteamVR binding can override the shipped defaults; select the default
Knuckles binding to use these mappings.

## Settings and Usage Tips

For your first session, configure controller alignment and turning before adjusting other options to suit your preferences. The first-time setup guide saves valid selections immediately; changes on the full "VR Settings" page are applied with the Save button.

The launcher offers visual, interaction, assistance, and cheat options. Get familiar with the default controls first, then adjust these as needed. Debug options are intended for troubleshooting and are usually unnecessary during normal play.

Launcher language and game language are managed separately. Switching the launcher's English or Chinese interface does not download or switch the original game's language assets.

Install updates manually from this project's [Releases page](https://github.com/Aeka0/Overlord/releases). The current VR version does not have an automatic update service.

## Current Status and Limitations

The project is still in development and release preparation.

- All interactions have been checked only on the developer's own machine, so coverage across all systems cannot be claimed. Support may vary between weapons, attachments, mission props, and story scenes.
- Device compatibility and performance need to be verified with the actual headset, controllers, runtime environment, and game scene. Device-specific adaptation has so far focused on Meta Quest 3.

The project does not currently publish unverified minimum hardware specifications, make performance promises, or guarantee an issue-free campaign. Confirmed compatibility, known issues, and version changes will be listed in release notes.

## Building from Source

Clone the repository with Git and initialize its submodules. Downloading GitHub's source ZIP does not include the required submodule contents.

Install Visual Studio's Desktop development with C++ tools and the Windows SDK, then run the following in the appropriate Developer Command Prompt:

~~~bat
git clone --recurse-submodules https://github.com/Aeka0/Overlord.git Overlord
cd Overlord
generate.bat
msbuild build\overlord.sln /t:client /m:2 /p:Configuration=RelWithDebInfo /p:Platform=x64 /p:PreferredToolArchitecture=x64 /p:CL_MPCount=4
~~~

`generate.bat` generates a Visual Studio 2022 project. The default toolset is v143. For an existing Visual Studio 2019 v142 toolchain, generate with `tools\premake5.exe vs2019`, use its x64 MSBuild, and specify `/p:PlatformToolset=v142`.

| Purpose | Build configuration | Client file |
| --- | --- | --- |
| Normal play and performance checks | `RelWithDebInfo` | `overlord.exe` |
| Development and troubleshooting | `Debug` | `overlord-debug.exe` |

Build output is placed in `build/bin/x64/<Configuration>/`. Use the executable and resources produced by the same build; do not mix mismatched versions.

See the [development guide](docs/development.md) for build details, checks appropriate to each change, and resource deployment. Other technical topics are listed in the [documentation index](docs/README.md). These developer documents are currently written mainly in English.

## Reporting Issues and Contributing

Search [existing issues](https://github.com/Aeka0/Overlord/issues) before submitting a report. Reports in English or Chinese are welcome.

While the problem is visible, press **Ctrl+Shift+F8** with the game window selected,
or run **`vr_diagnose`** in Overlord's console. After about five seconds, Explorer
opens with a diagnostic ZIP selected. Attach that one file and briefly describe
what happened and the mission/checkpoint or interaction. You do not need to copy
console text or manually collect versions and settings. If the game has already
crashed, attach its generated crash ZIP from `minidumps`. See the
[diagnostic guide](docs/vr-diagnostics-workflow.md) for capture contents and limits.

Do not upload game files, access credentials, or unchecked full memory dumps to public issues. See the [security policy](SECURITY.md) for reporting issues that involve sensitive personal information.

Read the [contributing guide](CONTRIBUTING.md) before working on the project. Changes should reuse existing components where possible, preserve the responsibility boundaries of native game logic, and clearly distinguish source checks, automated tests, and validation on actual hardware.

## License and Acknowledgments

The project code is licensed under [GNU GPLv3](LICENSE). Third-party code, fonts, and other resources retain their respective licenses and copyright notices. See the [third-party notices](THIRD_PARTY_NOTICES.md) and [source provenance](docs/source-provenance.md).

The VR adaptation is independently maintained by [Aeka0](https://github.com/Aeka0). This fork is not affiliated with or endorsed by the upstream project or its authors and do not disturb them. Maintenance and support for the VR changes are handled by this project.

The underlying code builds on H2-Mod and its earlier upstream projects, [IW6x](https://git.alterware.dev/alterware/iw6-mod) and [S1x](https://git.alterware.dev/alterware/s1-mod).

Thanks also to the following projects and contributors:
[momo5502](https://github.com/momo5502),
[JariKCoding](https://github.com/JariKCoding/CoDLuaDecompiler),
[xensik](https://github.com/xensik/gsc-tool),
[ZoneTool](https://github.com/ZoneTool/zonetool),
[quaK](https://github.com/Joelrau), and the related work of Valve, Khronos, and REFramework.

This is an independent community mod, not an official game release. Game names and trademarks belong to their respective rights holders. The project is intended for academic research and must not be used for piracy or attacks that circumvent copy protection. Users are responsible for any misuse.
