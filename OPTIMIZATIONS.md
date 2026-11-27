# OptiLord (Based on Overlord VR Beta 8) — Technical Optimization & VR Fix Reference

> **Version française :** [OPTIMIZATIONS_FR.md](OPTIMIZATIONS_FR.md)

This document details the performance optimizations (CPU, GPU, Memory, D3D11, OpenXR, Lua/GSC) and VR gameplay fixes included in **OptiLord** on top of **Overlord Beta 8** for *Call of Duty: Modern Warfare 2 Campaign Remastered*.

## 1. Memory & PE Executable Optimizations (`overlord.exe`)
- **Migration to lazy allocations (`vr::lazy_box<T>`, `vr::lazy_array<T, N>`)** in `src/client/component/vr/lazy_storage.hpp`:
  - `SizeOfImage` reduced from **282.9 MB to 111.7 MB** (**−171.2 MB** virtual footprint per process).
  - `.data` section reduced from **264.0 MB to 92.3 MB**.
  - Raw per-thread TLS template reduced from **2.58 MB to 46.2 KB** (**−98.2%**, eliminating ~2.5 MB of allocation and initialization overhead on every Windows / XAudio2 / D3D11 / Worker thread creation).

## 2. Single-Core CPU (Ryzen 5000 / 5700X) & DX11 Driver (`atidxx64.dll` / NVIDIA) Optimizations
- **MSVC `v145` compiler flags** (`premake5.lua`): `/O2 /Oi /Ot /Ob3 /Gw /arch:AVX2 /bigobj` + 32-byte AVX2 signature scanner (`src/common/utils/signature.cpp`).
- **Eliminated double full D3D11 readback per eye** (`src/client/component/vr/engine_stereo_eye_resources.cpp`):
  - Eliminated redundant `count_bound_target_views()` calls in `rebind_inherited_state` and `restore_inherited_state` and introduced the `cache.foreign` negative cache, saving **3,072 to 4,608 SRV reads + 512 to 768 UAV reads per frame** inside the DX11 driver.
- **Per-eye/route COM validation cache in `end_view()`** (`src/client/component/vr/engine_stereo_owner_pass.cpp`):
  - Avoids 7 COM queries (`GetResource`, `QueryInterface`, `GetDevice`, `GetDesc`) per eye under `h2_gpu_context_lock`.
- **Power-of-two open-addressing hash tables & D3D11 short-circuits** (`engine_stereo_pointer_cache.hpp`, `engine_stereo_output_merger.cpp`, `engine_stereo_execution.cpp`, `engine_stereo_draw_indexed.cpp`, `engine_stereo_sun_shadow.cpp`).

## 3. OpenXR, Swapchain & VR Shader Optimizations
- **32-bit HDR format priority `DXGI_FORMAT_R11G11B10_FLOAT`** (`openxr_d3d11.hpp`, `openxr_runtime.cpp`): cuts GPU memory bandwidth for left/right eye copies by **50%** compared to `R16G16B16A16_FLOAT`.
- **Batched stereo submission with a single GPU `Flush()`** (`render_eyes` in `openxr_runtime.cpp`, `openvr_runtime.cpp`, `vr_component.cpp`).
- **Enabled `XR_EXT_performance_settings`** (`PERF_LEVEL_SUSTAINED_HIGH` for CPU & GPU).
- **2D VSync decoupling (`SyncInterval = 0`)** while VR session is active (`d3d11.cpp`, `swap_chain_hook.cpp`, `vr_component.cpp`) + lock-free desktop mirror `IsIconic`.
- **Optimized spatial panel shaders** (`spatial_panel_renderer.cpp`: 4-tap / 9-tap bilinear blur instead of 9-tap / 25-tap).
- **Incremental Lua GC & GSC caches** (`data/ui_scripts/vr_gameplay/__init__.lua`, `data/cdata/scripts/killfeed/__init__.lua`, `data/cdata/scripts/killfeed_ducking.gsc`).

## 4. VR Gameplay Fixes (Doorways/Collisions, Holsters & Reloading)
- **VR Visual Camera / CoD Physical Collision Capsule Sync** (`roomscale_origin.hpp`, `head_pose_bridge.hpp`, `head_pose_bridge.cpp`, `controller_component.cpp`):
  - Automatically recenters the physical collision capsule (`playerState_s::origin` in `Pmove`) under the VR headset whenever horizontal offset exceeds `6 cm`, while compensating `turn_offset_meters` via `roomscale.shift_offset()` so the visual camera stays 100% stable. Eliminates the issue forcing players to visually clip into wall/doorframe textures to pass through doorways.
