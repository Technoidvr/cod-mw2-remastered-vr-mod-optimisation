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
