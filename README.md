# Aeyerji

## Note that this is a clone of the source files only. Git is not used for revision control of this project, Perforce is used due to better compatibility with Unreal Engine.

Unreal Engine 5.6 action-RPG prototype focused on responsive click-to-move combat, GAS-powered abilities, and a lightweight loot loop. C++ lives in `Source/Aeyerji`.

- **Core gameplay:** `AAeyerjiPlayerController` drives authoritative click-to-move, facing loops, ability targeting, and short-range avoidance. `AAeyerjiCharacter` is the GAS-ready pawn base with attribute sets, stat engine, pickup FX, and default abilities.
- **Abilities:** GAS abilities + data assets for melee/ranged basics, Blink, cone/arc AOE skills (`Source/Aeyerji/Public/Abilities`).
- **Loot & inventory:** ArcInventory-powered pickups (`AeyerjiLootPickup`, `AeyerjiInventoryBPFL`) with hover/highlight and a hold-to-show toggle (IA_ShowLoot).
- **AI:** StateTree tasks for targeting, patrol, and attack range checks (`Source/Aeyerji/Public/Enemy/Tasks`), plus custom movement smoothing.
- **Navigation & avoidance:** Authoritative nav context caching, smart goal selection around targets, optional avoidance profiles, and RVO-friendly movement components.

## Repository layout (selected)
- `Aeyerji.uproject` — UE 5.6 project descriptor.
- `Source/Aeyerji` — C++ gameplay code (Abilities, Enemy AI, Inventory, Player, etc.).
- `Content` — Blueprints, materials, VFX, input assets.
- `Config` — Project and platform config.
- `Design` / `Docs` — Design docs (e.g., MVP plan PDF).
- `Scripts` — Automation and helpers.

## Requirements
- Unreal Engine 5.6 (EngineAssociation set in `Aeyerji.uproject`).
- Visual Studio 2022 toolchain (or your platform’s C++ toolchain) for native code.

## Controls (default Enhanced Input set)
- Click-to-move and click-to-attack actions are mapped via `IMC_Default` (see `/Game/Player/Input` to remap).
- Hold **Show Loot** (`IA_ShowLoot`, default Left Alt) to highlight pickups.
- Ability targeting supports ground/actor confirmation flows; look for slots in `FAeyerjiAbilitySlot` and related BPs.