- **Holster Reliability (Stowing & Drawing)** (`weapon_carry_runtime.cpp`):
  - Filtered incompatible slots during stowing (`eligible_slots`: long weapons can no longer be intercepted and rejected by a hip holster).
  - Automatic support-hand detachment (`vr_supportGripLock` / Toggle mode) when stowing into a valid holster.
  - Dual Torso + Head and Hand + calibrated Wrist checks (`+4 cm` tolerance) for both stowing and drawing.
- **Magazine Grab & Physical Reload Reliability** (`hand_interaction/frame.hpp`, `weapon_carry_runtime.cpp`, `physical_reload_runtime.cpp`, `physical_reload_contact_sample.hpp`):
  - Automatic support-hand detachment when the off-hand moves away from the forend (`> 18 cm`) and pulls `Trigger` to grab a magazine or operate the bolt/slide.
  - Expanded in-game ammo zone along torso axes and the front of the belt / lower tactical vest (`+12 cm` forward, `+8 cm` up, `+4 cm` radius).
  - Extra margin (`+2.5 cm`) for grabbing charging handles / bolts and inserted magazines.

## 5. Shoulder/Back Holster Fix, Phase 3 Optimizations & Fork Rebranding
- **Critical fix for stowing assault rifles / long weapons on the shoulder/back** (`weapon_carry_runtime.cpp`):
  - Fixed the bitmask passed to `weapon_holsters::hit()` (`1u << 2 == 4` instead of `1u << unsigned(location::back) == 16`, which had blocked stowing all long weapons).
  - Synchronized shoulder haptic feedback (`finish_interactions()`) with the expanded volumes, both head/torso reference frames, and automatic support-hand release (`vr_supportGripLock`).
- **Additional Phase 3 Optimizations**:
  - Ultra-fast per-pair D3D11 target pointer validation in `engine_stereo_eye_resources.cpp::begin_pair()` (avoids ~30 COM calls under GPU lock each frame).
  - Disabled the 68-entry per-eye draw-list diagnostic census in production mode in `engine_stereo_owner_pass.cpp::end_view()` (retained only when `vr_debugViewProbes` is active).
  - Removed redundant per-frame `GetDevice()` COM call in `desktop_mirror.cpp::prepare()`.
  - Enabled `1 ms` system timer resolution (`timeBeginPeriod(1)`) and `ABOVE_NORMAL_PRIORITY_CLASS` process priority (`vr_component.cpp`) to stabilize OpenXR/OpenVR pacing on Ryzen.
- **Launcher Rebranding** (`src/client/product.hpp`, `launcher.cpp`, `credits.json`, `locales/*.json`, `src/launcher-ui/`):
  - Clear attribution to the **TechnoidVR performance fork** on home, header, startup notice, and About page, with a warm tribute and direct link to Aeka0's original **Overlord VR** repository (`https://github.com/Aeka0/Overlord`).

## 6. Phase 4 Optimizations (D3D11 / OpenXR / HUD & Spatial Panels)
- **Shaderless & CommandList-free OpenXR Blit in Normal Gameplay (`texture_blit.hpp`, `texture_blit.cpp`)**:
  - Added a direct `CopyResource` fast-path when `encoding::linear`, `dim == 0`, and both the source texture and OpenXR swapchain match in dimensions, format (`DXGI_FORMAT_R11G11B10_FLOAT`), single-sampling, and mip `0`, backed by cached source validation (`validated_device_`, `sources_` with `description`). Eliminates 2 `ID3D11CommandList` creations/executions (`FinishCommandList` + `ExecuteCommandList(..., TRUE)`) per frame on `atidxx64.dll` / DX11.
- **Reduced Isolation SRV/UAV Readbacks & `Set*` Calls (`engine_stereo_eye_resources.cpp`)**:
  - Sound `cache.foreign` negative cache for both `original` and `eyes[0/1/2]` (`match_target_resource` prevents untracked eye textures from entering it, gated by `foreign_cache_current()`).
  - Dirty-checks before `OMSetRenderTargets*`, `CSSetUnorderedAccessViews`, and `*SetShaderResources` in `rebind_inherited_state()` and `restore_inherited_state()`.
  - Touched SRV stage mask (`pair.touched_srv_stages`) and post-restore state flag (`post_restore_dirty`, `note_external_views_bound`) to skip reading clean stages and skip redundant `count_bound_target_views()` scans in `end_pair()` while preserving strict quarantine detection (`untracked_right_srv`).
