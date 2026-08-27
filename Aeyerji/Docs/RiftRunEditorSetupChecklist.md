# Greater Rift Editor Setup Checklist

This is the remaining Editor/content work required to make the current C++ Greater Rift implementation playable. It is ordered by dependency: complete the blocking data fixes first, then map population, boss flow, rewards, UI, and finally playtests.

The current content audit was performed against `/Game/Levels/NeonMap` and its referenced assets on 2026-07-11.

## Before the next Rift pacing test

Complete this short gate before launching another run. The later sections remain the full production checklist; this section contains only the work required to exercise the new encounter lifecycle safely.

### Required blockers

- [x] Restart the Editor after the latest C++ build so the new SpawnRegion, EncounterDirector, and enemy reveal properties are loaded.
- [x] Open `/Game/Levels/NeonMap` and assign **Rift Progression Index** to every eligible `BP_AeyerjiSpawnRegion`.
  - Use `0` for the first route area, then increase toward the boss route.
  - With ten sequential regions, `0` through `9` is the simplest first pass.
  - Regions that are lateral alternatives may share an index.
  - Regions carrying `Rift.Excluded` do not participate and do not need a route index.
  - Do not leave any eligible region at `-1`; the run will intentionally fail validation.
- [x] Open the `BP_AeyerjiEncounterDirector` class defaults or its assigned `EncounterDirectorDefinition` and verify these resolved values:
  - **Rift Region Staging Distance** = `6500`.
  - **Rift Ambient Enemy Fraction** = `0.33`.
  - **Rift Reveal Batch Size** = `3`.
  - **Rift Reveal Batch Interval** = `0.15`.
  - **Rift Reveal Duration Seconds** = `1.0`.
  - **Rift Ground Reveal Weight** = `0.75`.
  - **Rift Sky Reveal Weight** = `0.25`.
  - **Rift Prewarm Actors Per Tick** = `2`.
  - **Rift Prewarm Work Milliseconds Per Tick** = `4`.
  - **Rift Prewarm Replication Settle Seconds** = `1.0`.
  - **Rift Enemy Wake Distance** = `8000`.
  - **Rift Enemy Sleep Distance** = `10000`.
  - **Rift Maximum Awake Enemies** = `48`.
- [x] Confirm the assigned EncounterDirector definition still contains at least one valid fallback **Spawn Group**, or assign **Rift Encounter Group** explicitly on every eligible region.
- [x] Compile and save `BP_AeyerjiSpawnRegion`, `BP_AeyerjiEncounterDirector`, the LevelDirector Blueprint, and `NeonMap`.
- [x] Reimport `/Game/Systems/Rifts/RiftTierTable` from `Source/Aeyerji/Data/Rifts/GreaterRiftTiers.csv` if the six newer multiplier/level columns have not already appeared in the DataTable.

### Reveal presentation: recommended, but not a blocker

- [ ] Implement **BP On Encounter Reveal** in the enemy Blueprint if you want to judge the actual emergence presentation now.
- [ ] Handle `GroundEmergence` and `SkyDrop` cosmetically using the supplied duration.
- [ ] Leave collision, damage, AI, movement, perception, and StateTree activation alone; native code controls those.

You may test without this Blueprint event. Reinforcements will appear visibly locked in place for one second and then become active, which is sufficient to validate networking and pacing before montages or Niagara are authored.

### Only required when testing the complete run

- [ ] Finish the boss-arena respawn `PlayerStart` and `Boss Arena Respawn Player Start Tag` before testing death inside the boss phase.
- [ ] Configure the three Rift loot source pools before treating a no-loot completion as an encounter-system failure:
  - `Loot.Source.Rift.Base`
  - `Loot.Source.Rift.Timed`
  - `Loot.Source.Rift.Flawless`
- [ ] Confirm both Steam test profiles reach the applied/verified profile state before beginning the two-client acceptance test.

### First retest scope

For the first run, stop after validating these points:

