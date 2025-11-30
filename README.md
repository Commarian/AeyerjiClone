# Aeyerji

Unreal Engine 5.6 action-RPG prototype focused on responsive click-to-move combat, GAS-powered abilities, and a lightweight loot loop. C++ lives in `Source/Aeyerji`.

- **Core gameplay:** `AAeyerjiPlayerController` drives authoritative click-to-move, facing loops, ability targeting, and short-range avoidance. `AAeyerjiCharacter` is the GAS-ready pawn base with attribute sets, stat engine, pickup FX, and default abilities.
- **Abilities:** GAS abilities + data assets for melee/ranged basics, Blink, cone/arc AOE skills (`Source/Aeyerji/Public/Abilities`).
- **Loot & inventory:** ArcInventory-powered pickups (`AeyerjiLootPickup`, `AeyerjiInventoryBPFL`) with hover/highlight and a hold-to-show toggle (IA_ShowLoot).
- **AI:** StateTree tasks for targeting, patrol, and attack range checks (`Source/Aeyerji/Public/Enemy/Tasks`), plus custom movement smoothing.
- **Navigation & avoidance:** Authoritative nav context caching, smart goal selection around targets, optional avoidance profiles, and RVO-friendly movement components.

## Repository layout (selected)
- `Aeyerji.uproject` — UE 5.6 project descriptor (see `AdditionalPluginDirectories` for `PackagedPlugins`).
- `Source/Aeyerji` — C++ gameplay code (Abilities, Enemy AI, Inventory, Player, etc.).
- `Content` — Blueprints, materials, VFX, input assets (`/Game/Player/Input/IMC_Default`, etc.).
- `Config` — Project and platform config.
- `Design` / `Docs` — Design docs (e.g., MVP plan PDF).
- `Scripts` — Automation and helpers.

## Requirements
- Unreal Engine 5.6 (EngineAssociation set in `Aeyerji.uproject`).
- Visual Studio 2022 toolchain (or your platform’s C++ toolchain) for native code.
- Plugins enabled in the project (GameplayAbilities/TargetingSystem/StateTree/ArcInventory/etc.). Ensure anything listed under `AdditionalPluginDirectories` is present locally.

## Setup & build
1) Install/launch UE 5.6 and required plugins.  
2) Open `Aeyerji.uproject` (or right-click → Generate Visual Studio project files).  
3) Build the `AeyerjiEditor` target in Development Editor (from IDE or Unreal Editor prompt).  
4) Press Play in-editor. The default map/input assets live under `/Game/Player` and `/Game/Maps` (adjust in Project Settings → Maps & Modes if needed).

## Controls (default Enhanced Input set)
- Click-to-move and click-to-attack actions are mapped via `IMC_Default` (see `/Game/Player/Input` to remap).
- Hold **Show Loot** (`IA_ShowLoot`, default Left Alt) to highlight pickups.
- Ability targeting supports ground/actor confirmation flows; look for slots in `FAeyerjiAbilitySlot` and related BPs.

## Troubleshooting
- If hot-reload fails, close the editor and rebuild from IDE.  
- Stale build data: delete `Intermediate`, `DerivedDataCache`, and let UE regenerate.  
- Nav/ability targeting issues: enable debug draws in `AeyerjiPlayerController` (avoidance debug) or use `Stat StateTree` for AI task tracing.