- **Merged CommandList & HUD/Panel COM Caches (`spatial_panel_renderer.hpp`, `spatial_panel_renderer.cpp`, `native_hud_capture.cpp`)**:
  - `spatial_panel::renderer` caches internal texture metadata (`background_width_/height_/format_/view_format_/mipmapped_`, `ink_width_/height_`), avoids duplicate `GetDevice()`/`GetResource()` calls via stack-only `validated_surfaces`, and merges the two `CommandList` passes in `draw_layers()` into a single record/execute (explicitly unbinding `ink_target_` before binding `ink_view_` as SRV).
  - `native_hud_capture.cpp` caches immutable D3D11 states (`blends`, `depth_states`, `rasters`), restores 1 RTV when slots `1..7` are empty, and switches diagnostic counters/states to `std::memory_order_relaxed`.

## 7. OptiLord v0.1.0 — Dedicated Identity & Loading/Streaming Anti-Stutter
- **OptiLord Identity & Independent Versioning (`v0.1.0`)**:
  - Updated `version.json` (`"name": "0.1.0"`, `file_version: [0, 1, 0, 0]`, `prerelease: true`), `src/client/product.hpp` (`name = "OptiLord"`), `src/client/resource.rc`, `assets/steamvr/overlord.vrmanifest` (`"OptiLord (Overlord VR)"`), and the multilingual launcher (`index.html`, `App.tsx`, 9 `locales/*.json` files), crediting **Overlord Beta 8** and **Aeka0**.
  - Intentionally preserved `overlord.exe` / `overlord-debug.exe`, `application_key` (`"overlord"` / `"overlord.debug"`), and `overlord.vrmanifest` for seamless SteamVR compatibility.
- **Lock-free Streaming View Filtering (`src/client/component/vr/engine_stereo_eye_resources.cpp`)**:
  - Added a seqlock-protected watch table (`watched_resources`, `watched_resource_count`, `watched_resources_seq`, `publish_watched_resources()`, `is_watched_resource()`) tracking active `original` and `eyes` resources.
  - `device_create_view_stub` increments `view_creation_epoch` only when `is_watched_resource(resource)` is true (with conservative fallback during concurrent publication or on null pointers), preventing background texture streaming from causing mass `cache.foreign` invalidations and 768-SRV re-scans while keeping ABA protection intact for stereo targets.
- **Eliminated Kernel Syscalls & Contention in `src/client/component/d3d11/texture_diagnostics.cpp`**:
  - Replaced both `ReadProcessMemory(GetCurrentProcess(), ...)` syscalls per `CreateTexture2D` in `copy_pointer()` with SEH-guarded copying (`__try` / `__except (EXCEPTION_EXECUTE_HANDLER)` + `std::memcpy`, free of non-trivial destructors).
  - Added `identity_count` (`std::atomic<std::size_t>`) updated in `identify()` so `create_texture_stub()` scans only occupied slots (`0..count-1` instead of 32).
  - Added relaxed pre-check `target.active.load(std::memory_order_relaxed)` before `target.writing.test_and_set(std::memory_order_acquire)` in `begin_active()` to prevent cache-line bouncing.
- **File Size Caching & I/O Synchronization (`src/client/component/database.cpp`)**:
  - Computed `uint64_t size` once on file open in `bnet_file_handle_t` (`bnet_fs_open_file_stub`), removing repeated `seekg(0, std::ios::end)` / `tellg()` pairs on every read (`bnet_fs_read_stub`) or size query (`bnet_fs_size_stub`).
  - Guarded `bnet_file_handles` and `handle_allocator` with `std::mutex bnet_file_handles_mutex` (`bnet_fs_open_file_stub`, `bnet_fs_read_stub`, `bnet_fs_tell_stub`, `bnet_fs_size_stub`, `bnet_fs_close_stub`, `bink_io_read_stub`, `bink_io_seek_stub`, `close_fastfile_handles()`), unlocking prior to any fallback hook invocation.

## 8. OptiLord v0.2.0 — Dense-Scene CPU Hot-Path Optimizations (*Operation Wolverines*)
- **Removed locks, COM copies, and post-bootstrap probes in the stereo renderer (`src/client/component/vr/engine_stereo_renderer.cpp`)**:
  - In `backend_target_select_probe_stub`, removed `d3d11::get_device_snapshot()` (avoiding `graphics_mutex` acquisition and 3 pairs of COM `AddRef`/`Release` per `R_SetRenderTarget`) as well as the unused `get_current_binding()` call, passing `owner_pass->context.Get()` and `0` directly to `engine_stereo_owner_pass::note_target_selection()`.
  - In `h2_query_publish_probe_stub` and `record_h2_query_result`, short-circuited post-bootstrap whenever `bootstrap_complete()` returns `true`.
