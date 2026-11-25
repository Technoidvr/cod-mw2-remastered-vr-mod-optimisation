# Changelog — OptiLord

Toutes les évolutions notables d'**OptiLord** (maintenu par **TechnoidVR**, basé sur **Overlord Beta 8** d'**[Aeka0](https://github.com/Aeka0/Overlord)**) sont documentées ci-dessous.

---

## [0.1.0] — OptiLord (Base Overlord Beta 8)

### Identité & Launcher
- **Versionnage indépendant `0.1.0`** (`version.json` : `"name": "0.1.0"`, `file_version: [0, 1, 0, 0]`, `prerelease: true`).
- **Identité OptiLord** : mise à jour de `product::name` (`src/client/product.hpp`), des métadonnées PE (`src/client/resource.rc`), du titre SteamVR `"OptiLord (Overlord VR)"` (`assets/steamvr/overlord.vrmanifest`), et du launcher WebView2/React (`index.html`, `App.tsx`, et les 9 fichiers de langues `locales/*.json` : `en`, `fr`, `de`, `es`, `ja`, `ko`, `ru`, `zh-CN`, `zh-TW`), avec mention explicite de la base **Overlord Beta 8** et remerciements à **Aeka0**.
- **Compatibilité SteamVR préservée** : conservation de l'exécutable `overlord.exe` / `overlord-debug.exe`, de la clé `application_key` (`"overlord"` / `"overlord.debug"`) et du manifeste `overlord.vrmanifest`.

### Anti-Stutters Streaming & Chargements (v0.1.0)
- **Filtrage sans verrou des vues créées par le streamer (`src/client/component/vr/engine_stereo_eye_resources.cpp`)** :
  - Ajout d'une table de surveillance lock-free par *seqlock* (`watched_resources`, `watched_resource_count`, `watched_resources_seq`) publiée par `publish_watched_resources()` lors des transitions de graphe (`rebuild_resources()`, `ensure_auxiliary_resources()`, `release_generation_state()`).
  - `device_create_view_stub` n'incrémente désormais `view_creation_epoch` que si `is_watched_resource(resource)` est vrai (avec comportement conservateur en cas d'écriture concurrente ou de pointeur nul), éliminant la tempête de purges `cache.foreign` et les re-balayages de 768 SRV provoqués par le streaming d'arrière-plan tout en préservant la détection ABA des vues stéréo isolées.
- **Élimination des appels noyau et balayages inutiles dans les diagnostics de textures (`src/client/component/d3d11/texture_diagnostics.cpp`)** :
  - Remplacement des 2 syscalls `ReadProcessMemory(GetCurrentProcess(), ...)` par appel `CreateTexture2D` dans `copy_pointer()` par une copie SEH `__try`/`__except (EXCEPTION_EXECUTE_HANDLER)` sans destructeur non trivial.
  - Ajout du high-water mark atomique `identity_count` mis à jour dans `identify()` pour limiter le scan de `create_texture_stub()` aux slots occupés (`0..count-1` au lieu de 32).
  - Pré-lecture détendue `if (target.active.load(std::memory_order_relaxed)) continue;` dans `begin_active()` avant `writing.test_and_set(std::memory_order_acquire)` pour éviter le *cache-line bouncing*.
- **Cache de taille fichier & synchronisation I/O dans `src/client/component/database.cpp`** :
  - Stockage unique de `uint64_t size` dans `bnet_file_handle_t` à l'ouverture (`bnet_fs_open_file_stub`), évitant les `seekg(0, std::ios::end)` / `tellg()` répétés dans `bnet_fs_read_stub` et `bnet_fs_size_stub`.
  - Protection de `bnet_file_handles` et `handle_allocator` par `std::mutex bnet_file_handles_mutex` à travers `bnet_fs_open_file_stub`, `bnet_fs_read_stub`, `bnet_fs_tell_stub`, `bnet_fs_size_stub`, `bnet_fs_close_stub`, `bink_io_read_stub`, `bink_io_seek_stub` et `close_fastfile_handles()` (avec déverrouillage systématique avant tout `*_hook.invoke(...)`).

