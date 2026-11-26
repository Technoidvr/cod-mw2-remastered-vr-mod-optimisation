# Overlord VR (Beta 8) — Pack d'Optimisations & Correctifs Gameplay (MW2 Campaign Remastered)

> **Version anglaise :** [English Version — OPTIMIZATIONS.md](OPTIMIZATIONS.md)

Ce dépôt contient les optimisations de performances (CPU/GPU/Mémoire/D3D11/OpenXR/Lua/GSC) ainsi que les correctifs de gameplay VR appliqués par-dessus **Overlord Beta 8** pour *Call of Duty: Modern Warfare 2 Campaign Remastered*.

## 1. Optimisations Mémoire & Exécutable PE (`overlord.exe`)
- **Migration vers allocations paresseuses (`vr::lazy_box<T>`, `vr::lazy_array<T, N>`)** dans `src/client/component/vr/lazy_storage.hpp` :
  - `SizeOfImage` réduit de **282,9 Mo à 111,7 Mo** (**-171,2 Mo** d'empreinte virtuelle par processus).
  - Section `.data` réduite de **264,0 Mo à 92,3 Mo**.
  - Template TLS brut par thread réduit de **2,58 Mo à 46,2 Ko** (**-98,2 %**, éliminant ~2,5 Mo d'allocation et d'initialisation à chaque création de thread Windows / XAudio2 / D3D11 / Worker).

## 2. Optimisations CPU Monocœur (Ryzen 5000 / 5700X) & Pilote DX11 AMD (`atidxx64.dll`) / NVIDIA
- **Flags de compilation MSVC `v145`** (`premake5.lua`) : `/O2 /Oi /Ot /Ob3 /Gw /arch:AVX2 /bigobj` + scanner de signatures AVX2 32 octets (`src/common/utils/signature.cpp`).
- **Suppression de la double relecture complète D3D11 par œil** (`src/client/component/vr/engine_stereo_eye_resources.cpp`) :
  - Élimine l'appel redondant à `count_bound_target_views()` dans `rebind_inherited_state` et `restore_inherited_state` et utilise le cache négatif `cache.foreign`, économisant **3 072 à 4 608 lectures SRV + 512 à 768 lectures UAV par frame** dans le pilote DX11.
- **Cache de validation COM par œil/route dans `end_view()`** (`src/client/component/vr/engine_stereo_owner_pass.cpp`) :
  - Évite 7 requêtes COM (`GetResource`, `QueryInterface`, `GetDevice`, `GetDesc`) par œil sous le verrou `h2_gpu_context_lock`.
- **Tables de hachage ouvertes en puissance de 2 & courts-circuits D3D11** (`engine_stereo_pointer_cache.hpp`, `engine_stereo_output_merger.cpp`, `engine_stereo_execution.cpp`, `engine_stereo_draw_indexed.cpp`, `engine_stereo_sun_shadow.cpp`).

## 3. Optimisations OpenXR, Swapchain & Shaders VR
- **Priorité au format HDR 32-bit `DXGI_FORMAT_R11G11B10_FLOAT`** (`openxr_d3d11.hpp`, `openxr_runtime.cpp`) : réduit de **50 %** la bande passante mémoire GPU des copies œil gauche/droit par rapport à `R16G16B16A16_FLOAT`.
- **Soumission stéréo groupée à un seul `Flush()` GPU** (`render_eyes` dans `openxr_runtime.cpp`, `openvr_runtime.cpp`, `vr_component.cpp`).
- **Activation de `XR_EXT_performance_settings`** (`PERF_LEVEL_SUSTAINED_HIGH` CPU & GPU).
- **Découplage VSync 2D (`SyncInterval = 0`)** lorsque la session VR est active (`d3d11.cpp`, `swap_chain_hook.cpp`, `vr_component.cpp`) + miroir bureau `IsIconic` sans verrou.
- **Shaders de panneaux spatiaux optimisés** (`spatial_panel_renderer.cpp` : flou bilinéaire 4-tap / 9-tap au lieu de 9-tap / 25-tap).
- **GC Lua incrémental & caches GSC** (`data/ui_scripts/vr_gameplay/__init__.lua`, `data/cdata/scripts/killfeed/__init__.lua`, `data/cdata/scripts/killfeed_ducking.gsc`).

## 4. Correctifs Gameplay VR (Portes/Collisions, Holsters & Rechargement)
- **Synchronisation Caméra Visuelle VR / Capsule de Collision Physique CoD** (`roomscale_origin.hpp`, `head_pose_bridge.hpp`, `head_pose_bridge.cpp`, `controller_component.cpp`) :
  - Recentre automatiquement la capsule de collision physique (`playerState_s::origin` dans `Pmove`) sous le casque VR dès que l'écart horizontal dépasse `6 cm`, tout en compensant `turn_offset_meters` via `roomscale.shift_offset()` pour que la caméra visuelle reste 100 % stable. Élimine le bug obligeant à traverser visuellement les textures de murs/cadres pour passer les portes.
- **Fiabilisation des Holsters (Rangement & Dégainage)** (`weapon_carry_runtime.cpp`) :
  - Filtrage des slots incompatibles lors du rangement (`eligible_slots` : une arme longue ne peut plus être interceptée et rejetée par un holster de hanche).
  - Détachement automatique de la main de support (`vr_supportGripLock` / mode Toggle) lors du rangement de l'arme dans un holster valide.
  - Double vérification Torse + Tête et Main + Poignet calibré (`+4 cm` de tolérance) pour le rangement et le dégainage.
- **Fiabilisation de la Prise de Chargeur & du Rechargement Physique** (`hand_interaction/frame.hpp`, `weapon_carry_runtime.cpp`, `physical_reload_runtime.cpp`, `physical_reload_contact_sample.hpp`) :
  - Détachement automatique de la main de support lorsque la main secondaire s'éloigne du garde-main (`> 18 cm`) et actionne `Trigger` pour saisir un chargeur ou armer la culasse.
  - Extension de la zone de munitions en jeu aux axes du torse et à l'avant de la ceinture / bas du gilet tactique (`+12 cm` vers l'avant, `+8 cm` vers le haut, `+4 cm` de rayon).
  - Marge additionnelle (`+2,5 cm`) sur la saisie du levier d'armement / culasse et du chargeur engagé.

