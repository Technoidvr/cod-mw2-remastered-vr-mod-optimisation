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