1. Loading remains visible until `[RiftRun][Prewarm] Complete`.
2. The log reports the intended full inactive pool and no validation failure for a missing progression index.
3. Approaching a region at roughly `6500 cm` produces the ambient attractor pack.
4. Reaching roughly `2500 cm`, damaging an attractor, or entering combat reveals the remaining enemies in batches.
5. No `[RiftPool] Emergency runtime construction` warning occurs.
6. Moving forward does not open a new pack behind the highest progression index.
7. Backtracking wakes existing enemies instead of creating replacements.

If those seven checks pass, proceed to the complete solo and two-client smoke-test gates near the end of this document.

## Already verified

- [x] C++ now reads all Greater Rift tiers from one `UDataTable` using `FAeyerjiRiftTierRow`.
- [x] `DefaultGame.ini` points **Default Rift Tier Table** at `/Game/Systems/Rifts/RiftTierTable`.
- [x] **Rift Enemy Reference Level** is now legacy fallback-only and set to `1`; normal runs use their frozen Activity Level.
- [x] The source-controlled import file is `Source/Aeyerji/Data/Rifts/GreaterRiftTiers.csv`.
- [x] Per-tier DataAsset classes, the Rift scaling DataAsset class, and the Rift population-profile dependency have been removed from C++.
- [x] `BP_AeyerjiLevelDirector` and `BP_AeyerjiEncounterDirector` are present and the LevelDirector references `NeonMapZoneRun`.
- [x] C++ now freezes and prewarms the exact full Rift population during `TransitionLoading`; gameplay is not released until prewarm completes.
- [x] C++ owns the `Planned -> Staged -> Revealing -> Active -> Retired` region lifecycle, monotonic progression frontier, finite skipped-budget transfer, reveal lock, and region-aware AI sleeping.
- [x] `BossDefMap1` references `BP_AeyerjiLinkedTeleporter_Boss`.
- [x] `BossTargetPointA`, `BossTargetPointB`, `BossSpawnPoint`, and `EndRunPortalSpawn` are present in `NeonMap` with matching Actor Tags.
- [x] `NeonMapZoneRun` has a boss definition, extraction portal class, and extraction spawn-point tag.
- [x] `BP_AeyerjiWorldDirector` is present in `L_PersistentRoot` and currently starts `Zone.Menu` through server world flow.

## Important: fields that no longer exist

Do not look for or recreate these old Greater Rift properties:

- **Enemy Level Baseline** on a Rift Tier.
- **World Tier** on a Rift Tier or Rift run state.
- Per-tier `RiftTier1`, `RiftTier2`, and `RiftTier3` DataAssets.
- The `RiftScalingSettings` DataAsset.
- A Rift tier **Population Profile** reference.
- A duplicated **Tier Number** field. The DataTable row name is the tier identity.
- The old Rift enemy-level compatibility getter.
- WorldDirector StateTree or level-proof fields. `AAeyerjiWorldDirector` is now only the persistent streaming bootstrap.

The selected Rift Tier is a difficulty rank, not an enemy level. C++ freezes an `Activity` snapshot at run start: Standard Rift uses the highest launch-party Character Level; Excursion uses that same level capped by the row's **Max Activity Level**. `MonsterPower` then supplies the tier's independent combat and reward modifiers. The main-menu **Campaign** choice launches Standard Rift; **Excursion** launches the tier-capped mode. `Rift Activity Type` on a Zone Run Definition is only the direct-editor fallback.

## 1. Create the single Rift Tier DataTable

Restart the Editor after compiling the C++ change so `FAeyerjiRiftTierRow` is available, then:

