# Rift Treasure Spawn System

## Purpose

Rift treasure uses designer-authored `AAeyerjiTreasureSpawnPoint` actors as possible chest locations. The authority validates those points against the current Rift's NavMesh, selects a seeded spatially weighted layout, spawns the normal reward chest class, and gives that chest an explicit `FAeyerjiTreasureLootProfileRow` from a DataTable.

The system intentionally separates:

```text
authored point -> selected point for this run -> chest presentation -> existing loot/pickup flow
```

No map, Blueprint, or DataTable binary was changed by the C++ implementation. The setup below is therefore required before enabling it for a Rift.

## Required editor setup

1. Restart Unreal after compiling so the `Aeyerji Treasure Spawn Point` actor and **Rift Treasure Loot Profile** DataTable row struct appear.
2. Create one DataTable using row struct `FAeyerjiTreasureLootProfileRow`, for example `/Game/Systems/Rifts/DT_RiftTreasureLootProfiles`.
   - Import `Data/Rifts/TreasureLootDT.json` for starter rows `Treasure_Mobs`, `Treasure_Elite`, and `Treasure_Boss`. `Treasure_Mobs` is the standard starter row and uses the dedicated `Loot.Source.Rift.Treasure` source.
   - Author actual item definitions once in `/Game/Loot/BP_AeyerjiLootTable`; the Rift DataTable deliberately does not contain item assets, chances, or level gates.
     1. Add a **new first pool** in `BP_AeyerjiLootTable`. Pool order matters because the first matching pool wins, and the existing pools currently have blank `Source Tag` values that act as wildcards.
     2. Set that pool's `Source Tag` to `Loot.Source.Rift.Treasure`, leave its world-tier and level bounds at `0`, and add item definitions directly to its `Entries` array. Do not create a separate entry-set DataAsset for this starter pool.
     3. For a guaranteed level-one proof pool, add these existing definitions as Common entries: `DI_BasicSword_Json`, `DI_BasicHammer_Json`, `DI_Shield_Json`, `DI_SilverHeart_Json`, and `DI_PoisonousDagger_Json` from `/Game/Inventory/Items/JsonImported`.
     4. Set each entry's `Percentage Chance To Drop In Pool` to `100`, `Weight` to `1`, and `Min Level` / `Max Level` to `0`. The exact chest items, rarities, weights, and level gates belong here, not on a Rift row.
   - Later, give the old Mobs, Boss, Elite, and Survival pools their own source tags so they stop behaving as wildcards:
     - `MobsMap1LootSet` pool: `Loot.Source.Mobs`
     - `Boss` pool: `Loot.Source.Boss`
     - `Elite` pool: `Loot.Source.Elite`
     - `Survival` pool: `Loot.Source.5RoundsSurvived`
   - Every Rift row has exactly four editable settings: `Enabled`, `Source Tag`, `Minimum Rarity`, and `Drops Per Chest`.
     - `Source Tag` selects the pool that owns the real item definitions.
     - `Minimum Rarity` is the floor applied to each selected item.
     - `Drops Per Chest` is a fixed requested number of loot-service rolls. Rolls that are suppressed or cannot resolve a valid item skip the chest rather than creating an empty chest.
   - Player/enemy level, world tier, pity, rarity weights, item-level jitter, uniqueness/bucket rules, debug settings, reward presentation, and pickup ownership are deliberately not Rift-treasure row settings. Runtime supplies live gameplay context; Rift chest pickups remain server-authoritative and personal to the instigating player.
   - A row does not own a separate reward implementation; the normal `ULootService`, reward chest, pickup, and inventory path remain in use.
3. Create `BP_RiftTreasureSpawnPoint` as a Blueprint child of `AAeyerjiTreasureSpawnPoint`.
   - Assign a static mesh or simple marker to the inherited `PreviewMesh` component.
   - Leave `Show Preview` enabled for editor placement. The component is always `Hidden In Game`, has no collision, and never affects navigation.
4. Place approximately 20 `BP_RiftTreasureSpawnPoint` instances first. Expand toward the intended hundred only after the first validation and playtest.
   - Leave `Enabled` true and give important locations a positive `Spawn Weight`.
   - Use `Zone Id` to bias distribution across areas, not to impose a fixed chest count.
   - Set `Rift Zone Id` only if multiple Rift point sets are loaded together; it must match `ZoneRunDefinition.ZoneId`.
   - Use point-level chest/DataTable-row overrides only for special locations such as a hidden side room.
5. Open the Rift's `ZoneRunDefinition` and configure **Treasure Spawn Config**.
   - Set `Enabled` only once defaults and candidate points are ready.
   - Set `Default Chest Class` to `/Game/Loot/BP_RewardChest` or a derived reward chest.
   - Set `Default Loot Profile Row` to `Treasure_Mobs` in `TreasureLootDT`.
   - Start with `Minimum Chests = 10`, `Maximum Chests = 13` and tune start exclusion/separation for the map.
   - If the LevelDirector transform is not the practical Rift entry, assign a unique actor tag in `Rift Start Actor Tag` to a nearby start marker.