## 5. Correctif Holster Épaule / Dos, Optimisations Phase 3 & Launcher Fork
- **Correction critique du rangement des fusils d'assaut / armes longues à l'épaule** (`weapon_carry_runtime.cpp`) :
  - Correction du masque de bit passé à `weapon_holsters::hit()` (`1u << 2 == 4` au lieu de `1u << unsigned(location::back) == 16`, qui bloquait le rangement de toutes les armes longues).
  - Synchronisation du retour haptique d'épaule (`finish_interactions()`) avec les volumes étendus, les deux référentiels tête/torse et le relâchement automatique de la main de soutien (`vr_supportGripLock`).
- **Optimisations supplémentaires (Phase 3)** :
  - Validation ultra-rapide par paire des pointeurs de cibles D3D11 dans `engine_stereo_eye_resources.cpp::begin_pair()` (évite ~30 appels COM sous verrou GPU à chaque frame).
  - Désactivation du recensement diagnostic des 68 draw-lists par œil en mode production dans `engine_stereo_owner_pass.cpp::end_view()` (conservé uniquement si `vr_debugViewProbes` est actif).
  - Suppression de l'appel COM `GetDevice()` redondant par frame dans `desktop_mirror.cpp::prepare()`.
  - Activation de la résolution de timer système 1 ms (`timeBeginPeriod(1)`) et priorité processus `ABOVE_NORMAL_PRIORITY_CLASS` (`vr_component.cpp`) pour stabiliser le pacing OpenXR/OpenVR sur Ryzen.