- [x] Import `Source/Aeyerji/Data/Rifts/GreaterRiftTiers.csv` as a DataTable using row struct **Aeyerji Rift Tier Row** (`FAeyerjiRiftTierRow`).
- [x] Save it exactly as `/Game/Systems/Rifts/RiftTierTable`.
- [x] Confirm the imported row names are `Tier_1`, `Tier_2`, and `Tier_3`.
- [x] Confirm all three rows have **Time Limit Seconds = 900**, **Progress Target Points = 100**, **Enemy Budget = 120**, and **Region Activation Distance = 2500 cm**.
- [ ] Reimport the updated CSV and confirm **Minimum Character Level**, **Max Activity Level**, **Density Multiplier**, **Elite Rate Multiplier**, **Encounter Size Multiplier**, and **Progress Multiplier** appear on all rows.
- [x] Keep **Fixed Run Seed = 0** for normal play. Set a non-zero value only to reproduce a deterministic test run.
- [x] Delete the obsolete `/Game/Systems/Rifts/RiftTier1`, `RiftTier2`, `RiftTier3`, and `RiftScalingSettings` assets in the Content Browser.
- [x] Delete the obsolete `/Game/Levels/Director/RiftPopulationProfile` asset if it is still present locally.
- [x] Open and resave `/Game/Systems/NeonMapZoneRun` so Unreal strips the removed per-tier asset array from its serialized data.
- [x] Compile/resave any Blueprint that still has broken pins for the removed Rift `EnemyLevelBaseline` or Rift `WorldTier` fields.

Do not add a `TierNumber` column. The server resolves tier 2 by row name `Tier_2`; this prevents the row identity and an editable integer from disagreeing.

## 2. Review tier balance in the table

Open `/Game/Systems/Rifts/RiftTierTable`. Each tier is one row, so timing, population budget, monster power, and reward counts can be reviewed together and changed through the CSV without locking separate binary assets.

- [x] Leave **Rift Enemy Reference Level** at `1`; it is used only by invalid/legacy contexts that do not have a frozen run snapshot.
- [ ] Verify Tier 1 is **Minimum Character Level = 1**, **Max Activity Level = 20**; Tier 2 is `10`/`35`; Tier 3 is `25`/`50`.
- [x] Review or accept each row's health multiplier.
- [x] Review or accept each row's damage multiplier.
- [x] Review or accept each row's defense multiplier.
- [x] Review or accept each row's reward-quality multiplier.

The seeded C++ starting values are:

| Rift Tier | Health | Damage | Defense | Reward quality |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1.00 | 1.00 | 1.00 | 1.00 |
| 2 | 1.35 | 1.12 | 1.08 | 1.05 |
| 3 | 1.80 | 1.25 | 1.16 | 1.10 |

These are starting values, not final balance. Do not add per-tier enemy levels: edit **Max Activity Level** instead. Density, elite-rate, encounter-size, progress, health, damage, defense, and reward quality are all independent tier controls applied after the base enemy level is resolved. Movement speed, attack speed, ranges, perception, and resource behavior are intentionally not multiplied by monster power.

## 3. Review the direct encounter budget

The Rift planner no longer consumes a generic world-population asset. It uses the selected row's **Enemy Budget** and **Fixed Run Seed** directly.

- [x] The CSV starts each tier at **Enemy Budget = 120** for the all-one-point smoke test.
- [x] The CSV starts each tier at **Fixed Run Seed = 0** for server-generated production seeds.
- [ ] After weighted enemies are authored, tune **Enemy Budget** per tier if the reserved weighted progress is no longer in the intended range.

The old world-population profile's cluster count, cluster radius, density curve, and total budget do not define Greater Rifts. GR reuses the placed `AAeyerjiSpawnRegion` bounds and their existing regional weight/elite controls, but owns its budget and activation distance in the tier row.

The final reserved weighted progress must be at least 120% of the target. For a target of `100`:

- Below `120` is a run-start failure.
- `120-130` is the preferred reserve.
- Above `130` runs, but logs a tuning warning.

If you assign elites or tougher roles more than one point, reduce the enemy budget or composition counts accordingly after the basic all-one-point smoke test works.

## 4. Switch `NeonMapZoneRun` to proximity regions

`NeonMapZoneRun` is currently configured as **Fixed World Population**.

