# Overlord VR (Beta 8) — Pack d'Optimisations & Correctifs Gameplay (MW2 Campaign Remastered)

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