- **Rebranding du Launcher VR (`src/client/product.hpp`, `launcher.cpp`, `credits.json`, `locales/*.json`, `src/launcher-ui/`)** :
  - Mention claire du **fork optimisé par TechnoidVR** sur l'accueil, l'en-tête, la notice de démarrage et la page **À propos**, avec un hommage et un bouton direct vers le dépôt original **Overlord VR d'Aeka0** (`https://github.com/Aeka0/Overlord`).

## 6. Optimisations Phase 4 (D3D11 / OpenXR / HUD & Panneaux Spatiaux)
- **Blit OpenXR sans CommandList ni shader en jeu normal (`texture_blit.hpp`, `texture_blit.cpp`)** :
  - Ajout d'un chemin rapide `CopyResource` lorsque `encoding::linear`, `dim == 0` et que la texture source et la swapchain OpenXR ont les mêmes dimensions, format (`DXGI_FORMAT_R11G11B10_FLOAT`), mono-échantillon et mipmap `0`, avec cache de validation source (`validated_device_`, `sources_` avec `description`). Élimine 2 créations/exécutions de `ID3D11CommandList` (`FinishCommandList` + `ExecuteCommandList(..., TRUE)`) par frame sur `atidxx64.dll` / DX11.
- **Réduction des relectures SRV/UAV et appels `Set*` d'isolation (`engine_stereo_eye_resources.cpp`)** :
  - Cache négatif `cache.foreign` sécurisé pour `original` et `eyes[0/1/2]` (`match_target_resource` empêchant toute texture œil non suivie d'y entrer, gardé par `foreign_cache_current()`).
  - *Dirty-checks* avant `OMSetRenderTargets*`, `CSSetUnorderedAccessViews` et `*SetShaderResources` dans `rebind_inherited_state()` et `restore_inherited_state()`.
  - Suivi des stages SRV touchés (`pair.touched_srv_stages`) et de l'état post-restauration (`post_restore_dirty`, `note_external_views_bound`) pour sauter les lectures des stages propres et éviter le balayage redondant `count_bound_target_views()` dans `end_pair()` tout en conservant la détection stricte de quarantaine (`untracked_right_srv`).
- **Fusion CommandList & caches COM HUD/Panneaux (`spatial_panel_renderer.hpp`, `spatial_panel_renderer.cpp`, `native_hud_capture.cpp`)** :
  - `spatial_panel::renderer` met en cache les métadonnées internes (`background_width_/height_/format_/view_format_/mipmapped_`, `ink_width_/height_`), évite les doubles validations `GetDevice()`/`GetResource()` via `validated_surfaces` et fusionne les deux `CommandList` de `draw_layers()` en un seul enregistrement/exécution (avec déliaison explicite de `ink_target_` avant lecture de `ink_view_`).
  - `native_hud_capture.cpp` met en cache les états immuables (`blends`, `depth_states`, `rasters`), restaure 1 seule cible OM lorsque les slots `1..7` sont vides et passe les compteurs/états diagnostiques en `std::memory_order_relaxed`.

## 7. OptiLord v0.1.0 — Identité dédiée & Anti-Stutters Chargement/Streaming
- **Identité OptiLord & versionnage indépendant (`v0.1.0`)** :
  - Mise à jour de `version.json` (`"name": "0.1.0"`, `file_version: [0, 1, 0, 0]`, `prerelease: true`), `src/client/product.hpp` (`name = "OptiLord"`), `src/client/resource.rc`, `assets/steamvr/overlord.vrmanifest` (`"OptiLord (Overlord VR)"`) et du launcher multi-langues (`index.html`, `App.tsx`, 9 fichiers `locales/*.json`), avec mention de la base **Overlord Beta 8** et remerciements à **Aeka0**.
  - Conservation volontaire de `overlord.exe` / `overlord-debug.exe`, `application_key` (`"overlord"` / `"overlord.debug"`) et `overlord.vrmanifest` pour assurer la compatibilité SteamVR sans rupture.
- **Filtrage lock-free des vues de streaming (`src/client/component/vr/engine_stereo_eye_resources.cpp`)** :
  - Ajout d'un tableau de surveillance par *seqlock* (`watched_resources`, `watched_resource_count`, `watched_resources_seq`, `publish_watched_resources()`, `is_watched_resource()`) contenant les ressources `original` et `eyes` actuellement suivies.
  - `device_create_view_stub` n'incrémente `view_creation_epoch` que lorsque `is_watched_resource(resource)` est vrai (avec repli conservateur pendant une publication ou sur pointeur nul), supprimant les invalidations massives de `cache.foreign` et les re-balayages 768 SRV provoqués par le streamer de textures en arrière-plan tout en préservant la détection ABA des vues recréées sur les cibles stéréo.
- **Suppression de syscalls noyau et contention dans `src/client/component/d3d11/texture_diagnostics.cpp`** :
  - Remplacement des deux appels `ReadProcessMemory(GetCurrentProcess(), ...)` par `CreateTexture2D` dans `copy_pointer()` par une copie protégée SEH (`__try` / `__except (EXCEPTION_EXECUTE_HANDLER)` + `std::memcpy`, sans objet à destructeur non trivial).
  - Ajout de `identity_count` (`std::atomic<std::size_t>`) mis à jour dans `identify()` afin que `create_texture_stub()` ne scanne que les slots réellement occupés (`0..count-1` au lieu de 32).
  - Pré-vérification relâchée `target.active.load(std::memory_order_relaxed)` avant `target.writing.test_and_set(std::memory_order_acquire)` dans `begin_active()` pour éviter le *cache-line bouncing*.
- **Cache de taille fichier & synchronisation I/O (`src/client/component/database.cpp`)** :
  - Calcul unique de `uint64_t size` dans `bnet_file_handle_t` à l'ouverture (`bnet_fs_open_file_stub`), supprimant les allers-retours `seekg(0, std::ios::end)` / `tellg()` à chaque lecture (`bnet_fs_read_stub`) ou requête de taille (`bnet_fs_size_stub`).
  - Protection par `std::mutex bnet_file_handles_mutex` des accès à `bnet_file_handles` et `handle_allocator` (`bnet_fs_open_file_stub`, `bnet_fs_read_stub`, `bnet_fs_tell_stub`, `bnet_fs_size_stub`, `bnet_fs_close_stub`, `bink_io_read_stub`, `bink_io_seek_stub`, `close_fastfile_handles()`), en libérant le mutex avant tout appel aux hooks d'origine.

## 8. OptiLord v0.2.0 — Optimisations CPU Hot-Path Scènes Denses (Opération Wolverines)
- **Suppression de verrous, copies COM et sondes post-bootstrap dans le renderer stéréo (`src/client/component/vr/engine_stereo_renderer.cpp`)** :
  - Dans `backend_target_select_probe_stub`, suppression de `d3d11::get_device_snapshot()` (évite la prise de `graphics_mutex` et 3 paires `AddRef`/`Release` COM à chaque `R_SetRenderTarget`) et de l'appel inutilisé `get_current_binding()`, en passant directement `owner_pass->context.Get()` et `0` à `engine_stereo_owner_pass::note_target_selection()`.
  - Dans `h2_query_publish_probe_stub` et `record_h2_query_result`, contournement post-bootstrap dès que `bootstrap_complete()` retourne `true`.
- **Évitement d'`OMGetRenderTargets` sur les cibles non-display (`src/client/component/vr/engine_stereo_owner_pass.cpp`)** :
  - Dans `note_target_selection()`, déplacement de `OMGetRenderTargets` à l'intérieur du seul chemin `target_index < target_count` après résolution de `target_index`, épargnant environ **95 %** des cibles intermédiaires non-display dans les scènes chargées.
- **Anneau lock-free à 8 vues pour le cache négatif d'ombre solaire (`src/client/component/vr/engine_stereo_sun_shadow.cpp`)** :
  - Remplacement du cache négatif à 1 seule entrée par un anneau lock-free de 8 vues non-matching (`non_matching_views`, remis à zéro dans `watch()` et `unwatch()`), supprimant les évictions répétées lorsque plusieurs cibles hors ombre solaire alternent.
  - Exploité à la fois dans `dsv_matches_watched()` et dans `write_view()` afin d'éviter des appels COM `GetResource()` redondants.
- **Armement conditionnel de la capture HUD sur `DrawIndexed` (`src/client/component/vr/native_hud_capture.cpp`, `src/client/component/vr/engine_stereo_draw_indexed.cpp`)** :
  - Suppression de l'enregistrement permanent de `copy_draw` dans `post_unpack()` ; `copy_draw` n'est désormais armé que pendant l'exécution de `native_hud_capture::handle()` (et restauré à `nullptr` en sortie).
  - Lecture de `copy_observer` en tout premier dans `draw_stub()` (avant `live_installed` et `live_context`), court-circuitant **99,9 %** des appels `DrawIndexed` 3D hors HUD.
- **Rationalisation de l'isolation par œil (`src/client/component/vr/engine_stereo_eye_resources.cpp`)** :
  - Rationalisation de `active_isolated_eye()` : un seul appel à `GetCurrentThreadId()`, vérifications thread-local effectuées avant les lectures atomiques cross-core (tout en conservant la détection d'erreur de contexte/génération).
  - Factorisation du chemin unitaire dans `rewrite_admitted_view_synced()` afin que `rewrite_admitted_view_batch()` n'exécute `sync_foreign_caches_on_owner()` et `foreign_cache_current()` qu'une seule fois avant la boucle, supprimant la double consultation `foreign_cache_current()` / `foreign_cached()` par slot tout en préservant intégralement la table `watched_resources` de la v0.1.0.

## 9. OptiLord v0.3.0 — Échelle de rendu GPU, FidelityFX CAS & Hidden Area Mesh OpenXR (`XR_KHR_visibility_mask`)
- **Découplage de l'échelle de rendu VR (`src/client/component/vr/render_tuning.hpp`, `openxr_runtime.cpp`, `openvr_runtime.cpp`)** :
  - `vr::render_tuning` centralise les réglages atomiques `render_scale` (`vr_renderScale`), `sharpening` (`vr_sharpening`) et `hidden_area_mesh` (`vr_hiddenAreaMesh`), ainsi que `scale_extent()` qui borne l'échelle à `[0,50 ; 1,20]` (court-circuit exact à `1,0` autour de `±0,005`), aligne les dimensions sur des multiples de 8 (`>= 64`, bornés par les limites maximales du runtime) et valide via `engine_scene_resolution::valid()`.
  - `openxr_runtime.cpp` et `openvr_runtime.cpp` conservent les dimensions recommandées pleines du casque pour les swapchains OpenXR/OpenVR (`eyes_`, `status_.recommended_eye_width`/`height`), tout en demandant `{scene_width_, scene_height_}` à `engine_scene_resolution::request()` et en validant l'armement/préparation natifs (`try_arm_native()` sous OpenXR ; `activate_submit_format_candidate()`, `try_arm_gpu_transport()`, `prepare_frame()` et `prepare_scene()` sous OpenVR) sur `scene_width_`/`scene_height_`.
  - Réduire `vr_renderScale` (ex. `80 %`–`90 %`) diminue la charge pixel-shader 3D dans les scènes extérieures denses tout en conservant le HUD et les swapchains OpenXR à leur netteté native.
- **FidelityFX CAS 5-taps en espace linéaire (`src/client/component/vr/texture_blit.hpp`, `src/client/component/vr/texture_blit.cpp`)** :
  - Ajout du champ `sharpness` dans `draw_request` et extension du constant buffer `Parameters` à 32 octets (`mode`, `dim`, `sharpness`, `rcp_source_size`).
  - Dans `pixel_source` (`SOURCE_LINEAR`), lorsque `sharpness > 0.001`, exécute un filtre AMD FidelityFX Contrast Adaptive Sharpening 5-taps en espace HDR linéaire à partir du pixel central et de ses 4 voisins cardinaux (`rcp_source_size`). Le chemin rapide `CopyResource` sans shader reste actif uniquement lorsque `encoding::linear`, `dim == 0`, `sharpness == 0` et que les dimensions source/cible correspondent.
  - `openxr_runtime.cpp::draw_eye()` transmet en direct `render_tuning::sharpening` (ajustement immédiat sans redémarrage).
- **OpenXR Hidden Area Mesh (`XR_KHR_visibility_mask`) en Reverse-Z (`openxr_dispatch.{hpp,cpp}`, `openxr_runtime.cpp`, `hidden_area_mask.hpp`, `engine_stereo_eye_resources.{hpp,cpp}`, `engine_stereo_output_merger.{hpp,cpp}`)** :
  - `dispatch_table::load_instance()` résout optionnellement `xrGetVisibilityMaskKHR` ; `openxr_runtime.cpp` active `XR_KHR_VISIBILITY_MASK_EXTENSION_NAME`, interroge `XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR` via `refresh_visibility_mask()` après `xrCreateSession` et sur `XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR` dans `poll_events()`, et transmet les tangentes de projection depuis `locate_views()` vers `vr::hidden_area_mask`.
  - `vr::hidden_area_mask` projette les sommets du masque plan tangent vers les coordonnées NDC de chaque œil et écrit `z = 1.0` (plan proche en profondeur *reverse-Z*, `D3D11_COMPARISON_ALWAYS`, `D3D11_DEPTH_WRITE_MASK_ALL`, `RenderTargetWriteMask = 0`) via un contexte différé dédié (`native_conversion_command_list::mark_recording_context` / `mark`) et `ExecuteCommandList(..., TRUE)` pour restaurer intégralement l'état D3D11 du contexte immédiat.
  - `engine_stereo_eye_resources::current_isolated_eye()` identifie l'œil isolé actif (`< 2`) sur le contexte, et `engine_stereo_output_merger::set_post_clear_depth_hook()` déclenche `post_clear_depth_hidden_area` après chaque `ClearDepthStencilView` en *reverse-Z* (`D3D11_CLEAR_DEPTH`, `depth == 0.0f`) hors `active_replay_guard` et `active_transaction` (protégé par le drapeau TLS `in_post_clear_hook`).
- **Paramètres Launcher, 9 Langues & Tests (`settings.hpp`, `vr_component.cpp`, `settings-schema.json`, `locales/*.json`, `tests/vr/mock_smoke.cpp`, `tests/vr/output_merger_probe.cpp`)** :
  - Enregistrement des 3 dvars `DVAR_FLAG_SAVED` (`vr_renderScale` 50–120 %, `vr_sharpening` 0–100 %, `vr_hiddenAreaMesh` activé par défaut) dans `vr_component.cpp`, ajout du groupe `settings.performance` en fin de catégorie `basics` dans les 9 locales, et validation automatisée sous WARP (upscaling CAS, `scale_extent()`, et estampillage profondeur *reverse-Z*).

## 10. OptiLord v0.4.0 — Contention CPU & Frame Pacing
- **Diagnostic par télémétrie en session réelle (`vr_debugWaitTiming` — Meta Quest 3 / RX 9070 / Ryzen 7 5700X)** :
  - Les captures `vr_debugWaitTiming` en jeu réel ont montré que `scheduler::r_end_frame_stub()` (thread Frontend/Renderer de H2) subissait des blocages de **`1,0–4,5 ms` par frame (jusqu'à `87,5 ms` max)** sur `runtime.facade.enabled` lors des appels à `applied_enabled()` et `poll_capture()`, bloqué derrière le thread Present/Backend qui conservait `mutex_` pendant les appels bloquants `xrWaitFrame` / `xrWaitSwapchainImage`.
- **Suppression des verrous sur `requested_enabled()`, `applied_enabled()` et `poll_capture()` (`src/client/component/vr/runtime_backend.cpp`, `openxr_runtime.cpp`, `openvr_runtime.cpp`, `tests/vr/mock_smoke.cpp`)** :
  - Ajout de `desired_enabled_atomic_` et `applied_enabled_atomic_` (`std::atomic_bool`) aux côtés de `session_running_atomic_` dans `openxr_runtime.cpp` et `openvr_runtime.cpp`, synchronisés dans `publish_status_locked()` et à chaque transition de `status_.desired_enabled` / `status_.applied_enabled` afin que `requested_enabled()` et `applied_enabled()` effectuent des lectures atomiques sans verrou (`std::memory_order_acquire`).
  - Suppression des verrous `mutex_` dans `requested_enabled()`, `applied_enabled()` et `poll_capture()` au sein de `runtime_backend::implementation` et `runtime_backend` (`runtime_backend.cpp`), en s'appuyant sur l'immutabilité du choix de backend après construction tout en préservant la synchronisation des transitions Present/cycle de vie, avec couverture de `openxr::runtime_backend` (`run_happy_path`) et `vr::runtime_backend` (`expect_facade_lockfree_queries`) dans `tests/vr/mock_smoke.cpp`.
- **Épinglage de priorité des threads VR critiques (`src/client/component/vr/vr_component.cpp`)** :
  - Ajout de l'assistant gardé par `thread_local` (`boost_critical_vr_thread_priority()`) épinglant `THREAD_PRIORITY_ABOVE_NORMAL` via `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL)` une seule fois par thread sur `on_present_pre()` (thread Present/Backend/OpenXR) et `on_renderer_frame()` (thread Renderer/Frontend), complétant `ABOVE_NORMAL_PRIORITY_CLASS` et le timer système `1 ms` (`timeBeginPeriod(1)`).
- **Chemin rapide sans observateur pour les opérations ressources D3D11 (`src/client/component/vr/engine_stereo_resource_ops.cpp`)** :
  - Ajout de `record_call_if_no_observers()` vérifiant `active_observer_mask.load(std::memory_order_acquire) == 0`, tout en conservant à l'identique le filtrage `expected_context == 0`, `foreign_context_calls` et l'incrémentation de `calls[index(operation)]` avant de contourner `publish()`.
  - Appliqué sur les 8 stubs d'opérations ressources (`CopySubresourceRegion`, `CopyResource`, `UpdateSubresource`, `CopyStructureCount`, `ClearUnorderedAccessViewUint`, `ClearUnorderedAccessViewFloat`, `GenerateMips`, `ResolveSubresource`) pour éviter `_ReturnAddress()` et la construction de la structure `event` de 80 octets sur plus de **500 000 appels D3D11 de copie/mise à jour** lorsqu'aucun observateur n'est actif.

## 11. OptiLord v0.5.0 — Installateur & Désinstallateur Propre
- **Détection automatique multi-sources de MW2CR (`tools/installer/game_paths.hpp`)** :
  - `detect_game_directories()` collecte, normalise (`normalize_candidate_path()`), valide (`is_valid_game_dir()` exigeant `MW2CR.exe` ou `h2_sp64_bnet_ship.exe`) et déduplique sans sensibilité à la casse les dossiers de jeu depuis cinq sources :
    1. Chemin d'installation existant dans `HKCU\Software\OptiLord` (`InstallDir`).
    2. Clés Windows Uninstall (`HKLM\SOFTWARE\WOW6432Node\...`, `HKLM\SOFTWARE\...`, `HKCU\SOFTWARE\...`) correspondant à MW2CR (`DisplayName`) via `InstallLocation` et `DisplayIcon`.
    3. Configuration JSON Battle.net (`%APPDATA%\Battle.net\Battle.net.config` -> `Client.Install.DefaultInstallPath`).
    4. Tous les disques fixes (`GetLogicalDriveStringsW()` + `DRIVE_FIXED`) sur les emplacements usuels Battle.net, Blizzard, Epic/Epic Games, SteamLibrary (`steamapps\common\...`), `Program Files`, `Games` et racine du disque.
    5. Dossier de travail courant et dossier parent de l'exécutable installateur.
- **Assainissement Zip-Slip & protection contre les points d'analyse (`tools/installer/install_engine.hpp`)** :
  - `safe_relative_zip_path()` impose des chemins relatifs stricts avant toute extraction ou suppression par manifeste : rejette les chaînes vides, `/` ou `\` initiaux/finaux, caractères de contrôle (`< 0x20`, `0x7f`), lecteurs `:` et caractères Windows interdits (`< > " | ? *`), segments `.`/`..`/vides, points ou espaces finaux et noms réservés Windows (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`).
  - `has_reparse_component()` / `is_reparse_point()` bloque toute traversée ou écriture à travers des liens symboliques ou jonctions (`FILE_ATTRIBUTE_REPARSE_POINT`).
- **Purge des binaires obsolètes avant mise à jour (`clean_mod_files()`, `inspect_installation()`)** :
  - Élimine les plantages dus aux fichiers résiduels ou versions mixtes lors d'une mise à jour depuis Overlord ou une version antérieure d'OptiLord (`overlord.pdb`, `overlord-debug.exe`, `h2-mod/`, `steamvr/`, `vr_input/`, etc.).
  - Bloque l'opération si `find_running_game_processes()` détecte `overlord.exe`, `overlord-debug.exe`, `mw2cr.exe` ou `h2_sp64_bnet_ship.exe`, puis supprime les entrées du manifeste précédent (`optilord-install-manifest.json`), les fichiers racine connus du mod et les sous-dossiers mod (`h2-mod`, `steamvr`, `vr_input`, `licenses`, `docs`) avant l'extraction atomique par blocs de 64 Ko via `.tmp` + renommage (`unzOpen2_64`).
- **Découpe du stub PE pour `OptiLord-Uninstall.exe` (`compute_pe_image_file_size()`)** :
  - L'exécutable auto-extractible `OptiLord-v0.5.0-Setup.exe` embarque l'archive `ZIP_DEFLATED` directement après le stub Win32 compilé (`tools/build_installer.py`).
  - Pendant `install_mod()`, `compute_pe_image_file_size()` lit `IMAGE_DOS_HEADER::e_lfanew`, `IMAGE_NT_SIGNATURE`, `SizeOfHeaders` et `max(PointerToRawData + SizeOfRawData)` sur tous les en-têtes `IMAGE_SECTION_HEADER`, ne copiant que l'image PE exacte `[0 .. pe_size)` vers `OptiLord-Uninstall.exe` (`< 2 Mo`, sans la charge ZIP).
  - Lorsque `OptiLord-Uninstall.exe` est exécuté depuis le dossier du jeu, `uninstall_mod()` lance un `cmd.exe` détaché et masqué (`CREATE_NO_WINDOW`) depuis `System32` pour supprimer le désinstallateur après fermeture sans laisser aucun verrou sur le dossier.
- **Protection des fichiers du jeu & des sauvegardes (`players2/`) (`is_protected_game_path()`)** :
  - `is_protected_game_path()` et `is_protected_top_dir_name()` interdisent toute écriture ou suppression touchant `MW2CR.exe`, `h2_sp64_bnet_ship.exe`, `MW2CR Launcher.exe`, `Data/`, `main/`, `players2/`, `.battle.net/` et `mods/`.
  - `diagnose/` et `minidumps/` sont également conservés par défaut lors des mises à jour et désinstallations sauf activation explicite de `remove_diagnostics` / `--remove-diagnostics`, le tout validé bout-en-bout par `tests/vr/test_installer.py`.