- [x] Set **Spawn Mode** to **Proximity Encounter Regions**.
- [x] Keep **Run Win Condition** as **Kill Target Then Boss**.
- [x] Do not create or populate a per-region tag array; that property no longer exists.
- [x] Keep the existing world-population `AeyerjiSpawnerGroup` configured as the shared spawning/pooling executor if `World Population Spawner Actor Tag` already resolves it. C++ creates a runtime executor if it is absent.
- [x] Ensure `Encounter Director Definition` resolves the intended `Spawn Groups`; these groups supply GR enemy composition.

The LevelDirector no longer resolves individual encounters by tag. The server automatically discovers every valid `AAeyerjiSpawnRegion`, orders it by **Rift Progression Index** and then stable actor path, and uses the region's bounds for distance and spawn placement. A missing progression index now rejects the run before gameplay.

## 5. Review the existing SpawnRegions and encounter composition

Do not add tags to normal regions. For each existing `BP_AeyerjiSpawnRegion`:

- [ ] Assign **Rift Progression Index** on every ordinary Rift anchor in `NeonMap`.
  - Start at `0` near the party entry and increase toward the boss route.
  - Equal indices are allowed for lateral/parallel anchors.
  - Do not leave any eligible region at `-1`; launch validation rejects missing indices.
- [x] Confirm **Region Bounds** cover navigable ground and do not overlap inaccessible nav islands.
- [ ] Set **Rift Encounter Group** on each ordinary anchor when it needs authored composition. Empty uses the Encounter Director Definition's fallback pool.
- [x] Keep **Region Weight** positive. C++ divides the tier's enemy budget across regions using these weights.
- [x] Review **Elite Chance Bonus** and **Allow Elites**; they modify the selected spawn group's GR elite chance.
- [x] Add the Actor Tag `Rift.Excluded` only to regions that must never receive a GR encounter, such as the boss arena or an unsafe presentation-only area.
- [x] Leave all ordinary regions untagged.
- [x] Do not enable collision on `Region Bounds`. Authority measures player distance to the box directly every EncounterDirector update.
- [x] Confirm the selected regions have a nav path back to the playable route.

Only regions at or ahead of the monotonic party frontier may stage. The pressure evaluator selects the next forward authored index, not an arbitrary nearest rear anchor. If the party skips unopened lower-index regions, their exact reserved enemies are transferred to still-unopened forward regions; already activated enemies left behind remain alive and progress-bearing.

In `BP_AeyerjiEncounterDirector` or its definition, review:

- **Rift Region Staging Distance**: `6500 cm`.
- Tier row **Region Activation Distance**: `2500 cm`, used for reinforcement reveal.
- **Rift Ambient Enemy Fraction**: `0.33` (normally 4 of a 12-enemy region).
- **Rift Reveal Batch Size**: `3`.
- **Rift Reveal Batch Interval**: `0.15 s`.
- **Rift Reveal Duration Seconds**: `1.0 s`.
- **Rift Ground Reveal Weight / Rift Sky Reveal Weight**: `0.75 / 0.25`.
- **Rift Prewarm Replication Settle Seconds**: `1.0 s`; keep actors relevant and awake-but-hidden long enough for connected clients to receive initial actor channels.
- **Rift Enemy Wake Distance / Sleep Distance**: `8000 / 10000 cm`.
- **Rift Maximum Awake Enemies**: `48`.
- **Rift Minimum Spawn Distance From Players**: `1200 cm`.
- **Rift Pressure Evaluation Interval**: `2 s`.
- **Rift Minimum Active Enemy Pressure**: `8`.

### Enemy reveal presentation

- [ ] In every Rift enemy Blueprint that needs an entrance, implement **BP On Encounter Reveal**.
- [ ] Switch on `RevealStyle`:
  - `GroundEmergence`: play the authored climb/emergence montage, Niagara, and ground effect.
  - `SkyDrop`: play the authored fall/drop presentation and impact effect.
  - `Immediate`: requires no entrance animation.