### Optimisations Phase 4 DX11 / OpenXR / HUD (incluses depuis `db01d96`)
- **Blit OpenXR direct `CopyResource` (`texture_blit.hpp`, `texture_blit.cpp`)** : copie matérielle directe sans shader ni `FinishCommandList` / `ExecuteCommandList(..., TRUE)` en jeu normal (`encoding::linear`, `dim == 0`, dimensions/format/mipmap/multisampling identiques), avec validation en cache des textures sources (`validated_device_`, `sources_` avec `description`).
- **Réduction des relectures SRV/UAV et appels `Set*` d'isolation (`engine_stereo_eye_resources.cpp`)** :
  - Validation sûre de `cache.foreign` pour les vues normales et isolées (`foreign_cache_current()`, `match_target_resource()` empêchant `eyes[0/1/2]` et `untracked_right_srv` d'entrer dans `cache.foreign`).
  - *Dirty-checks* avant `OMSetRenderTargets*`, `CSSetUnorderedAccessViews` et `set_shader_resources` dans `rebind_inherited_state()` et `restore_inherited_state()`.
  - Masque `pair.touched_srv_stages` et indicateur `post_restore_dirty` (avec `note_external_views_bound()` / `state_maintenance_scope`) pour éviter les lectures des stages propres et le balayage `count_bound_target_views()` dans `end_pair()`.
- **Fusion `CommandList` & caches COM HUD/Panneaux (`spatial_panel_renderer.hpp`, `spatial_panel_renderer.cpp`, `native_hud_capture.cpp`)** :
  - Cache des métadonnées internes (`background_width_/height_/format_/view_format_/mipmapped_`, `ink_width_/height_`), structure pile `validated_surfaces` sans rétention de `ComPtr` externes, et fusion des deux `CommandList` de `draw_layers()` en un seul enregistrement/exécution (avec déliaison explicite de `ink_target_` avant lecture de `ink_view_`).
  - Caches `raster_entry`, `depth_states` et `blend_entry` dans `native_hud_capture.cpp`, restauration `OMSetRenderTargets(1, ...)` et compteurs diagnostiques en `std::memory_order_relaxed`.

### Base incluse : Overlord Beta 8 + Phases 1–3
- **Mémoire PE & TLS (`lazy_storage.hpp`)** : `SizeOfImage` réduit de **282,9 Mo à 111,7 Mo**, `.data` réduite de **264,0 Mo à 92,3 Mo**, template TLS réduit de **98,2 %** (**2,58 Mo → 46,2 Ko**).
- **CPU, DX11 & OpenXR (Phases 1–3)** : compilation MSVC AVX2 (`/O2 /Ob3 /arch:AVX2`), scanner AVX2, priorité `DXGI_FORMAT_R11G11B10_FLOAT`, soumission stéréo en un seul `Flush()`, `XR_EXT_performance_settings`, découplage VSync 2D (`SyncInterval = 0`), flou spatial allégé, GC Lua incrémental, timer système `1 ms` et priorité `ABOVE_NORMAL_PRIORITY_CLASS`.
- **Correctifs Gameplay VR** : synchronisation de la capsule de collision room-scale (`roomscale_origin`, `head_pose_bridge`), holster d'épaule/dos pour fusils d'assaut (correction du masque de bits + haptique + relâchement auto de la main de soutien), et tolérances élargies sur les chargeurs à la ceinture et le levier d'armement.

---

## English Summary

### [0.1.0] — OptiLord (Built on Overlord Beta 8)
- **Dedicated Identity & Independent Versioning (`v0.1.0`)**: Rebranded launcher, PE metadata, SteamVR manifest (`OptiLord (Overlord VR)`), and all 9 locales crediting **Aeka0** and **Overlord Beta 8**, while preserving `overlord.exe`, `overlord` app key, and `overlord.vrmanifest` for SteamVR compatibility.
- **Anti-Stutter Streaming & Loading Fixes (`v0.1.0`)**:
  - `engine_stereo_eye_resources.cpp`: Lock-free seqlock `watched_resources` table so background texture streaming `Create*View` calls no longer flush `cache.foreign` and force 768-SRV re-scans, while preserving ABA detection for stereo isolated targets.
  - `texture_diagnostics.cpp`: Replaced `ReadProcessMemory` syscalls in `copy_pointer` with SEH `__try`/`__except` `memcpy`, added `identity_count` high-water mark to bound slot scans, and relaxed `active` pre-check in `begin_active()`.
  - `database.cpp`: Cached `uint64_t size` on open in `bnet_file_handle_t` (removing repeated `seekg(end)`/`tellg()` in `bnet_fs_read_stub` and `bnet_fs_size_stub`) and guarded `bnet_file_handles` / `handle_allocator` with `std::mutex bnet_file_handles_mutex` (unlocking before any fallback hook call).
- **Included Phase 4 DX11 / OpenXR / HUD Optimizations (`db01d96`)**: Direct `CopyResource` eye blit (`texture_blit`), sound `cache.foreign` + dirty-checked `Set*` + `touched_srv_stages` / `post_restore_dirty` (`engine_stereo_eye_resources`), merged single-CommandList `draw_layers()` + cached metadata (`spatial_panel_renderer`), and cached D3D11 states + 1-RTV restore + relaxed counters (`native_hud_capture`).
- **Included Overlord Beta 8 + Phases 1–3**: Lazy storage (`SizeOfImage` 282.9 MB → 111.7 MB, TLS −98.2%), AVX2 build, `R11G11B10_FLOAT`, single-Flush stereo, 1 ms timer + `ABOVE_NORMAL_PRIORITY_CLASS`, and VR gameplay fixes (room-scale doorway collision sync, assault-rifle back holster bitmask + haptics, belt pouch & charging handle tolerances).