- **Avoided `OMGetRenderTargets` on non-display targets (`src/client/component/vr/engine_stereo_owner_pass.cpp`)**:
  - In `note_target_selection()`, moved `OMGetRenderTargets` inside the `target_index < target_count` branch after `target_index` resolution, sparing ~95% of intermediate non-display target calls in heavy scenes.
- **8-entry lock-free ring for sun-shadow negative caching (`src/client/component/vr/engine_stereo_sun_shadow.cpp`)**:
  - Replaced the single-entry negative cache with an 8-entry lock-free `non_matching_views` ring (cleared in `watch()` and `unwatch()`), eliminating thrashing when multiple non-sun-shadow targets alternate.
  - Shared across both `dsv_matches_watched()` and `write_view()` to eliminate redundant `GetResource()` COM queries.
- **Conditional HUD capture arming on `DrawIndexed` (`src/client/component/vr/native_hud_capture.cpp`, `src/client/component/vr/engine_stereo_draw_indexed.cpp`)**:
  - Removed permanent `copy_draw` registration in `post_unpack()`; `copy_draw` is now armed solely during `native_hud_capture::handle()` execution and restored to `nullptr` on exit.
  - Loaded `copy_observer` first in `draw_stub()` (before `live_installed` and `live_context`), short-circuiting **99.9%** of 3D `DrawIndexed` calls outside HUD capture.
- **Streamlined per-eye resource isolation (`src/client/component/vr/engine_stereo_eye_resources.cpp`)**:
  - Streamlined `active_isolated_eye()`: calls `GetCurrentThreadId()` once and runs thread-local checks before cross-core atomic loads (while preserving context/generation mismatch detection).
  - Extracted the single-slot core into `rewrite_admitted_view_synced()` so `rewrite_admitted_view_batch()` calls `sync_foreign_caches_on_owner()` and `foreign_cache_current()` once before the loop, removing duplicate per-slot foreign-cache lookups while fully preserving the `watched_resources` table from v0.1.0.

## 9. OptiLord v0.3.0 — GPU Render Scale, FidelityFX CAS Sharpening & OpenXR Hidden Area Mesh (`XR_KHR_visibility_mask`)
- **Decoupled VR Scene Render Scale (`src/client/component/vr/render_tuning.hpp`, `openxr_runtime.cpp`, `openvr_runtime.cpp`)**:
  - `vr::render_tuning` exposes atomic `render_scale`, `sharpening`, and `hidden_area_mesh` flags, plus `scale_extent()` which clamps the scale factor to `[0.50, 1.20]` (leaving `[0.995, 1.005]` at exact `1.0`), aligns scaled dimensions to multiples of 8 (`>= 64`, bounded by runtime maximum extents), and validates through `engine_scene_resolution::valid()`.
  - Both `openxr_runtime.cpp` and `openvr_runtime.cpp` keep full headset recommended swapchain dimensions for composition (`eyes_` and `status_.recommended_eye_width`/`height`), while passing scaled `{scene_width_, scene_height_}` to `engine_scene_resolution::request()` and validating native arming/preparation (`try_arm_native()` in OpenXR; `activate_submit_format_candidate()`, `try_arm_gpu_transport()`, `prepare_frame()`, and `prepare_scene()` in OpenVR) against `scene_width_`/`scene_height_`.
  - Lowering `vr_renderScale` (e.g., `80%`–`90%`) cuts per-pixel 3D shading cost in dense outdoor missions without degrading HUD/menu clarity or OpenXR swapchain resolution.
- **5-Tap Linear-Space AMD FidelityFX CAS (`src/client/component/vr/texture_blit.hpp`, `src/client/component/vr/texture_blit.cpp`)**:
  - Added `sharpness` to `draw_request` and expanded the `Parameters` constant buffer to 32 bytes (`mode`, `dim`, `sharpness`, `rcp_source_size`).
  - Inside `pixel_source` (`SOURCE_LINEAR`), when `sharpness > 0.001`, applies 5-tap linear HDR Contrast Adaptive Sharpening using the center pixel and 4 cardinal neighbors (`rcp_source_size`), keeping unscaled `CopyResource` strictly active when `encoding::linear`, `dim == 0`, `sharpness == 0`, and dimensions match.
  - `openxr_runtime.cpp::draw_eye()` forwards live `render_tuning::sharpening`, allowing real-time tuning without restarting the game.