- [ ] Fit presentation to the supplied `RevealDurationSeconds`.
- [ ] Do not enable collision, damage, targeting, movement, perception, or StateTree logic in Blueprint. C++ owns the one-second lock and unlocks even when no Blueprint animation is implemented.

### Weighted progress and composition authoring

Open every `UEnemySpawnGroupDefinition` referenced by the zone's `EncounterDirectorDefinition` and configure:

- [x] At least one valid **Enemy Type**.
- [x] Optional **Elite Enemy Types**.
- [x] **Rift Elite Chance**; `0` disables elite-pool selection for this group.
- [x] **Rift Progress Points**; use `1` for ordinary enemies or `2` for tougher ordinary roles.
- [x] **Rift Elite Progress Points**; start at `5` for planned elites.

The deterministic run plan freezes the exact class, elite status, and progress value before anything spawns. If the reserved weighted total is below 120% of the target, readiness fails. Above 130% produces a tuning warning.

Do not hardcode progress by enemy class in Blueprint. The selected spawn-group data is the authoritative input and C++ registers the frozen points on each spawned enemy.

## 6. Finish the boss arena and respawn contract

The linked teleporter class and both endpoint markers are already configured. C++ changes the spawned teleporter to A-to-B only when the boss phase starts.

- [x] `BossDefMap1.BossLinkedTeleporterClass` points to `BP_AeyerjiLinkedTeleporter_Boss`.
- [x] Endpoint A uses Actor Tag `BossTargetPointA` and that marker exists.
- [x] Endpoint B uses Actor Tag `BossTargetPointB` and that marker exists.
- [ ] Verify endpoint A is outside the arena and endpoint B is safely inside it.
- [ ] Build/verify one-way arena collision so players cannot walk, dash, or be knocked back out of the boss arena.
- [ ] Add a new `PlayerStart` inside the boss arena.
- [ ] Give it a dedicated **Player Start Tag**, for example `Rift.BossArena.Respawn`.
- [ ] Set `BossDefMap1.Boss Arena Respawn Player Start Tag` to that exact value. It is currently unset.
- [ ] Keep the four existing `Zone.Neon.Entry` starts for normal zone entry; do not reuse that tag for boss respawn.
- [ ] Verify the boss Blueprint/spawner does not heal, destroy, or recreate Legion when a player dies. Only the player should respawn.
- [ ] Verify a second co-op player can still use endpoint A after the first player has entered.

## 7. Configure the reward presentation and loot pools

The imported table rows already provide the MVP reward counts, rarity floors, and bonus-cache class:

| Layer | Source Tag (owned by C++) | Imported rolls | Presentation |
| --- | --- | ---: | --- |
| Base | `Loot.Source.Rift.Base` | `4 +/- 1` | Immediate private pickups |
| Timed | `Loot.Source.Rift.Timed` | `3` | Shared visual cache, private payload |
| Flawless | `Loot.Source.Rift.Flawless` | `1` | Same shared cache, private payload |

- [ ] Review the flat Base/Timed/Flawless drop-count, variance, and minimum-rarity columns in `RiftTierTable`.
- [ ] Confirm **Bonus Reward Presentation Class** resolves to `/Game/Loot/BP_RewardPresentationActor` for every row.
- [ ] In `BP_RewardPresentationActor`, use **Interact To Release** for the bonus cache.
- [ ] If Blueprint overrides **Handle Release Requested**, play the opening presentation and then call `ReleaseStoredLoot`; an override that never calls release will trap the player's bundle.
- [ ] Verify its interaction sphere, interaction radius, mesh, collision, ground snap, and replicated opened/released presentation.

`/Game/Loot/BP_AeyerjiLootTable` currently contains pools for sources such as Boss, Elite, Mobs, and survival rounds, but no Rift source pools were found.

