# Aeyerji Loot, Reward, Pickup, and Drop Memory System Audit

This document describes the current C++ loot and item-reward implementation. It is intended to be the source-of-truth for what already exists, how the pieces fit together, what is still editor/data work, and what should not be duplicated.

## Current Status

The project already has a full item definition, item instance, inventory, pickup, loot-roll, and save/load stack. The recent C++ work repaired the unsafe pickup paths, added stable item snapshot identifiers, added detailed pickup/save logging, added StateTree-facing loot request/pity condition nodes, added named pity memory, added native loot source/pity tags, and added an opt-in normal enemy death reward hook.

The remaining major work is mostly content and wiring:

1. Configure actual loot tables, source rules, source tags, and pity groups in data assets.
2. Wire `Request Loot Drop` and `Loot Pity Condition` into `ST_RunDirector` or encounter/boss StateTrees.
3. Enable/configure the automatic normal-enemy drop trigger on the enemy archetypes that should use it.
4. Build designer-facing source matrices/debug UI once source routing is final.

No manual `Content` assets were edited as part of this pass.

## System Map

| Area | Primary code | Current behavior |
| --- | --- | --- |
| Item definitions | `UItemDefinition` | Primary data asset for definition keys, tags, rarity affix ranges, default equipment slot, inventory footprint, base modifiers, and gameplay effects. |
| Item instances | `UAeyerjiItemInstance` | Runtime rolled item object with rarity, level, affixes, modifiers, unique id, inventory/equipment identity, and definition pointer. |
| Item generation | `UItemGenerator::RollItemInstance` | Rolls item instances from definition, rarity, level, seed, and slot intent. |
| Inventory ownership | `UAeyerjiInventoryComponent` | Owns authoritative `Items`, `GridPlacements`, and `EquippedItems`; handles add, equip, grid placement, drop, save, and load. |
| Inventory helpers | `UAeyerjiInventoryBPFL` | Provides pickup/reward helpers, including `EquipFirstThenBag` and loot pickup spawning from definitions, instances, roll results, or distributed drop contexts. |
| Pickup actors | `AAeyerjiLootPickup`, `AItemPickup` | Main controller-driven loot pickup plus legacy overlap pickup. Both now keep the pickup alive if inventory rejects the item. |
| Loot context | `FLootContext` | Runtime input for rolls: player, enemy level, player level, world tier, source tag, pity group, forced item, rarity gates, rarity weights, difficulty scale, jitter, and pity overrides. |
| Loot result | `FLootDropResult` | Roll output: definition key, definition pointer, rarity, level, seed, source tag, pity group, and pity-success marker. |
| Loot tables | `UAeyerjiLootTable`, `UAeyerjiLootEntrySet` | Designer-authored pools and reusable entry sets keyed by source tags, world tier, level, rarity weights, and entry chances. |
| Source rules | `ULootSourceRuleSet` | Resolves the best matching source profile from a tag query and priority, then applies it to a base `FLootContext`. |
| Loot service | `ULootService` | Central roll math: rarity selection, table pool selection, entry chances, weighted entries, multi-drop buckets, difficulty scaling, generic legendary pity, and named pity recording. |
| Loot memory | `FPlayerLootStats`, `UPlayerStatsTrackingComponent` | Tracks rarity drops/pickups, drops since last legendary, rolling legendary window, item pickup counts by definition key, and persistent named pity memory. |
| Profile persistence | `UAeyerjiSaveGame`, `CharacterStatsLibrary` | Character/profile save persists inventory, equipment, selected abilities/passives, loot stats, difficulty, and character-scoped world facts. |
| World facts | `UAeyerjiWorldStateSubsystem` | Global/run/character/session world-state registry used by StateTree tasks/conditions and persistent fact saves. |
| StateTree integration | `USTT_RequestLootDropTask`, `USTC_LootPityCondition` | Lets StateTrees ask the loot service to roll/spawn rewards and query loot memory without owning item state. |
| Normal enemy death rewards | `AEnemyParentNative::TrySpawnEnemyDeathRewards` | Optional server-only entry point for normal enemy drops. Disabled by default so bosses/special encounters can continue using StateTree or custom reward paths. |