- **OpenXR Hidden Area Mesh (`XR_KHR_visibility_mask`) in Reverse-Z (`openxr_dispatch.{hpp,cpp}`, `openxr_runtime.cpp`, `hidden_area_mask.hpp`, `engine_stereo_eye_resources.{hpp,cpp}`, `engine_stereo_output_merger.{hpp,cpp}`)**:
  - `dispatch_table::load_instance()` resolves `xrGetVisibilityMaskKHR` optionally; `openxr_runtime.cpp` requests `XR_KHR_VISIBILITY_MASK_EXTENSION_NAME`, populates `XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR` per eye after `xrCreateSession` and on `XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR`, and pushes live projection tangents from `locate_views()` to `vr::hidden_area_mask`.
  - `vr::hidden_area_mask` transforms tangent-plane mask vertices into NDC and stamps `z = 1.0` (reverse-Z near plane, `D3D11_COMPARISON_ALWAYS`, `D3D11_DEPTH_WRITE_MASK_ALL`, `RenderTargetWriteMask = 0`) using a dedicated deferred context (`native_conversion_command_list::mark_recording_context` / `mark`) and `ExecuteCommandList(..., TRUE)` to leave H2's immediate-context state untouched.
  - `engine_stereo_eye_resources::current_isolated_eye()` identifies the active isolated eye (`< 2`) on the D3D11 context, and `engine_stereo_output_merger::set_post_clear_depth_hook()` triggers `post_clear_depth_hidden_area` immediately after reverse-Z depth clears (`D3D11_CLEAR_DEPTH`, `depth == 0.0f`) outside `active_replay_guard` and `active_transaction` (guarded by TLS `in_post_clear_hook`).
- **Launcher Performance Group & 9-Language Localization (`settings.hpp`, `vr_component.cpp`, `settings-schema.json`, `locales/*.json`, `tests/vr/mock_smoke.cpp`, `tests/vr/output_merger_probe.cpp`)**:
  - Registered `vr_renderScale` (`50%`–`120%`), `vr_sharpening` (`0%`–`100%`), and `vr_hiddenAreaMesh` (default enabled) as `DVAR_FLAG_SAVED` dvars in `vr_component.cpp`, added the `settings.performance` group at the end of `basics` across all 9 locales, and added automated WARP verification for CAS upscaling, `scale_extent()`, and reverse-Z depth stamping.

## 10. OptiLord v0.4.0 — CPU Contention & Frame Pacing
- **Real-Session Telemetry Diagnosis (`vr_debugWaitTiming` — Meta Quest 3 / RX 9070 / Ryzen 7 5700X)**:
  - Real-play `vr_debugWaitTiming` captures showed `scheduler::r_end_frame_stub()` (H2 Frontend/Renderer thread) stalling **`1.0–4.5 ms` per frame (up to `87.5 ms` max)** at `runtime.facade.enabled` while calling `applied_enabled()` and `poll_capture()`, blocked behind the Present/Backend thread holding `mutex_` across blocking `xrWaitFrame` / `xrWaitSwapchainImage` waits.
- **Lock-Free `requested_enabled()`, `applied_enabled()`, and `poll_capture()` (`src/client/component/vr/runtime_backend.cpp`, `openxr_runtime.cpp`, `openvr_runtime.cpp`, `tests/vr/mock_smoke.cpp`)**:
  - Added lock-free `desired_enabled_atomic_` and `applied_enabled_atomic_` (`std::atomic_bool`) beside `session_running_atomic_` in `openxr_runtime.cpp` and `openvr_runtime.cpp`, updated in `publish_status_locked()` and across every `status_.desired_enabled` / `status_.applied_enabled` transition so `requested_enabled()` and `applied_enabled()` perform pure `std::memory_order_acquire` loads with no mutex acquisition.
  - Removed `mutex_` locks from `requested_enabled()`, `applied_enabled()`, and `poll_capture()` in both `runtime_backend::implementation` and `runtime_backend` (`runtime_backend.cpp`), leveraging immutable backend selection after construction while preserving synchronized Present/lifecycle transitions and verifying both `openxr::runtime_backend` (`run_happy_path`) and `vr::runtime_backend` (`expect_facade_lockfree_queries`) in `tests/vr/mock_smoke.cpp`.
- **Critical VR Thread Priority Pinning (`src/client/component/vr/vr_component.cpp`)**:
  - Added a `thread_local`-guarded helper (`boost_critical_vr_thread_priority()`) pinning `THREAD_PRIORITY_ABOVE_NORMAL` via `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)` once per thread on both `on_present_pre()` (Present/Backend/OpenXR owner thread) and `on_renderer_frame()` (Renderer/Frontend thread), complementing `ABOVE_NORMAL_PRIORITY_CLASS` and `1 ms` system timer resolution (`timeBeginPeriod(1)`).
