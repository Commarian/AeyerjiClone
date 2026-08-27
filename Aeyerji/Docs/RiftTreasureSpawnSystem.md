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
   - Import `Data/Rifts/TreasureLootDT.json` for starter rows `Treasure_Mobs`, `Treasure_Elite`, and `Treasure_Boss`.
   - The currently loaded `BP_AeyerjiLootTable` pools have blank `Source Tag` values, which behave as wildcards. Until those tags are authored, every starter row resolves the first Mobs pool rather than its intended Mobs, Elite, or Boss pool.
   - Assign these source tags to the corresponding global loot-table pools before using the starter rows:
     - `MobsMap1LootSet` pool: `Loot.Source.Mobs`
     - `Boss` pool: `Loot.Source.Boss`
     - `Elite` pool: `Loot.Source.Elite`
     - `Survival` pool: `Loot.Source.5RoundsSurvived`
   - Alternatively, author a dedicated Rift treasure pool with a `100%` overall drop gate and give it a dedicated source tag. Point the Rift rows at that tag instead of changing ordinary mob-drop behavior.
   - For normal Rift treasure, configure only `Enabled`, `Source Tag`, `Minimum Rarity`, and `Drops Per Chest`.
     - `Source Tag` selects the tagged pool in `BP_AeyerjiLootTable`.
     - `Minimum Rarity` is the floor applied to each selected item.
     - `Drops Per Chest` is the requested number of loot-service rolls. Rolls that are suppressed or cannot resolve a valid item do not create an empty chest; that candidate is skipped with a diagnostic log.
   - The advanced fields are exceptions rather than ordinary tuning:
     - `Drop Count Variance` adds a random amount to `Drops Per Chest`; leave it at `0` for a predictable chest.
     - `Forced Item Definition` bypasses source-pool item selection. Use it only for a curated reward or a temporary test, then clear it. The item must be eligible for the current player level; otherwise the candidate is skipped with a warning.
     - `Drop Mode` controls the existing authoritative pickup ownership policy. Leave the default `Drop Only For Instigator` unless the Rift explicitly needs a shared reward.
   - Player/enemy level, world tier, pity, rarity weights, item-level jitter, uniqueness/bucket rules, debug settings, and reward-presentation lifetime are deliberately not Rift-treasure row settings. Runtime supplies the relevant live context and the ordinary loot system keeps ownership of its global policy.
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
6. Repeat with two clients or a dedicated server. The selected loot-profile row's `Drop Mode` semantics remain authoritative; the placement system does not choose shared versus personal rewards.
7. Enable auto-open/auto-collect at level 1, then repeat at maximum-level mode.
8. Run the editor simulation and inspect selection frequencies for unintended point bias.

## Verification performed

- `AeyerjiEditor Win64 Development` built successfully after the DataTable-row conversion.
- Headless UE automation `Aeyerji.Rift.Treasure.SeededSelection` passed; it verifies identical selections for the same seed and enforces the configured hard chest separation.
