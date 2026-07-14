# Greater Rift Editor Setup Checklist

This is the remaining Editor/content work required to make the current C++ Greater Rift implementation playable. It is ordered by dependency: complete the blocking data fixes first, then map population, boss flow, rewards, UI, and finally playtests.

The current content audit was performed against `/Game/Levels/NeonMap` and its referenced assets on 2026-07-11.

## Already verified

- [x] C++ now reads all Greater Rift tiers from one `UDataTable` using `FAeyerjiRiftTierRow`.
- [x] `DefaultGame.ini` points **Default Rift Tier Table** at `/Game/Systems/Rifts/RiftTierTable`.
- [x] **Rift Enemy Reference Level** is now legacy fallback-only and set to `1`; normal runs use their frozen Activity Level.
- [x] The source-controlled import file is `Source/Aeyerji/Data/Rifts/GreaterRiftTiers.csv`.
- [x] Per-tier DataAsset classes, the Rift scaling DataAsset class, and the Rift population-profile dependency have been removed from C++.
- [x] `BP_AeyerjiLevelDirector` and `BP_AeyerjiEncounterDirector` are present and the LevelDirector references `NeonMapZoneRun`.
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

The LevelDirector no longer resolves individual encounters by tag. The server automatically discovers every valid `AAeyerjiSpawnRegion`, orders it by stable actor path for deterministic planning, and uses the region's bounds for distance and spawn placement.

## 5. Review the existing SpawnRegions and encounter composition

Do not add tags to normal regions. For each existing `BP_AeyerjiSpawnRegion`:

- [x] Confirm **Region Bounds** cover navigable ground and do not overlap inaccessible nav islands.
- [ ] Set **Rift Encounter Group** on each ordinary anchor when it needs authored composition. Empty uses the Encounter Director Definition's fallback pool.
- [x] Keep **Region Weight** positive. C++ divides the tier's enemy budget across regions using these weights.
- [x] Review **Elite Chance Bonus** and **Allow Elites**; they modify the selected spawn group's GR elite chance.
- [x] Add the Actor Tag `Rift.Excluded` only to regions that must never receive a GR encounter, such as the boss arena or an unsafe presentation-only area.
- [x] Leave all ordinary regions untagged.
- [x] Do not enable collision on `Region Bounds`. Authority measures player distance to the box directly every EncounterDirector update.
- [x] Confirm the selected regions have a nav path back to the playable route.

The nearest viable unopened anchor to a living participant is reserved atomically. A periodic server pressure check can also reserve one when live enemy pressure is low. Once reserved, an anchor cannot activate again through either path. Spawn attempts require NavMesh, the configured safety distance from every living player, and preferably an occluded location; unsafe anchors defer rather than spawning beside players.

In `BP_AeyerjiEncounterDirector` or its definition, review **Rift Minimum Spawn Distance From Players** (default `1200 cm`), **Rift Pressure Evaluation Interval** (`2 s`), **Rift Minimum Active Enemy Pressure** (`8`), and **Rift Prefer Hidden Spawn Locations**.

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
- [ ] Confirm the run logs `[RiftRun][Activity]` with the frozen Activity Level and selected Excursion Tier (zero for Standard).
- [ ] Confirm `[RiftRun][EncounterPlan] Ready` reports the intended anchor count, finite effective budget, and at least 120 reserved progress for a target of 100.
- [ ] Confirm a level-1 Standard Rift produces level-1 enemies and a level-10 Tier-1 Excursion produces level-10 enemies.
- [ ] Confirm a level-50 player in Tier 1 produces level-20 enemies, and gaining a level mid-run does not change later enemy levels.
- [ ] Confirm no Rift enemy spawns before a living player comes within the tier's activation distance of a region.
- [ ] Confirm an ordinary group never appears within **Rift Minimum Spawn Distance From Players** and unsafe anchors log a deferred/rejected activation.
- [ ] Approach multiple anchors and verify proximity and timer activation cannot reserve the same anchor twice.
- [ ] Reach 100%, verify unused regions stop activating and already-accepted queues/enemies remain alive and pursuing.
- [ ] Enter the boss arena, die once, respawn at the arena PlayerStart, and verify Legion retains its current health.
- [ ] Kill Legion and verify extraction appears only after results and rewards finalize.
- [ ] Collect private base pickups and claim the bonus cache once.
- [ ] Retry, then return to menu and confirm the profile persists.

### Two clients

- [ ] Verify either client can activate a nearby region and simultaneous proximity checks consume it once.
- [ ] Verify only the leader can select/retry tiers.
- [ ] Verify the common tier cap uses the lower participant unlock.
- [ ] Verify both players can enter through endpoint A independently.
- [ ] Verify either boss-phase death removes flawless for the shared run.
- [ ] Verify each player sees and claims only their own cache payload and owned pickups.
- [ ] Verify simultaneous cache interactions do not duplicate or cross-award loot.

Keep Output Log filters visible for `[RiftRun]`, `LogAeyerjiWorldFlow`, `LogEncounterDirector`, `[ProfileLoad]`, `[ProfileCheckpoint]`, `Warning`, and `Error`. Record the run serial, selected tier, seed, player identity, and server/client role for every defect.

## Ready for balance and presentation work

Only begin the larger combat-feedback, ability-tuning, signature-item-effect, animation, VFX, audio, and pacing passes after the solo and two-client functional gates above pass without duplicate actions, cross-player loot, save loss, blocked retry, or state disagreement.