- **Zero-Observer Resource-Ops Fast-Path (`src/client/component/vr/engine_stereo_resource_ops.cpp`)**:
  - Added `record_call_if_no_observers()` checking `active_observer_mask.load(std::memory_order_acquire) == 0`, preserving `expected_context == 0` filtering, `foreign_context_calls`, and `calls[index(operation)]` accounting identically before avoiding `publish()`.
  - Invoked across all 8 resource-operation stubs (`CopySubresourceRegion`, `CopyResource`, `UpdateSubresource`, `CopyStructureCount`, `ClearUnorderedAccessViewUint`, `ClearUnorderedAccessViewFloat`, `GenerateMips`, `ResolveSubresource`) to skip `_ReturnAddress()` capture and 80-byte `event` struct construction across **500,000+ D3D11 copy/update calls** when no observer is active.

## 11. OptiLord v0.5.0 — Clean Installer & Uninstaller
- **Multi-Source MW2CR Auto-Detection (`tools/installer/game_paths.hpp`)**:
  - `detect_game_directories()` gathers, normalizes (`normalize_candidate_path()`), validates (`is_valid_game_dir()` requiring `MW2CR.exe` or `h2_sp64_bnet_ship.exe`), and case-insensitively deduplicates game paths across five sources:
    1. Previous install path in `HKCU\Software\OptiLord` (`InstallDir`).
    2. Windows Uninstall registry keys (`HKLM\SOFTWARE\WOW6432Node\...`, `HKLM\SOFTWARE\...`, `HKCU\SOFTWARE\...`) matching MW2CR `DisplayName` entries via `InstallLocation` and `DisplayIcon`.
    3. Battle.net client configuration JSON (`%APPDATA%\Battle.net\Battle.net.config` -> `Client.Install.DefaultInstallPath`).
    4. All fixed logical drives (`GetLogicalDriveStringsW()` + `DRIVE_FIXED`) across standard Battle.net, Blizzard, Epic/Epic Games, SteamLibrary (`steamapps\common\...`), `Program Files`, `Games`, and drive-root folders.
    5. Current working directory and installer executable parent directory.
- **Zip-Slip & Reparse-Point Path Sanitization (`tools/installer/install_engine.hpp`)**:
  - `safe_relative_zip_path()` enforces strict relative paths before any extraction or manifest cleanup: rejects empty names, leading/trailing `/` or `\`, ASCII control characters (`< 0x20`, `0x7f`), drive colons and invalid Windows filename chars (`< > " | ? *`), empty/`.`/`..` path segments, trailing dots/spaces, and Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`).
  - `has_reparse_component()` / `is_reparse_point()` rejects traversing or writing across symbolic links or directory junctions (`FILE_ATTRIBUTE_REPARSE_POINT`).
- **Pre-Upgrade Stale-Binary Purge (`clean_mod_files()`, `inspect_installation()`)**:
  - Eliminates stale-binary and mixed-resource crashes when upgrading from earlier Overlord or OptiLord releases (e.g., lingering `overlord.pdb`, `overlord-debug.exe`, `h2-mod/`, `steamvr/`, `vr_input/`, or untracked files from older archives).
  - Refuses to proceed if `find_running_game_processes()` detects `overlord.exe`, `overlord-debug.exe`, `mw2cr.exe`, or `h2_sp64_bnet_ship.exe`, then removes prior `optilord-install-manifest.json` entries, known mod root files, and mod-only subdirectories (`h2-mod`, `steamvr`, `vr_input`, `licenses`, `docs`) prior to atomic 64 KB `.tmp` + rename ZIP extraction (`unzOpen2_64`).
- **PE Stub Trimming for `OptiLord-Uninstall.exe` (`compute_pe_image_file_size()`)**:
  - The self-extracting `OptiLord-v0.5.0-Setup.exe` stores its `ZIP_DEFLATED` payload appended right after the compiled Win32 PE stub (`tools/build_installer.py`).
  - During `install_mod()`, `compute_pe_image_file_size()` parses `IMAGE_DOS_HEADER::e_lfanew`, `IMAGE_NT_SIGNATURE`, optional header `SizeOfHeaders`, and `max(PointerToRawData + SizeOfRawData)` across all `IMAGE_SECTION_HEADER` entries, copying only the exact PE image bytes `[0 .. pe_size)` into `OptiLord-Uninstall.exe` (`< 2 MB`, no appended ZIP payload).
  - When `OptiLord-Uninstall.exe` runs directly from the game directory, `uninstall_mod()` spawns a detached hidden `cmd.exe` (`CREATE_NO_WINDOW`) from `System32` to delete the uninstaller binary after exit so zero locks remain.
