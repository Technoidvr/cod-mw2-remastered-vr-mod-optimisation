# Changelog — OptiLord

All notable changes in **OptiLord** (maintained by **TechnoidVR**, based on **Overlord Beta 8** by **[Aeka0](https://github.com/Aeka0/Overlord)**) are documented below.

---

## [0.3.0] — OptiLord (GPU Render Scale, FidelityFX CAS & OpenXR Hidden Area Mesh)

### Decoupled VR Render Scale & AMD FidelityFX CAS Sharpening (`v0.3.0`)
- **Live Render Tuning State (`src/client/component/vr/render_tuning.hpp`)**:
  - Added `vr::render_tuning` with atomic settings `render_scale` (`1.0f`), `sharpening` (`0.0f`), and `hidden_area_mesh` (`true`).
  - Added `scale_extent()` clamping render scale to `[0.50, 1.20]` (short-circuiting within `0.005` of `1.0`), rounding scaled scene width/height to multiples of 8 (`>= 64`, bounded by runtime maximum extents), and validating via `engine_scene_resolution::valid()`.
- **Linear-Space AMD FidelityFX CAS Upscaling (`src/client/component/vr/texture_blit.hpp`, `src/client/component/vr/texture_blit.cpp`)**:
  - Added `float sharpness{};` to `texture_blit::renderer::draw_request`, clamped to `[0.f, 1.f]`.
  - Preserved the zero-shader `CopyResource` fast-path strictly for unscaled linear passes (`encoding::linear`, `clamped_dim == 0.f`, `clamped_sharpness == 0.f`, and matching source/destination dimensions); any render scale != `100%` or non-zero sharpening uses the shader path.
  - Expanded the `Parameters` constant buffer to 32 bytes (`mode`, `dim`, `sharpness`, `rcp_source_size`) and implemented 5-tap linear-space AMD FidelityFX Contrast Adaptive Sharpening in `pixel_source` under `SOURCE_LINEAR` when `sharpness > 0.001`.
- **Decoupled Scene Resolution in OpenXR & OpenVR (`src/client/component/vr/openxr_runtime.cpp`, `src/client/component/vr/openvr_runtime.cpp`)**:
  - `openxr_runtime.cpp`: Kept full OpenXR recommended eye swapchain dimensions in `view_configs_`, `eyes_`, and `status_.recommended_eye_width`/`height`, while computing scaled `scene_width_` / `scene_height_` via `render_tuning::scale_extent()` and requesting `{scene_width_, scene_height_}` from `engine_scene_resolution::request()`. Updated `try_arm_native()` to validate `scene_width_`/`scene_height_` and forwarded `render_tuning::sharpening` into `draw_eye()`.
  - `openvr_runtime.cpp`: Computed decoupled `scene_width_` / `scene_height_` in `configure_eye_dimensions()` and updated `activate_submit_format_candidate()`, `try_arm_gpu_transport()`, `prepare_frame()`, and `prepare_scene()` to validate against `scene_width_` / `scene_height_` (falling back to `status_.eyes` when `scene_width_ == 0`).

### OpenXR Hidden Area Mesh (`XR_KHR_visibility_mask`) (`v0.3.0`)
- **OpenXR Dispatch & Session Integration (`src/client/component/vr/openxr_dispatch.hpp`, `src/client/component/vr/openxr_dispatch.cpp`, `src/client/component/vr/openxr_runtime.cpp`)**:
  - Added `PFN_xrGetVisibilityMaskKHR get_visibility_mask` to `dispatch_table` with optional non-fatal instance loading in `dispatch_table::load_instance()`.
  - Enabled `XR_KHR_VISIBILITY_MASK_EXTENSION_NAME` when supported in `initialize_instance()`, added `refresh_visibility_mask()` querying `XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR` after `xrCreateSession` and on `XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR` in `poll_events()`, pushed live tangent projections from `locate_views()` to `hidden_area_mask::set_eye_projection()`, and cleared state in `teardown_locked()`.
- **Reverse-Z Hidden Area Depth Stamping (`src/client/component/vr/hidden_area_mask.hpp`)**:
  - Added header-only `vr::hidden_area_mask` (`vertex2`, `set_eye_mesh()`, `set_eye_projection()`, `stamp_eye_depth()`, `clear()`, `invalidate_device()`, `get_diagnostics()`).
  - Projects tangent-plane mask vertices into NDC using live eye tangents and writes `z = 1.0` (reverse-Z near plane, `D3D11_COMPARISON_ALWAYS`, `D3D11_DEPTH_WRITE_MASK_ALL`, `RenderTargetWriteMask = 0`) onto a deferred command list marked via `native_conversion_command_list::mark_recording_context()` / `mark()` and executed with `ExecuteCommandList(..., TRUE)` to preserve all immediate-context D3D11 state.
- **Per-Eye Output-Merger Clear Hook (`src/client/component/vr/engine_stereo_eye_resources.hpp`, `src/client/component/vr/engine_stereo_eye_resources.cpp`, `src/client/component/vr/engine_stereo_output_merger.hpp`, `src/client/component/vr/engine_stereo_output_merger.cpp`)**:
  - Added `engine_stereo_eye_resources::current_isolated_eye(ID3D11DeviceContext*)` returning `pair.current_eye` when an isolated eye (`< 2`) is active on the context, or `0xFFFFFFFFu` otherwise.
  - Added `post_clear_depth_hook_fn` and `set_post_clear_depth_hook()` in `engine_stereo_output_merger`, invoked after `ClearDepthStencilView` only outside `active_replay_guard` and `active_transaction` with TLS recursion guard `in_post_clear_hook`.

### Launcher Settings, Localization, Documentation & Verification (`v0.3.0`)
- **Settings & Game Dvars (`src/client/component/vr/settings.hpp`, `src/client/component/vr/vr_component.cpp`)**:
  - Added `vr_renderScale` (50%–120%, step 5%, default 100%), `vr_sharpening` (0%–100%, step 5%, default 0%), and `vr_hiddenAreaMesh` (toggle, default `true`), registered as `DVAR_FLAG_SAVED` in `post_unpack()`.
  - Hooked `post_clear_depth_hidden_area` in `on_device_created()` (unhooked + `hidden_area_mask::invalidate_device()` in `on_device_destroying()` and `pre_destroy()`), synced `vr_sharpening` and `vr_hiddenAreaMesh` live in `on_renderer_frame()`, and synced all 3 settings in `request_reinitialize()`.
- **Launcher Schema & 9 Locales (`src/client/resources/launcher/settings-schema.json`, `src/client/resources/launcher/locales/*.json`, `src/launcher-ui/tests/model.test.ts`)**:
  - Added the `settings.performance` group at the end of `basics` containing `vr_renderScale`, `vr_sharpening`, and `vr_hiddenAreaMesh`, localized across all 9 languages (`de`, `en`, `es`, `fr`, `ja`, `ko`, `ru`, `zh-CN`, `zh-TW`) and covered by unit tests in `model.test.ts`.
- **WARP & Probe Tests (`tests/vr/mock_smoke.cpp`, `tests/vr/output_merger_probe.cpp`)**:
  - Added WARP tests covering FidelityFX CAS upscaling, unscaled `CopyResource` fast-path, `render_tuning::scale_extent()`, reverse-Z `hidden_area_mask::stamp_eye_depth()`, and `set_post_clear_depth_hook()` guard invariants.
- **Versioning & Documentation (`version.json`, `README.md`, `CHANGELOG.md`, `OPTIMIZATIONS.md`, `OPTIMIZATIONS_FR.md`)**:
  - Bumped `version.json` to `"0.3.0"` (`file_version: [0, 3, 0, 0]`, `prerelease: true`), standardized `README.md` and `CHANGELOG.md` in clean English, added full English technical reference `OPTIMIZATIONS.md`, and appended Section 9 to `OPTIMIZATIONS_FR.md`.

---

## [0.2.0] — OptiLord (Dense-Scene CPU Hot-Path Optimizations / Operation Wolverines)

### Dense-Scene CPU Hot-Path Optimizations (`v0.2.0`)
- **Removed locks, COM copies, and post-bootstrap probes in the stereo renderer (`src/client/component/vr/engine_stereo_renderer.cpp`)**:
  - Removed `d3d11::get_device_snapshot()` (`graphics_mutex` lock + 3× COM `AddRef`/`Release`) and unused `get_current_binding()` from `backend_target_select_probe_stub`, passing `owner_pass->context.Get()` and `0` directly to `note_target_selection()`.
  - Bypassed `h2_query_publish_probe_stub` and `record_h2_query_result` post-bootstrap whenever `bootstrap_complete()` returns `true`.
- **Avoided `OMGetRenderTargets` on non-display targets (`src/client/component/vr/engine_stereo_owner_pass.cpp`)**:
  - Moved `OMGetRenderTargets` inside the `target_index < target_count` branch in `note_target_selection()`, sparing ~95% of intermediate non-display target selections in dense scenes.
- **8-entry lock-free negative cache ring for sun-shadow detection (`src/client/component/vr/engine_stereo_sun_shadow.cpp`)**:
  - Replaced the single-entry negative cache with an 8-entry lock-free `non_matching_views` ring cleared on `watch()`/`unwatch()` and shared across both `dsv_matches_watched()` and `write_view()`.
- **Conditional HUD capture arming on `DrawIndexed` (`src/client/component/vr/native_hud_capture.cpp`, `src/client/component/vr/engine_stereo_draw_indexed.cpp`)**:
  - Removed permanent `copy_draw` registration in `post_unpack()`, arming `copy_draw` exclusively during `native_hud_capture::handle()` and resetting it to `nullptr` on exit.
  - Checked `copy_observer` first in `draw_stub()` before `live_installed` and `live_context`, short-circuiting 99.9% of 3D `DrawIndexed` calls.
- **Streamlined per-eye resource isolation (`src/client/component/vr/engine_stereo_eye_resources.cpp`)**:
  - Streamlined `active_isolated_eye()` to call `GetCurrentThreadId()` once and verify thread-local state before cross-core atomics.
  - Factored `rewrite_admitted_view_synced()` so `rewrite_admitted_view_batch()` performs `sync_foreign_caches_on_owner()` and `foreign_cache_current()` once before the slot loop (eliminating duplicate per-slot foreign-cache checks while preserving `watched_resources`).
- **Versioning & test alignment (`version.json`, `tests/ui_script_modules_tests.cpp`)**:
  - Updated `version.json` to `"0.2.0"` (`file_version: [0, 2, 0, 0]`, `prerelease: true`) and aligned UI script assertions with `product::name == "OptiLord"`.

---

## [0.1.0] — OptiLord (Built on Overlord Beta 8)

### Identity & Launcher
- **Independent `0.1.0` versioning (`version.json`)**: Set `"name": "0.1.0"`, `file_version: [0, 1, 0, 0]`, `prerelease: true`.
- **OptiLord Identity**: Updated `product::name` (`src/client/product.hpp`), PE version metadata (`src/client/resource.rc`), SteamVR title `"OptiLord (Overlord VR)"` (`assets/steamvr/overlord.vrmanifest`), and the WebView2/React launcher (`index.html`, `App.tsx`, and all 9 locale files under `src/client/resources/launcher/locales/`), crediting **Overlord Beta 8** and **Aeka0**.
- **SteamVR Compatibility Preserved**: Kept `overlord.exe` / `overlord-debug.exe`, `application_key` (`"overlord"` / `"overlord.debug"`), and `overlord.vrmanifest`.

### Anti-Stutter Streaming & Loading Fixes (`v0.1.0`)
- **Lock-free streamer view filtering (`src/client/component/vr/engine_stereo_eye_resources.cpp`)**:
  - Added a seqlock-protected watch table (`watched_resources`, `watched_resource_count`, `watched_resources_seq`, `publish_watched_resources()`, `is_watched_resource()`) updated at resource graph transitions (`rebuild_resources()`, `ensure_auxiliary_resources()`, `release_generation_state()`).
  - `device_create_view_stub` increments `view_creation_epoch` only when `is_watched_resource(resource)` is true (with conservative fallback on concurrent writes or null pointers), preventing background texture streaming from triggering repeated `cache.foreign` flushes and 768-SRV re-scans while preserving stereo target ABA detection.
- **Eliminated kernel syscalls and slot scan overhead in texture diagnostics (`src/client/component/d3d11/texture_diagnostics.cpp`)**:
  - Replaced `ReadProcessMemory(GetCurrentProcess(), ...)` calls in `copy_pointer()` with SEH `__try` / `__except (EXCEPTION_EXECUTE_HANDLER)` `std::memcpy` (no non-trivial destructors).
  - Added atomic `identity_count` high-water mark updated in `identify()` so `create_texture_stub()` scans only occupied slots (`0..count-1` instead of all 32).
  - Added relaxed pre-check `if (target.active.load(std::memory_order_relaxed)) continue;` in `begin_active()` before `writing.test_and_set(std::memory_order_acquire)` to avoid cache-line bouncing.
- **Cached Bnet file size & I/O synchronization (`src/client/component/database.cpp`)**:
  - Computed `uint64_t size` once on file open in `bnet_file_handle_t` (`bnet_fs_open_file_stub`), removing repeated `seekg(0, std::ios::end)` / `tellg()` calls in `bnet_fs_read_stub` and `bnet_fs_size_stub`.
  - Guarded `bnet_file_handles` and `handle_allocator` with `std::mutex bnet_file_handles_mutex` across `bnet_fs_open_file_stub`, `bnet_fs_read_stub`, `bnet_fs_tell_stub`, `bnet_fs_size_stub`, `bnet_fs_close_stub`, `bink_io_read_stub`, `bink_io_seek_stub`, and `close_fastfile_handles()`, unlocking before any fallback `*_hook.invoke(...)`.

### Included Phase 4 DX11 / OpenXR / HUD Optimizations (`db01d96`)
- **Direct `CopyResource` OpenXR blit (`texture_blit.hpp`, `texture_blit.cpp`)**: Shaderless hardware copy without `FinishCommandList` / `ExecuteCommandList(..., TRUE)` in normal gameplay (`encoding::linear`, `dim == 0`, matching dimensions/format/mipmap/multisampling), with cached source validation (`validated_device_`, `sources_` with `description`).
- **Reduced isolation SRV/UAV readbacks & `Set*` calls (`engine_stereo_eye_resources.cpp`)**: Sound `cache.foreign` for both normal and isolated views (`foreign_cache_current()`, `match_target_resource()` keeping `eyes[0/1/2]` and `untracked_right_srv` out of `cache.foreign`), dirty-checks before `OMSetRenderTargets*` / `CSSetUnorderedAccessViews` / `set_shader_resources`, and `pair.touched_srv_stages` + `post_restore_dirty` tracking to skip clean stage readbacks and redundant `count_bound_target_views()` scans in `end_pair()`.
- **Merged `CommandList` & COM caches for HUD/Panels (`spatial_panel_renderer.hpp`, `spatial_panel_renderer.cpp`, `native_hud_capture.cpp`)**: Cached internal panel dimensions/formats, stack-only `validated_surfaces`, merged `draw_layers()` into a single `CommandList` execution (explicitly unbinding `ink_target_` before binding `ink_view_` as SRV), and cached D3D11 states (`blends`, `depth_states`, `rasters`) + 1-RTV restore + relaxed diagnostic counters in `native_hud_capture.cpp`.

### Included Base: Overlord Beta 8 + Phases 1–3
- **Memory & PE Layout (`lazy_storage.hpp`)**: `SizeOfImage` reduced from **282.9 MB to 111.7 MB**, `.data` reduced from **264.0 MB to 92.3 MB**, raw TLS template reduced by **98.2%** (**2.58 MB → 46.2 KB**).
- **CPU, DX11 & OpenXR (Phases 1–3)**: MSVC AVX2 flags (`/O2 /Ob3 /arch:AVX2`), AVX2 signature scanner, `DXGI_FORMAT_R11G11B10_FLOAT` swapchain preference, single-`Flush()` stereo submission, `XR_EXT_performance_settings`, 2D VSync decoupling (`SyncInterval = 0`), lighter spatial panel blur, incremental Lua GC, `1 ms` Windows timer, and `ABOVE_NORMAL_PRIORITY_CLASS`.
- **VR Gameplay Fixes**: Room-scale physical collision capsule synchronization (`roomscale_origin`, `head_pose_bridge`), shoulder/back holster fix for assault rifles (bitmask fix + synchronized haptics + automatic support-hand release), and widened belt ammo pouch / charging handle tolerances.