6. Select the LevelDirector in the editor and use **Validate Rift Treasure Spawn Points**.
   - It records a validation state on each point and emits totals for disabled, out-of-scope, missing-config, NavMesh, start-exclusion, and unreachable failures.
   - A valid point keeps its visual transform. The nearby projected NavMesh position is used only as the player interaction/navigation anchor.
7. Use **Simulate Rift Treasure Layouts** after point placement. It runs the configured number of deterministic samples and logs selection hits per valid point.

## DataTable conversion

`UAeyerjiTreasureLootProfile` has been removed; Rift treasure loot policies now exist only as `FAeyerjiTreasureLootProfileRow` rows. If any old profile asset was created, copy its values into a row and remove that asset in the editor.

`DefaultLootProfile` and point-level `LootProfileOverride` have become `Default Loot Profile Row` and `Loot Profile Row Override`. Reassign any existing ZoneRunDefinition/spawn-point defaults (and reconnect any Blueprint property graphs) to the intended table and row. Leave both override fields empty only when the Rift default should apply; a half-filled handle is intentionally treated as invalid and reported by treasure validation.

The row now contains only the four fields shown in the DataTable Row Editor. `Drop Count Variance`, `Fixed Item Definition`, and per-row `Drop Mode` were intentionally removed because the DataTable Row Editor hides advanced properties while its grid still shows them. Reimport `TreasureLootDT` after compiling; existing row handles remain valid because the row struct and row names are unchanged.

## Runtime behavior

- `AAeyerjiGameState::Server_StartRun` invokes `AAeyerjiLevelDirector::SpawnRiftTreasuresForRun` after the server has frozen the Rift run serial and seed.
- Only the server gathers candidates, rolls the requested chest count, picks points, rolls loot, and spawns chest actors. Clients receive the replicated chests and their replicated reward summary/interaction anchor.
- Candidate validity requires enabled state, Rift-zone scope, chest class, an enabled valid typed loot-profile row, a nearby NavMesh projection, optional start exclusion, and optional synchronous path reachability from the Rift start.
- Selection uses the run seed, stable actor-path ordering, authored weights, hard minimum separation, soft distance weighting, and optional zone-repeat/unused-zone weighting.
- A Rift chest has its ground snap disabled before reward initialization, preserving the designer-authored chest transform. Its separate replicated interaction anchor lets movement/range validation resolve on nearby NavMesh.
- Existing reward release guards prevent duplicate reward generation/release. Manual interaction and auto-open both call the same chest release request. Auto-collect configures the existing authoritative pickup path; if inventory rejects an item, the pickup remains in the world.
- Rift end/reset destroys only Rift-owned reward chests and the unretrieved pickups tracked by those chests.

## Auto-open and auto-collect

Both are opt-in in **Treasure Spawn Config** and disabled by default.

- For development, enable auto-open and use `Auto Open Unlock Level = 1`.
- For a max-level quality-of-life version, enable `Require Max Character Level For Auto Open`; C++ queries `UAeyerjiDifficultySettings::GetMaxGameplayLevel()` instead of hardcoding a cap.
- `Auto Open Radius` is checked by the authority against the interaction anchor. It invokes the normal `HandleReleaseRequested` route, so a Blueprint chest opening animation still controls when `ReleaseStoredLoot` occurs.
- `Enable Auto Collect` only enables the existing pickup overlap/inventory transfer path after release. It never grants a separate direct reward.

## Playtest checklist

1. Run the editor validation action and address every unexpected invalid/unreachable point.
2. Start a solo Rift and inspect `[Treasure]` logs for the run seed, requested count, candidate summary, chosen points, and spawned count.
3. Confirm a chest visually remains at its placed transform while click-to-move/range validation uses the nearby navigation anchor.
4. Manually interact with a chest and confirm one release only, normal pickup spawning, and that an inventory-full rejection leaves the pickup alive.
5. Reset/end the Rift with unopened and released chests present; confirm the new run has no stale chest or pickup actors.
6. Repeat with two clients or a dedicated server. Rift chest rolls and released pickups remain authoritative and personal to the instigating player.
7. Enable auto-open/auto-collect at level 1, then repeat at maximum-level mode.
8. Run the editor simulation and inspect selection frequencies for unintended point bias.

## Verification performed

- `AeyerjiEditor Win64 Development` built successfully after the DataTable-row conversion.
- Headless UE automation `Aeyerji.Rift.Treasure.SeededSelection` passed; it verifies identical selections for the same seed and enforces the configured hard chest separation.