- **Game Data & Save-File (`players2/`) Protection (`is_protected_game_path()`)**:
  - `is_protected_game_path()` and `is_protected_top_dir_name()` strictly guard `MW2CR.exe`, `h2_sp64_bnet_ship.exe`, `MW2CR Launcher.exe`, `Data/`, `main/`, `players2/`, `.battle.net/`, and `mods/` against both ZIP extraction and manifest/cleanup deletion.
  - `diagnose/` and `minidumps/` are likewise preserved across upgrades and uninstalls unless `remove_diagnostics` / `--remove-diagnostics` is explicitly checked, verified end-to-end by `tests/vr/test_installer.py`.

## 12. OptiLord v0.6.0 — Dual Shoulder Holsters, Trigger Zone Fixes & Multi-Zone Hover Haptics
- **Dual Shoulder Weapon Holsters (`weapon_carry.hpp`, `weapon_holsters.hpp`, `weapon_carry_grip.hpp`, `weapon_carry_runtime.cpp`, `weapon_inventory_snapshot.hpp`)**:
  - Replaced the single shared `location::back` slot with independent `location::left_back` and `location::right_back` shoulder slots, allowing two two-handed / heavy weapons to be carried simultaneously on the back (`schema = 2`, `slots[4]`, `back_slot()`, `next_overflow()`, `next_back(location)`).
  - Expanded `holsters` storage (`centers`, `radii`, `volumes`) from 3 to 4 slots and split `locate_holsters()` into left (`Y = +back_width`) and right (`Y = -back_width`) shoulder boxes meeting cleanly at `Y = 0` with `hit(eligible=0xFu)`, `hit_distance()`, and 4-bit `draw_contact()` masks (`1, 2, 4, 8`).
  - Exposed `vr_holsterBackWidth` (`0.10–0.40 m`, default `0.20 m`, step `0.01 m`) in `settings.hpp`, `slot_settings`, `vehicles/runtime.cpp`, `settings-schema.json`, and all 9 launcher locales (`92` total settings), plus 4-slot debug HUD labels (`LEFT/RIGHT SHOULDER`) and `left_back`/`right_back` probe aliases.
- **Torso-Aligned Body Pose & Unblocked Body Holstering (`body_pose.hpp`, `weapon_carry.hpp`, `weapon_carry_runtime.cpp`)**:
  - Replaced frozen `eye_reference_` with torso-yaw-rotated `torso_eye` and `std::max(eye[2], 0.f)` downward-pitch clamping so waist, shoulder, chest, and abdomen trigger zones remain body-locked across 180° room-scale turns and downward head tilts.
  - Moved `if (!clear)` after `if (target != location::absent)` in `inventory::release()` and skipped `check_release` wall collision traces when `target` is a body holster so standing near walls never prevents stowing weapons.
- **Trigger-Zone Arbitration, Waist Sync & Hysteresis (`hand_interaction/frame.hpp`, `underbarrel_runtime.cpp`, `nightvision.hpp`, `nightvision_runtime.cpp`, `body_equipment.hpp`, `special_equipment_policy.hpp`, `quick_reload_runtime.cpp`, `world_interaction.cpp`)**:
  - Exported `hand_interaction::waist_distance()` with the `+0.04 m` front-belly/torso bonus uniformly applied, fed it into `underbarrel_runtime.cpp` (`collect_interactions`, `update_modules`), added waist-entry auto-unlock for `vr_supportGripLock`, and unified `resolve_holster_draw()` / `resolve_holster_hit()`.
  - Set foregrip/control interactions to Priority `16`, raised NVG Grip Priority `11 -> 18` above chest/abdomen (`20`), tightened NVG `acquisition()` bounds, reduced chest `z` half-extents (`knife .17f`, `grenade .14f`), and gave `abdominal_grab_distance_at()` a `[0..0.25]` interior distance gradient (`abdomen()` at `-0.24 m`).
  - Added `+4 cm` active dwell exit hysteresis and begin/complete haptic cues to Quick Reload (`quick_reload_runtime.cpp`), plus `+0.35 m` / `+8°` retention hysteresis on active world `use_lease` interactions (`world_interaction.cpp`).
- **Multi-Zone Hover Readiness Haptics (`holster_feedback.hpp`, `weapon_feedback.{hpp,cpp}`, `equipment_runtime.{hpp,cpp}`, `grenade_runtime.{hpp,cpp}`, `special_equipment_runtime.{hpp,cpp}`, `weapon_carry_runtime.cpp`)**:
  - Updated `back_slot_candidate()` to `0xCu`, added read-only `chest_knife_ready()`, `chest_slot_ready(unsigned)`, and `abdomen_ready()` zone checks, defined distinct `hover_zone` readiness profiles (`left_waist`, `right_waist`, `left_back`, `right_back`, `chest_knife`, `chest_tactical`, `chest_lethal`, `abdomen`, `ammo_pouch`), and wired per-zone `zone_hover_feedback` latching with `+4 cm` exit margins and `hand_interaction::free()` gating in `finish_interactions()`.