- [ ] Add or map valid loot pools for `Loot.Source.Rift.Base`.
- [ ] Add or map valid loot pools for `Loot.Source.Rift.Timed`.
- [ ] Add or map valid loot pools for `Loot.Source.Rift.Flawless`.
- [ ] Confirm every referenced item definition is valid at the test player's level.
- [ ] Verify the three source tags produce intentionally different loot rather than falling back to the table's first pool.

Blueprint must not reroll rewards or decide eligibility. C++ has already rolled each private ledger and decided base/timed/flawless eligibility before presenting it.

## 8. Wire the Mission HUD, tier selector, and results screen

### Mission HUD

Use `/Game/GUI/HUD/WBP_MissionHUD` and its existing `UW_AeyerjiMissionHUD` contract.

- [ ] In **Handle Objective State Applied**, display `ProgressPoints / ProgressPointTarget` and `Progress01` as weighted progress, not literal kill count.
- [ ] Bind to `AAeyerjiGameState.OnRiftRunStateChanged` for phase/overtime/flawless presentation changes.
- [ ] Display the selected tier from `GetRiftRunState().SelectedRiftTier`.
- [ ] Display `MonsterPower` only in debug UI if useful; it is not a player-facing enemy level.
- [ ] Drive the visible timer from `GetAuthoritativeRunRemainingSeconds()` or synchronized server time. Do not start an independent client timer.
- [ ] At zero, switch to **Overtime** presentation without showing run failure.
- [ ] When `bBossPhaseStarted` becomes true, switch the objective presentation from progress collection to Legion.
- [ ] When `bBossPhaseDeathOccurred` becomes true, show that flawless eligibility was lost.

### Pre-run Rift Tier selector

- [ ] Show each loaded player's `HighestUnlockedRiftTier` and the party's common selectable cap.
- [ ] Allow only the elected leader to interact; non-leaders see read-only state.
- [ ] Call `AAeyerjiPlayerState.RequestSelectRiftTier(RequestedTier)` from the leader's selection control.
- [ ] Refresh from `OnRiftTierProgressionChanged` rather than assuming the request succeeded.
- [ ] Handle `OnRiftTierSelectionRejected` and map every rejection enum to localized text.

### Results screen

Use `/Game/GUI/WBP_EndRunScreen` as the presentation surface.

- [ ] Bind the local PlayerState's `OnPersonalRunResultsChanged` event.
- [ ] Use `GetPersonalRunResults()` for personal reward counts and unlock status; do not use the shared GameState result for private values.
- [ ] Present selected tier, weighted progress, elapsed time, on-time/overtime, boss-phase death, flawless reward, base/timed/flawless roll counts, earned next tier, highest unlocked tier, and whether this profile unlocked a new tier.
- [ ] Wire **Retry Same Tier** to `RequestRetryRun()`.
- [ ] Wire **Retry Earned Tier** to `RequestRetryEarnedRiftTier()` and show it only when a new tier was earned.
- [ ] Wire **Return to Menu** to `RequestReturnToMenu()`.
- [ ] Keep retry/tier-changing controls read-only for non-leaders.

## 9. Add localized Rift text

Add stable keys to `Source/Aeyerji/Data/Strings/GlobalStringTable.csv`, then reimport `/Game/Localization/GlobalStringTable` in the Editor.

- [ ] Rift Tier selector title and tier labels.
- [ ] Weighted progress and time-remaining formats.
- [ ] Boss phase, overtime, flawless-eligible, and flawless-lost labels.
- [ ] Base, timed, and flawless reward labels.
- [ ] Retry same tier, retry earned tier, and return-to-menu labels.
- [ ] One message for every `EAeyerjiRiftTierSelectionFailure` value.
- [ ] Results labels for selected tier, completion time, earned tier, and personal highest tier.

Do not place player-facing fallback English directly in Blueprint graphs when it belongs in the string table.

## 10. Prepare two persistent test identities

- [ ] Create/reset a Tier 1 identity with a unique save slot and stable PlayerId.
- [ ] Create/reset a second identity and unlock it through Tier 3 for common-cap tests.
- [ ] Record XP, level, equipment, inventory, last selected tier, highest unlocked tier, and completed-run count before testing.
- [ ] Confirm both profiles reach `Applied` profile-load state before the run-start UI enables.