## Pickup Flow

### `AAeyerjiLootPickup`

`AAeyerjiLootPickup` is the intended main pickup path. It accepts a controller request, checks authority and eligibility, duplicates/prepares the source `UAeyerjiItemInstance`, resolves the pawn inventory component, and grants through `UAeyerjiInventoryBPFL::EquipFirstThenBag`.

Important current guarantees:

1. The pickup is destroyed only after inventory accepts the item.
2. A failed grant leaves the pickup alive.
3. The transfer duplicate gets a fresh unique id and the inventory component as the object outer.
4. Logs use `[InventoryPickup]` and include controller, pawn, definition, rarity, level, unique id, inventory component, and final containment state.
5. A successful result is checked against `Items`, `EquippedItems`, and `GridPlacements` so a "success" has a concrete storage location.

### `AItemPickup`

`AItemPickup` is the legacy overlap path. It now calls `AddItemInstance` and destroys itself only when the add succeeds. If the add fails, it logs the rejection and remains in the world. This prevents the old silent item-loss failure.

## Inventory Insert And Equipment Policy

`UAeyerjiInventoryBPFL::EquipFirstThenBag` is the pickup/reward insertion policy:

1. Rejects null inventory, null item, or missing definition with a distinct result.
2. Adds the item to `Items` first, using skip-auto-place when it intends to equip/bag manually.
3. Attempts to equip into the item definition's preferred equipment slot.
4. If equip is rejected, for example owner level too low or slot unavailable, it still attempts bag placement.
5. If bag placement succeeds, the item remains owned in `Items` and gets a grid placement.
6. If no valid storage target exists, it returns a distinct failure result and logs why.

The important design rule is: equip rejection is not item rejection. The item should still go to the bag when possible.

## Save And Load

Inventory save/load now uses stable item definition identifiers:

| Field | Purpose |
| --- | --- |
| `DefinitionKey` on `FInventoryItemSnapshot` | Stable saved item definition id, populated from `UItemDefinition::GetDefinitionKey()` / `MakeDefinitionKey`. |
| Legacy `Definition` pointer | Compatibility fallback for older saves. If no key exists, load derives a key from this pointer when possible. |

Load behavior:

1. Resolve item definitions by `DefinitionKey` through existing definition lookup logic.
2. Fall back to old `Definition` when loading legacy saves.
3. Skip unresolved item snapshots with a clear `[InventorySave]` warning instead of crashing.
4. Rebuild item objects, equipped entries, and grid placements.
5. Emit a post-load summary with restored item count, equipped count, grid placement count, and grid size.

## Loot Roll Flow

The normal intended roll flow is:

1. A caller builds an `FLootContext` directly or through `ULootSourceRuleSet::ResolveContext`.
2. `ULootService` chooses rarity using base chance, rarity weights, generic pity, named pity, and difficulty scaling.
3. The service resolves a loot table pool/entry or forced item definition.
4. The service emits `FLootDropResult`.
5. Spawning helpers convert that result into one or more `AAeyerjiLootPickup` actors.
6. Pickup grants the item to inventory.
7. Loot memory is updated through player stats tracking and persisted through the character profile.

## Normal Enemy Death Hook

`AEnemyParentNative` now exposes `TrySpawnEnemyDeathRewards`. This is a server-only, opt-in hook called from native death handling when `bSpawnNormalDeathLoot` is enabled.

Current behavior:

1. Disabled by default on the base class to avoid duplicating boss, mini-boss, and StateTree reward drops.
2. Uses `ULootService` and `UAeyerjiInventoryBPFL` spawn helpers; it does not roll items independently.
3. Fills missing runtime context from enemy scaling data: level, difficulty scale, and source tag.
4. Falls back to `Loot.Source.NormalEnemy` when no source tag was supplied by scaling/config.
5. Supports single roll or `FLootMultiDropConfig`.
6. Uses `DeathLootDropMode` to decide whether a drop is instigator-only or distributed.
7. Guards against duplicate rolls if death is reported by more than one system.

Editor setup is still required: enable `bSpawnNormalDeathLoot` only on enemy archetypes/classes that should use this generic route. Bosses, mini-bosses, scripted encounters, and StateTree-authored rewards should leave it off and continue using `Request Loot Drop` or custom event logic.

### Required Normal Enemy Loot Source Setup

Normal enemy drops depend on the runtime `FLootContext.SourceTag` matching a pool in `BP_AeyerjiLootTable`. Setting an entry set or entry to 100% drop chance is not enough if the active pool is never selected.

For every enemy type that should use native death loot:

1. Enable `bSpawnNormalDeathLoot` on the enemy blueprint/class.
2. Set `DeathLootContext.SourceTag` to the exact loot source pool tag, for example `Loot.Source.Mobs`.
3. Verify `BP_AeyerjiLootTable` has a pool whose `SourceTag` matches that tag and includes the intended entry set.

If `DeathLootContext.SourceTag` is left empty, the death hook uses the enemy scaling snapshot source tag. That value comes from the matching `FEnemyScalingRow.SourceTag` when the enemy is spawned through `AAeyerjiSpawnerGroup`. If both are empty, the hook falls back to `Loot.Source.NormalEnemy`.

The pool match is hierarchical: a runtime tag such as `Loot.Source.Mobs.Grunt` can match a pool tagged `Loot.Source.Mobs`, but `Loot.Source.NormalEnemy` will not match `Loot.Source.Mobs`. The death log prints the runtime tag in `[LootReward] EnemyDeathReward ... SourceTag=...`; use that value to confirm which pool the loot service is trying to select.

`ULootSourceRuleSet::ResolveContext` now propagates the full source profile into runtime context:

| Profile field | Applied to runtime context |
| --- | --- |
| `SourceTag` | Replaces base source tag when set. |
| `PityGroup` | Replaces base pity group when set. |
| `ForcedItemDefinition` | Forces a specific item when set. |
| `BaseLegendaryChance` | Clamped to 0..1. |
| `MinimumRarity` | Applies rarity floor. |
| `PitySuccessRarity` | Defines what counts as named pity success. |
| `DifficultyScale` | Defaults invalid values back to 1. |
| `RarityWeights` | Negative weights clamp to zero. |
| `ItemLevelJitterMin/Max` | Swapped when min > max. |
| `PitySoftStartOverride`, `PitySoftSlopeOverride`, `PityHardAttemptsOverride`, `PityMaxChanceOverride` | Negative values normalize to the "use service default" sentinel; max chance clamps to 0..1. |

## StateTree Integration

### `USTT_RequestLootDropTask`

`Request Loot Drop` is a Blueprintable StateTree task that:

1. Uses the task's `FLootContext`.
2. Optionally rolls `FLootMultiDropConfig`.
3. Uses the StateTree owner as `PlayerActor` and/or pickup instigator when configured.
4. Resolves a drop transform from the owner or explicit world location.
5. Calls the existing loot service and pickup spawn helpers.

It does not store items, bypass inventory, or own pity counters.

### `USTC_LootPityCondition`

`Loot Pity Condition` is a Blueprintable StateTree condition that reads player loot stats and supports:

1. Drops since last legendary at least.
2. Computed legendary chance at least.
3. Has never picked up item definition key.
4. Named pity attempts since success at least.
5. Named pity successes at least.

It can invert the result and can use the StateTree owner as the player actor.

## Gameplay Tags