## 13. OptiLord v0.7.0 — Generalized Sun Shadow Reuse, CommandList Caching, Ring Pacing & PIP Scope Relief
- **Generalized Sun Shadow Reuse (`engine_stereo_sun_shadow.{hpp,cpp}`, `engine_stereo_owner_pass.{hpp,cpp}`, `diagnostics/owner_status.cpp`)**:
  - Expanded `inputs` (`max_partitions = 4`) and generalized `snapshot()` to accept shadow tile resolutions `{512, 1024, 2048, 4096}`, configured `{2, 3, 4}` cascades, active `1..configured` partitions, main targets `{26, 27}`, and optional transparent sun-shadow targets `{28, 29}` tracked via dual watched resources (`watched[0]`, `watched[1]`), while allowing static-geometry-only `end >= begin` ranges.
  - Removed the `active->auxiliary.valid` block so auxiliary scope views can also skip redundant sun-shadow draws (`note_skip(..., is_aux)`), keeping the watcher armed into right-eye execution and invalidating cleanly whenever a native draw runs.
  - Added granular `sun_shadow_reuse:` rejection counters (`reject_contract`, `reject_cache`, `reject_delegated`, `reject_texture`, `reject_post_left`, `reject_right_mismatch`) and live native header snapshots (`last_tile`, `last_configured`, `last_active`, `last_main_target`, `last_trans_target`, `last_trans_enabled`, `last_cache_enabled`).
- **Cached `ID3D11CommandList` Playback & Idle Fast-Paths (`hidden_area_mask.hpp`, `texture_blit.{hpp,cpp}`, `native_render_session.cpp`, `engine_stereo_execution.{hpp,cpp}`, `engine_stereo_draw_indexed.cpp`)**:
  - Cached recorded deferred `ID3D11CommandList` objects per eye `(context, dsv, width, height, revision)` in `hidden_area_mask::stamp_eye_depth()` and per `(source, destination, width, height, format)` in `texture_blit::renderer::draw()`, updating the 32-byte constant buffer on `graphics.context` only when `shader_parameters` changes.
  - Skipped redundant `texture_uses_device()` COM queries in `native_render_session::copy_eye()` when `conversion_source.Get() == source`, and added `needs_draw_indexed_observation()` fast-path passthrough in `engine_stereo_draw_indexed::draw_stub()`.
- **Scoped Dynamic-Arena Page Validation (`engine_stereo_dynamic_arena.{hpp,cpp}`, `engine_stereo_owner_pass.{hpp,cpp}`)**:
  - Exposed `validation_scope` reused within each synchronous owner view (reset in `begin_view()`, `end()`, and `retire_transaction_preserving_records()`), avoiding repeat `K32QueryWorkingSetEx` Ring-0 transitions across dynamic-index boundaries while preserving per-call SEH bounds guards and rejecting read-to-write promotion.
- **Triple-Buffered Ring & Stale-Family Fix (`native_render_session.hpp`, `engine_stereo_bridge.cpp`)**:
  - Increased `native_render_session::pair_count` from `2` to `3` (~30–40 MiB VRAM overhead for 1 extra `R11G11B10_FLOAT` stereo pair) to eliminate pipelined `admit_pair` slot starvation.
  - Updated `engine_stereo_bridge::take_view_family_for_frontend()` to return `false` when `published_taken && !next_family.valid && published_eye_mask == 0x3u` instead of re-issuing an already-completed stereo family and generating wasted `frontend_pair_unavailable` mono frames.
- **PIP Scope Rendering Modes & Effect Stripping (`settings.hpp`, `gameplay/optic_runtime.cpp`, `native_post_aa.cpp`, `engine_stereo_owner_pass.cpp`, `settings-schema.json`, `locales/*.json`)**:
  - Registered `vr_scopeRenderMode` (`on`, `thermal_only`, `off`) in `settings.performance` across all 9 launcher locales (`93` settings total), letting `thermal_only` fall back to the zero-cost same-eye magnified lens on standard optical scopes while keeping independent thermal optics active.
  - Bypassed `native_post_aa::select_display_target()` / `apply()` history work on `auxiliary_scene::view_index` and zeroed `thermal_scene::ssr_offset` on non-thermal `output.auxiliary_record` views.
