# Aeyerji

**A C++-first Unreal Engine 5.8 multiplayer action-RPG portfolio project built around ability-driven combat, loot, and progression.**

Fight through enemy-packed arenas suspended above the clouds, combining melee and ranged attacks with movement and area abilities. Violet-lit architecture, bright combat effects, and a shared animated UI theme define the current visual direction.

## Technical highlights

- **Gameplay and GAS combat:** native player command flow, targeting, held attacks, ability activation, cooldowns, resource costs, and physical damage rules.
- **Online session flow:** Unreal Online Subsystem integration for hosting, searching, joining, advertising, and progressing party/lobby state.
- **Encounter pacing:** data-driven spawn groups, elite pools, rift progress, population staging, safe spawn selection, and kill-velocity pacing.
- **Inventory and persistence:** server-validated item operations, replicated equipment and grid state, loot resolution, snapshots, and save-data application.
- **Automation and diagnostics:** focused combat, damage, inventory, and tuning tests alongside runtime telemetry and data-driven balance controls.

## Selected source code

The mirror is organized so a reviewer can jump directly from the systems above into representative C++ implementation:

- [Player command and targeting flow](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/AeyerjiPlayerController.cpp) and [primary melee ability](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/Private/Abilities/GA_PrimaryMeleeBasic.cpp)
- [Online session and lobby subsystem](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/Private/Frontend/AeyerjiSessionSubsystem.cpp)
- [Encounter director and rift population pacing](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/Private/Director/AeyerjiEncounterDirector.cpp)
- [Replicated inventory, equipment, and save-data handling](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/Private/Items/InventoryComponent.cpp)
- [Combat responsiveness automation test](https://github.com/Commarian/AeyerjiClone/blob/main/Aeyerji/Private/Tests/AeyerjiCombatResponsivenessTest.cpp)

[![Combat beside a glowing arena structure, with enemies, ability slots, and progression HUD](Screenshot%20%2828%29.png)](Screenshot%20%2828%29.png)

*Current development gameplay. Art, UI, encounter balance, and effects are still evolving.*

## Screenshots

Click any screenshot to view the full-resolution capture.

| Arena and gameplay HUD | Combat and progression |
| --- | --- |
| [![Player approaching enemies on a suspended arena bridge](Screenshot%20%2821%29.png)](Screenshot%20%2821%29.png) | [![Close-range combat with multiple enemies and active ability cooldowns](Screenshot%20%2824%29.png)](Screenshot%20%2824%29.png) |

[![Violet main menu with character level, gold, and XP](Screenshot%20%2820%29.png)](Screenshot%20%2820%29.png)

*The current main menu, including character progression and the living-menu visual theme.*

<details>
<summary>More combat screenshots</summary>

### Meeting the enemy pack

![Melee enemies closing in along a narrow bridge](Screenshot%20%2822%29.png)

### Fighting through the arena

![Combat spreading onto a wider platform](Screenshot%20%2823%29.png)

### Combat feedback and level progression

![A crowded fight with stagger feedback, combat text, and XP progression](Screenshot%20%2827%29.png)

</details>

## What's in the prototype

- **Combat and abilities:** native click-to-move and held-attack control, actor and ground targeting, melee/ranged attacks, movement skills, cooldowns, and resource costs built on Unreal's Gameplay Ability System (GAS).
- **Loot and character building:** item pickups, a grid inventory, equipment slots, item tooltips, attributes, XP, levels, and gold.
- **Encounters:** enemy AI, encounter directors, data-driven rift pacing, and survival-defense systems.
- **Multiplayer foundations:** server-authoritative gameplay and session, party-browser, and lobby code.
- **Presentation:** reusable C++ UI styling, animated menus, action-bar feedback, gameplay warnings, objective tracking, a minimap, and combat text.
- **Engineering tools:** automated tests, combat diagnostics, and data-driven tuning alongside the gameplay implementation.

These are actively developed systems, not a claim that every mode or interaction is release-ready.

## About this repository

This is a **source-code mirror and project showcase**. The full Unreal project is maintained in **Perforce**; GitHub is used to share the source and development screenshots.

The mirror contains the `Aeyerji` C++ module and its accompanying data and documentation. It does **not** include the complete Unreal project, Content assets, project configuration, or all dependencies needed to launch the game. Cloning this repository alone will not produce a playable build.

## Art and assets

Most of the art currently used in Aeyerji comes from free asset collections provided by Epic Games and serves as placeholder content while the gameplay takes shape. The remaining assets are made by me in **Blender**.

The screenshots show that mix of placeholder and original work in the current prototype. The visual direction is still evolving alongside the combat, environments, and UI.