Native tags now exist for the baseline source/pity taxonomy:

| Native tag symbol | Gameplay tag |
| --- | --- |
| `Loot_Source_NormalEnemy` | `Loot.Source.NormalEnemy` |
| `Loot_Source_Elite` | `Loot.Source.Elite` |
| `Loot_Source_MiniBoss` | `Loot.Source.MiniBoss` |
| `Loot_Source_Boss` | `Loot.Source.Boss` |
| `Loot_Source_Chest` | `Loot.Source.Chest` |
| `Loot_Source_Event` | `Loot.Source.Event` |
| `Loot_Pity_GenericLegendary` | `Loot.Pity.GenericLegendary` |
| `Loot_Pity_BossUnique` | `Loot.Pity.BossUnique` |
| `Loot_Pity_SetPiece` | `Loot.Pity.SetPiece` |
| `Loot_Pity_ProgressionKey` | `Loot.Pity.ProgressionKey` |
| `Loot_Pity_RareMaterial` | `Loot.Pity.RareMaterial` |
| `Loot_Pity_FirstClear` | `Loot.Pity.FirstClear` |

Designers can add child tags in Project Settings when a specific source needs more detail, for example `Loot.Source.Boss.Map1`, while C++ keeps the broad categories stable.

## Persistence Boundaries

| Data | Where it belongs |
| --- | --- |
| Item definitions | Content/data assets. |
| Runtime rolled items | `UAeyerjiInventoryComponent`. |
| Inventory/equipment snapshots | Character profile save. |
| Lifetime loot memory | Character profile save via `FPlayerLootStats`. |
| First-clear or boss-unique permanent facts | Character or global world state. |
| Shared global facts | Shared world-state save. |
| Run-only reward decisions | Run-scoped world state or runtime director state. |
| Active run timeline | Runtime only, unless intentionally promoted to a persistent fact. |

## Logging

Current targeted prefixes:

| Prefix | Coverage |
| --- | --- |
| `[InventoryPickup]` | Pickup authority, item id, definition, pawn/controller, duplication, equip/bag result, final containment. |
| `[InventorySave]` | Snapshot keys, legacy fallback, unresolved definitions, restored counts. |
| `[ItemStatsDebug]` | Item stat/effect application skips, such as missing ASC. |
| `[LootReward]` | Native enemy death reward hook decisions, source tag, level, instigator, rarity, definition key, and spawned pickup. |

These logs are intentionally detailed enough that one failed pickup attempt should show which branch rejected the item.

## Automation Coverage

Current Aeyerji automation suite includes:

| Test | Coverage |
| --- | --- |
| `Aeyerji.Inventory.EquipFirstThenBag` | Equip success, equip rejection followed by bag placement, missing definition rejection. |
| `Aeyerji.Inventory.LegacySnapshotLoad` | Legacy snapshot with only `Definition` still loads and round-trips with `DefinitionKey`. |
| `Aeyerji.Loot.PityMemory` | Named pity memory records attempts, source tag, successes, and last dropped definition key. |
| `Aeyerji.Loot.PityChance` | Named pity hard/soft thresholds affect legendary chance. |
| `Aeyerji.Loot.SourceRuleSetProfile` | Source-rule profiles propagate source/pity/rarity/pity override fields. |
| `Aeyerji.Loot.SourceRuleSetSanitizes` | Source-rule context sanitizes invalid probabilities, weights, difficulty, jitter, and pity sentinels. |
| `Aeyerji.Save.ProfileSerialization` | Character profile roundtrip for progression, selections, inventory/equipment, loot stats, and world-state entries. |
| `Aeyerji.Save.ProfileLargePayloadSerialization` | Large profile payload serialization over the legacy 65 KB RPC risk threshold. |
| `Aeyerji.WorldState.Types` | World-state key/value copy, equality, numeric compare, and data-only behavior. |

## Ownership Model