## First functional smoke-test gate

Run this in order after the Editor setup above is saved and all affected Blueprints compile.

### Solo

- [ ] Enter `NeonMap` through normal world flow without console intervention.
- [ ] Confirm the loading overlay remains until `[RiftRun][Prewarm] Complete` reports the full inactive planned population.
- [ ] Confirm gameplay starts with `FreshSpawnsDuringGameplay=0`; any `[RiftPool] Emergency runtime construction` line fails the normal-run pool acceptance gate.
- [ ] Confirm the run logs `[RiftRun][Activity]` with the frozen Activity Level and selected Excursion Tier (zero for Standard).
- [ ] Confirm `[RiftRun][EncounterPlan]` reports every anchor's authored progression index, finite effective budget, and at least 120 reserved progress for a target of 100.
- [ ] Confirm a level-1 Standard Rift produces level-1 enemies and a level-10 Tier-1 Excursion produces level-10 enemies.
- [ ] Confirm a level-50 player in Tier 1 produces level-20 enemies, and gaining a level mid-run does not change later enemy levels.
- [ ] At roughly `6500 cm`, confirm the region presents its combat-ready attractor pack without constructing actors during gameplay.
- [ ] At roughly `2500 cm`, or after damaging/engaging an attractor, confirm the remaining enemies reveal three at a time every `0.15 s`.
- [ ] Confirm revealing enemies remain noninteractive for the supplied reveal duration even when the Blueprint has no montage.
- [ ] Confirm an ordinary group never appears within **Rift Minimum Spawn Distance From Players** and unsafe anchors log a deferred/rejected activation.
- [ ] Approach multiple anchors and verify proximity and timer activation cannot reserve the same anchor twice.
- [ ] Move forward past an unopened lower index and verify its finite budget transfers forward without a rear pack appearing.
- [ ] Backtrack and verify existing left-behind enemies wake; no new rear region stages.
- [ ] Confirm at most 48 ordinary Rift enemies are awake while distant enemies pause AI/perception/movement and become dormant.
- [ ] Reach 100%, verify unused regions stop activating and already-accepted queues/enemies remain alive.
- [ ] Enter the boss arena, die once, respawn at the arena PlayerStart, and verify Legion retains its current health.
- [ ] Kill Legion and verify extraction appears only after results and rewards finalize.
- [ ] Collect private base pickups and claim the bonus cache once.
- [ ] Retry, then return to menu and confirm the profile persists.

### Two clients

- [ ] Verify all planned pooled actors are constructed on both clients before the loading overlay clears.
- [ ] Verify either client can activate a nearby region and simultaneous proximity checks consume it once.
- [ ] Verify the same four-attractor/reinforcement sequence is observed on both Steam clients.
- [ ] Verify a returning client sees the same sleeping enemies wake rather than replacement actors spawning.
- [ ] Verify only the leader can select/retry tiers.
- [ ] Verify the common tier cap uses the lower participant unlock.
- [ ] Verify both players can enter through endpoint A independently.
- [ ] Verify either boss-phase death removes flawless for the shared run.
- [ ] Verify each player sees and claims only their own cache payload and owned pickups.
- [ ] Verify simultaneous cache interactions do not duplicate or cross-award loot.

Keep Output Log filters visible for `[RiftRun]`, `LogAeyerjiWorldFlow`, `LogEncounterDirector`, `[ProfileLoad]`, `[ProfileCheckpoint]`, `Warning`, and `Error`. Record the run serial, selected tier, seed, player identity, and server/client role for every defect.

## Ready for balance and presentation work

Only begin the larger combat-feedback, ability-tuning, signature-item-effect, animation, VFX, audio, and pacing passes after the solo and two-client functional gates above pass without duplicate actions, cross-player loot, save loss, blocked retry, or state disagreement.