| Owner | Owns | Does not own |
| --- | --- | --- |
| Run Director StateTree | High-level reward timing, branch decisions, first-clear checks, calls into loot tasks/conditions. | Item storage, item rolling internals, pity counter persistence. |
| `ULootService` | Loot math, rarity, table selection, forced item, multi-drop, named/generic pity. | Inventory storage, equipment validation, pickup interaction, UI. |
| `UAeyerjiInventoryComponent` | Runtime item ownership, grid/equipment state, add/equip/drop/save/load. | Drop source decisions, loot table selection, pity math. |
| Pickup actors | World representation, interaction, transfer into inventory. | Roll math, long-term item memory. |
| Save/profile | Persistent character state, inventory, selected abilities/passives, loot stats, character facts. | Run-only timeline state. |
| World state | Global/character/run facts and StateTree-readable flags. | Individual item instances. |

## Confirmed Missing Or Incomplete

| Gap | Impact | Recommended next step |
| --- | --- | --- |
| Content asset wiring for loot StateTree nodes | C++ task/condition exists but no `Content` StateTree asset was edited. | Add `Request Loot Drop` and `Loot Pity Condition` to `ST_RunDirector` or encounter StateTrees in editor. |
| Actual source-rule and loot-table data | C++ can resolve and roll, but authored project data determines useful behavior. | Configure `ULootSourceRuleSet`, `UAeyerjiLootTable`, and entry sets for each reward source. |
| Automatic normal-enemy drop trigger content enablement | The C++ hook exists and is opt-in, but enemy archetypes/classes decide whether it runs. | Enable `bSpawnNormalDeathLoot` and configure `DeathLootContext`/multi-drop settings in editor for the intended normal enemies. |
| Per-run loot memory object | `FPlayerLootStats` is profile/lifetime oriented. | Add run memory to run-scoped world state or a runtime director component if a run needs isolated pity/drop state. |
| Boss/source-specific unique memory | Pickup counts are item-keyed, not source-specific. | Use character-scoped world-state facts for first-clear/unique obtained, or add structured boss memory. |
| Designer source matrix | There is no generated list of all configured sources, tables, pity groups, and persistence facts. | Create a doc/editor utility after data assets are wired. |
| Debug UI | Logs exist, but no consolidated in-game loot debug view exists. | Add a lightweight debug widget/command after routing stabilizes. |

## Editor Work Queue

1. Use native source/pity tags as the baseline taxonomy; add child tags only where designers need specific source identity.
2. Configure source-rule assets with `SourceTag`, `PityGroup`, forced item, rarity gates, difficulty scale, item-level jitter, and pity overrides.
3. Configure the project loot table asset and verify `LootTableAsset` points at the intended table.
4. Add source-tag pools for normal enemies, elites, mini-bosses, bosses, chests, and events.
5. Add entry sets for reusable bundles such as boss rewards, rare materials, set pieces, and progression keys.
6. Wire `Request Loot Drop` and `Loot Pity Condition` into `ST_RunDirector` or relevant encounter StateTrees.
7. Define which permanent facts are character scoped vs global, for example first clear, boss unique obtained, unlocked reward tier.
8. Enable and configure the opt-in normal-enemy death reward hook on the intended enemy archetypes/classes.
9. Manually test dedicated server plus separate client: pickup, equip-first, bag fallback, failed pickup retained, checkpoint save, restart/load.
10. Audit the Blueprint that owns `W_InventoryBag_DnD` creation during death/respawn. It should reuse one inventory widget or remove the old one before creating another; the C++ controller now rebinds live inventory widgets to the new pawn, but duplicate `AddToViewport` calls will still create confusing UI/log noise.

## Do Not Duplicate

Do not create another item definition class, item instance struct, rarity enum, inventory component, equipment system, pickup actor, affix roller, save profile, gameplay tag hierarchy, loot table asset format, or independent pity memory store. The current code already owns those responsibilities.
