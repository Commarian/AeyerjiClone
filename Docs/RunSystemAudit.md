# Aeyerji Run, World Flow, StateTree, and Persistence System Audit

This document describes the run system and StateTree-facing world-flow work currently implemented in C++. It covers the server-owned run state machine, streaming transitions, world-state registry, `ST_RunDirector` bridge, extraction flow, save boundaries, and remaining editor work.

## Current Status

The run system now has a C++ spine for:

1. Server-authoritative run state transitions through `AAeyerjiGameState`.
2. Zone streaming and gameplay-map flow through `UAeyerjiStreamingSubsystem`.
3. Persistent/global/character/run facts through `UAeyerjiWorldStateSubsystem`.
4. A persistent-map bootstrap actor, `AAeyerjiWorldDirector`, that owns a server-only `RunDirectorStateTree` component and defaults to `/Game/StateTrees/ST_RunDirector.ST_RunDirector`.
5. Level-run encounter orchestration through `AAeyerjiLevelDirector`.
6. Extraction-complete victory flow through `AAeyerjiEndRunPortal`.
7. Character/profile checkpoint saves through player state/controller/pawn paths and run completion persistence.

The current `AAeyerjiWorldDirector` StateTree path is event-driven. `AAeyerjiGameState` now mirrors real run lifecycle changes into `UAeyerjiWorldStateSubsystem` as `Run.*` facts, and the world director wakes `ST_RunDirector` from those facts on authority. The old `Run.Level = 60` branch remains as a configurable proof mode on `AAeyerjiWorldDirector` so existing test content can keep working while production branches are authored.

The profile load path is now chunked for large payloads, and checkpoint/profile logs are explicit. This matters because inventory, selected abilities/passives, character facts, loot memory, and run results can push profile payloads past the old single-RPC array risk zone.

## System Map

| Area | Primary code | Current behavior |
| --- | --- | --- |
| Networked run state | `AAeyerjiGameState` | Replicated authoritative state machine for `PreRun`, `InRun`, `BossDefeated`, `ObjectiveComplete`, `RunComplete`, and `ReturnToMenu`. |
| World-flow phase | `AAeyerjiGameState` | Replicated menu/loading/gameplay flow with transition id, active zone id, pending loading blockers, and player ready acknowledgements. |
| Streaming/session state | `UAeyerjiStreamingSubsystem`, `UAeyerjiStreamingManifest`, `UAeyerjiStreamingSaveGame` | Zone enter, sublevel load/unload tracking, gameplay map selection, retry/return-to-menu travel, campaign cursor, teleporter unlocks, and quest flags. |
| Persistent world facts | `UAeyerjiWorldStateSubsystem`, `UAeyerjiWorldStateSaveGame` | Server-authoritative key/value registry with global/run/character/session scopes and optional public replication. |
| Run StateTree bootstrap | `AAeyerjiWorldDirector` | Persistent-map actor with server-owned `UStateTreeComponent`, default `ST_RunDirector` asset reference, startup zone flow, event-driven StateTree evaluation, configurable watched tags, and optional level-proof seed/gate. |
| Level run orchestration | `AAeyerjiLevelDirector` | Encounter sequence/fixed population mode, shard tracking, boss gate, checkpoint, timer, difficulty snapshot, enemy level resync, boss teleporter, and extraction portal setup. |
| Encounter population | `AAeyerjiEncounterDirector`, `AAeyerjiSpawnerGroup` | Existing spawn/clear/progress events consumed by `AAeyerjiLevelDirector` and `AAeyerjiGameState`. |
| Extraction portal | `AAeyerjiEndRunPortal` | Replicated portal actor that starts a server countdown, notifies the owning client, and calls `Server_CompleteExtraction` on success. |
| StateTree world-state bridge | `USTT_SetWorldStateTask`, `USTC_WorldStateCondition` | StateTree task/condition for writing, incrementing, clearing, and querying central world-state facts. GameState publishes run lifecycle facts for these nodes. |
| StateTree loot bridge | `USTT_RequestLootDropTask`, `USTC_LootPityCondition` | StateTree task/condition for reward drops and loot-memory checks. |
| Save/checkpoint path | `AAeyerjiPlayerState`, `APlayerParentNative`, `CharacterStatsLibrary`, `UAeyerjiSaveManagerSubsystem` | Explicit checkpoint commits, profile transport/cache, streaming saves, and shared world-state saves. |
| Debug/inspection helpers | `AAeyerjiGameState`, `UAeyerjiWorldStateSubsystem`, `AAeyerjiWorldDirector` | Blueprint-callable summaries for run lifecycle facts, active run id, world-state counts, run fact strings, and last RunDirector evaluation reason/tag. |

## Run State Machine

`AAeyerjiGameState` is the replicated run-state authority. Valid high-level flow:

1. `PreRun`
2. `InRun`
3. `BossDefeated` when boss clear/death is accepted.
4. `ObjectiveComplete` after boss delay or kill-target completion.
5. `RunComplete` after extraction, time expiry, forced completion, or abandonment.
6. `ReturnToMenu` for menu transition/travel cleanup.

Important server calls:

| Call | Purpose |
| --- | --- |
| `Server_StartRun` | Transitions to `InRun`, starts a world-state run id, invokes `LevelDirector::StartRun`, and refreshes objective state. |
| `Server_NotifyBossDefeated` | Transitions to `BossDefeated`, stops level run, broadcasts boss objective event, and schedules objective-complete delay. |
| `Server_BeginObjectiveComplete` | Transitions to `ObjectiveComplete`, snapshots victory results, stops level run, and spawns extraction portal. |
| `Server_CompleteExtraction` | Finalizes victory as `RunComplete`, persists results/profile state, and freezes/cleans up the completed run world. |
| `Server_FailRunTimeExpired` | Finalizes time-expired failure and persists results/profile state. |
| `Server_MarkRunComplete` | Compatibility/manual end path, recorded as abandoned when appropriate. |
| `Server_ReturnToMenu` | Moves to `ReturnToMenu` and triggers streamed or hard menu return. |
| `Server_RetryRun` | Restarts the current gameplay mission, with PIE staging when needed. |

Run result snapshots use `FAeyerjiRunResults` and include run time, shards, kills, target, difficulty, zone id, resolution, speed bonus, and best time for difficulty.

## Objective Flow

`AAeyerjiLevelDirector` controls the level-specific objective model. Supported modes:

| Mode | Behavior |
| --- | --- |
| `BossCleared` | Boss clear/death can complete the main objective. |
| `KillTarget` | Encounter kill count reaching target can complete the main objective. |
| `KillTargetThenBoss` | Kill target marks the primary objective complete, opens boss progression, then boss clear completes the main objective. |

The GameState listens to level/encounter events and republishes objective snapshots to clients through `FAeyerjiObjectiveState`.

## Level Director Responsibilities

`AAeyerjiLevelDirector` owns local run mechanics for a gameplay zone:

1. Starts and ends the active run.
2. Tracks shard count and opens the boss gate when requirements are met.
3. Supports sequence mode and fixed world population mode.
4. Tracks checkpoint and respawns the player at checkpoint.
5. Runs an optional run timer and reports time expiry.
6. Captures world tier/difficulty from `AAeyerjiGameInstance`.
7. Resyncs enemy levels from the global difficulty/level curve on run start or player level-up when enabled.
8. Starts boss spawner or native boss spawn flow.
9. Spawns a linked boss teleporter when configured.
10. Spawns or configures extraction portal data through GameState.

The native boss spawn path is optional and disabled by default; Blueprint can still own boss presentation/spawn flow.

## World Flow And Streaming

`UAeyerjiStreamingSubsystem` owns persistent session/streaming state:

1. Loads manifest and persistent streaming save on initialize.
2. Enters zones and computes load/unload deltas.
3. Tracks loaded, desired, pending load, and pending unload sublevels.
4. Emits request, state change, zone-ready, and gameplay-map-selected events.
5. Selects random or campaign gameplay maps.
6. Supports retry/restart and travel to main menu.
7. Persists current zone, current gameplay map id, campaign mode/cursor, unlocked teleporters, and quest flags.

`AAeyerjiGameState::Server_BeginWorldTransition` coordinates the networked side:

1. Sets active zone and increments transition id.
2. Sets world flow phase to `TransitionLoading`.
3. Asks streaming subsystem to enter the zone.
4. Waits for server zone-ready.
5. Waits for player ready acknowledgements.
6. Waits for extra loading blockers, such as fixed population initial spawn.
7. Applies spawn policy.
8. Resolves gameplay actors and broadcasts gameplay-ready.
9. Enters `Gameplay` phase and starts/prepares run state as appropriate.

Clients respond to replicated world-flow state by entering the same zone locally and reporting readiness back to the server.

## World State Registry

`UAeyerjiWorldStateSubsystem` is a server-authoritative registry for facts and registered objects.

### Key Model

`FAeyerjiWorldStateKey` is:

| Field | Meaning |
| --- | --- |
| `StateTag` | Hierarchical gameplay tag naming the fact or object. |
| `InstanceId` | Optional unique instance id for repeated placed objects or source instances. |
| `OwnerId` | Optional owner/profile id for character-scoped state. |

### Value Types

`FAeyerjiWorldStateValue` supports bool, int, float, name, string, gameplay tag, soft object path, and transient live object pointer. Persistent/replicated copies drop transient object pointers through data-only copies.

### Scopes

| Scope | Intended lifetime |
| --- | --- |
| `Global` | Shared world/server facts saved in the shared world-state artifact when persistent. |
| `Run` | Active-run facts cleared by `BeginRun`, `EndRun`, or `ClearRunState`. |
| `Character` | Facts saved inside a specific character/profile using `OwnerId`. |
| `Session` | Process/session facts that are not one run and not persistent profile state. |

### Persistence And Replication

Only persistent entries that belong in the shared world save are written to `UAeyerjiWorldStateSaveGame`. Character-scoped persistent entries are exported into `UAeyerjiSaveGame::WorldStateEntries` during profile checkpoint save and imported on profile load. Public replicated entries are mirrored through `AAeyerjiGameState` fast-array replication and applied into client `UAeyerjiWorldStateSubsystem` instances.

### Promotion Helpers

Run facts stay runtime-only by default. When a StateTree or C++ system deliberately decides that a run outcome should persist, use the subsystem helpers:

| Helper | Result |
| --- | --- |
| `PromoteRunFactToPersistentCharacterFact` | Copies a run fact into character scope for a supplied owner id. It is exported into the character profile on checkpoint. |
| `PromoteRunFactToPersistentGlobalFact` | Copies a run fact into global scope. It is saved through the shared world-state save. |

These helpers preserve the run fact's typed value but rewrite scope/persistence/owner to the destination lane. This avoids StateTree content hand-rolling profile owner ids or writing persistent facts by accident.

## Run Director StateTree Bridge

`AAeyerjiWorldDirector` owns the server-side `RunDirectorStateTree` component:

1. Creates `UStateTreeComponent` with automatic start disabled.
2. Defaults the asset to `/Game/StateTrees/ST_RunDirector.ST_RunDirector`.
3. Runs only on authority/non-client worlds.
4. Loads and assigns the StateTree asset synchronously before evaluation.
5. Binds to `UAeyerjiWorldStateSubsystem::OnWorldStateChangedNative`.
6. Optionally seeds `Run.Level` as runtime-only run-scoped state for the current proof path.
7. Evaluates when `Run.*` facts change, when exact `WatchedRunDirectorTags` change, or when proof-gate tags change.
8. Starts the StateTree, then stops it immediately after one event-driven evaluation.
9. Can still use `Run.Event.SpawnMoreEnemies.Done` as the gate that prevents repeated level-proof execution.
10. Records the last evaluation reason and trigger tag for debug inspection.

This is currently a one-shot, fact-driven director evaluation model. It is not yet a full continuously running director timeline.

### Native Run Facts

`AAeyerjiGameState` publishes these runtime-only run-scoped facts on authority:

| Tag | Value | When |
| --- | --- | --- |
| `Run.State` | Name: `PreRun`, `InRun`, `BossDefeated`, `ObjectiveComplete`, `RunComplete`, or `ReturnToMenu`. | Every accepted run-state transition. |
| `Run.Zone` | Name: active zone id. | Every accepted run-state transition when an active zone exists. |
| `Run.Result` | Name: `Victory`, `TimeExpired`, `Abandoned`, or `Unknown`. | When the run reaches `RunComplete`. |
| `Run.Event.Started` | Bool true. | When the run reaches `InRun`. |
| `Run.Event.BossDefeated` | Bool true. | When the run reaches `BossDefeated`. |
| `Run.Event.ObjectiveComplete` | Bool true. | When the run reaches `ObjectiveComplete`. |
| `Run.Event.Completed` | Bool true. | When the run completes with victory. |
| `Run.Event.Failed` | Bool true. | When the run completes by time expiry. |
| `Run.Event.Abandoned` | Bool true. | When the run is manually ended/abandoned. |

These facts are cleared by `UAeyerjiWorldStateSubsystem::EndRun()` with the rest of run scope. Promote only deliberate permanent outcomes to `Character` or `Global` persistent facts.

## StateTree Tasks And Conditions

| Node | Purpose |
| --- | --- |
| `Set World State` | Writes, increments, or clears a central world-state entry with selected persistence, replication, and scope. |
| `World State Condition` | Checks existence/equality/numeric comparison for a central world-state entry. |
| `Request Loot Drop` | Rolls and spawns loot through existing loot service and pickup helpers. |
| `Loot Pity Condition` | Reads player loot memory for legendary drought, named pity, and item pickup history decisions. |

Relevant native tags:

| Tag symbol | Gameplay tag | Use |
| --- | --- | --- |
| `Run_Level` | `Run.Level` | Current run progression/value gate. |
| `Run_State` | `Run.State` | Current run-state name mirrored from GameState. |
| `Run_Zone` | `Run.Zone` | Current active zone id for StateTree branching/debug. |
| `Run_Result` | `Run.Result` | Completed run result name. |
| `Run_Event_Started` | `Run.Event.Started` | Run reached `InRun`. |
| `Run_Event_BossDefeated` | `Run.Event.BossDefeated` | Boss objective reached. |
| `Run_Event_ObjectiveComplete` | `Run.Event.ObjectiveComplete` | Main objective reached. |
| `Run_Event_Completed` | `Run.Event.Completed` | Victory completion event. |
| `Run_Event_Failed` | `Run.Event.Failed` | Time-expired failure event. |
| `Run_Event_Abandoned` | `Run.Event.Abandoned` | Manual/abandoned completion event. |
| `Run_Event_SpawnMoreEnemies_Done` | `Run.Event.SpawnMoreEnemies.Done` | Current proof gate for the level-60 run director event. |
| `Run_Difficulty_EnemyPressure` | `Run.Difficulty.EnemyPressure` | Run difficulty/pressure fact hook. |
| `World_Boss_Map1_Defeated` | `World.Boss.Map1.Defeated` | Example persistent boss/world fact. |
| `World_Teleporter_Unlocked` | `World.Teleporter.Unlocked` | Teleporter persistence tag family. |
| `World_Quest_Flag` | `World.Quest.Flag` | Quest flag persistence tag family. |

## Save And Checkpoint Boundaries

The save system is intentionally checkpoint driven. Runtime mutations can stay in memory until an explicit checkpoint commit.

Profile transport is no longer a single large client-to-server byte array. The owning client resolves the profile locally, then sends it to the authoritative pawn as bounded chunks. The server reconstructs the payload, applies it once, and logs load state as pending, applying, applied, or failed. Payloads over the old 65 KB risk threshold emit a warning and use chunked transport.

`CharacterStatsLibrary::LoadAeyerjiChar` applies profile hydration in this order and logs each phase:

1. Difficulty/world tier and loot memory.
2. Character-scoped world facts.
3. Level/XP and level-derived refresh.
4. Action bar/passive selections and granted abilities.
5. Inventory/equipment.
6. Full HP/Mana refill and replication refresh.

Checkpoint/profile commit paths include:

1. Profile creation or migration.
2. Profile load normalization.
3. Death before respawn.
4. Run completion.
5. Pawn `EndPlay`.
6. Controller/player path checkpoint helpers.
7. Return-to-menu/retry/travel and shutdown paths where profile capture is invoked.

Profile checkpoint capture includes:

1. Level/XP/stat snapshot.
2. Selected action bar abilities and passive selection.
3. Inventory and equipment.
4. Loot stats and named pity memory.
5. Difficulty/world tier selection.
6. Character-scoped persistent world-state entries.
7. Recent run records and best run times.

Shared world-state save covers global persistent world facts. Streaming save covers current zone/map/campaign cursor/quest flags/unlocked teleporters.

Run-scoped StateTree/director state is runtime-only unless explicitly promoted into a persistent global or character fact.

Checkpoint logs use `[ProfileCheckpoint]` and include reason, owner/slot key, revision, item count, equipped count, grid placement count, character world-state fact count, action-bar count, recent run count, and whether the revision is being bumped. Stale pawn `EndPlay` saves are skipped when the player state has already moved to a replacement pawn during respawn.

## Extraction And Run Completion

`AAeyerjiEndRunPortal` finalizes successful runs:

1. Server detects a player-controlled pawn overlapping the portal.
2. Server starts an extraction countdown.
3. Owning client receives countdown start/reset notifications through player controller RPCs.
4. Leaving the portal cancels the countdown.
5. Completing the countdown calls `AAeyerjiGameState::Server_CompleteExtraction`.
6. GameState snapshots victory results, persists profile/run records, finalizes the world, and destroys the portal.

## Persistence Artifacts

| Artifact | Owner | Contents |
| --- | --- | --- |
| Profile save | `UAeyerjiSaveGame` via `UAeyerjiSaveManagerSubsystem` | Character stats, selected abilities/passives, inventory/equipment, loot stats, character world facts, recent runs, best times. |
| Streaming save | `UAeyerjiStreamingSaveGame` via `UAeyerjiStreamingSubsystem` | Current zone, current gameplay map, campaign mode/cursor, unlocked teleporters, quest flags. |
| Shared world-state save | `UAeyerjiWorldStateSaveGame` via `UAeyerjiWorldStateSubsystem` | Global persistent world-state entries. |

The save manager stamps schema version, owner key, artifact kind, revision, and modified time. Profile and streaming saves can mirror to Steam UserCloud when available; shared world state is a fixed shared local artifact.

## Network Model

1. Run state, run results, objective state, objective events, world-flow phase, active zone, transition id, loading blocker count, and public world-state entries replicate through `AAeyerjiGameState`.
2. State changes are server-authored.
3. Clients perform local streaming for replicated zone transitions when enabled and report readiness to the server.
4. World-state writes are accepted only on authority for authoritative entries.
5. Public world-state entries use fast-array replication and are applied into client world-state subsystems.
6. Pickup and inventory grant paths remain authority controlled.

## Debug Logging And Helpers

| Prefix/API | Coverage |
| --- | --- |
| `[ProfileLoad]` | Client profile resolution, chunk transfer state, server apply state, hydration phases, and missing subsystem warnings. |
| `[ProfileCheckpoint]` | Checkpoint commit summaries and stale pawn save skips. |
| `[RunDirector]` | StateTree evaluation reason and trigger tag. |
| `[WorldState]` | Promotion helper success/failure details. |
| `AAeyerjiGameState::GetRunLifecycleDebugString` | Run state, result, result version, active zone, transition id, objective readiness, and world-state summary. |
| `UAeyerjiWorldStateSubsystem::GetWorldStateDebugSummary` | Active run id, total fact counts, scope/persistence counts, and dirty state. |
| `UAeyerjiWorldStateSubsystem::GetRunFactDebugStrings` | Sorted string list of current run-scoped facts. |
| `AAeyerjiWorldDirector::GetLastRunDirectorEvaluationReason/Tag` | Last reason/tag that woke `ST_RunDirector`. |

## Confirmed Missing Or Incomplete

| Gap | Impact | Recommended next step |
| --- | --- | --- |
| `ST_RunDirector` content wiring | C++ bridge exists, but production branches still need to be authored in the StateTree asset. | Add states/conditions/tasks in editor using `Run.Level`, event facts, reward tasks, and world-state gates. |
| Level-proof mode still defaults on | Useful for proof path, not final production progression. | In the placed `AAeyerjiWorldDirector`, disable `bUseLevelProofGate` / `bSeedLevelProofState` once `ST_RunDirector` branches from real `Run.*` facts. |
| Run director evaluation is one-shot restart/stop | Good for fact-driven decisions, not a long-lived director timeline. | Decide whether `ST_RunDirector` should stay event-driven or become a continuously running director. |
| Normal enemy reward route content enablement | A C++ opt-in enemy death hook exists, but enemy archetypes/classes decide whether it is used. | Enable `bSpawnNormalDeathLoot` only on enemies that should use the generic route; keep bosses/special encounters on StateTree/custom rewards. |
| Content actors still need setup | Level director, world director, portals, teleporters, spawners, and manifest data need correct placed references. | Track required actor properties in per-map editor checklist. |
| Run-specific persistent facts need design rules | Some facts should be runtime-only, others character/global persistent. | Define persistence policy per fact in a source matrix. |
| Debug UI is missing | Logs exist, but no unified runtime panel for run state/world flow/world facts. | Add debug widget/console commands after editor flow stabilizes. |

## Editor Work Queue

1. Place or verify `AAeyerjiWorldDirector` in the persistent/root map.
2. Confirm `RunDirectorStateTreeAsset` points to `ST_RunDirector`.
3. In `ST_RunDirector`, add production branches using `World State Condition`, `Set World State`, `Request Loot Drop`, and `Loot Pity Condition`.
4. Disable the level-proof seed/gate on `AAeyerjiWorldDirector` once `ST_RunDirector` uses real `Run.State`, `Run.Zone`, `Run.Result`, and `Run.Event.*` conditions.
5. Configure zone manifest data: menu zone, gameplay zones, sublevels, player start tags, spawn policy, and map rotation/campaign list.
6. Verify each gameplay zone has the intended `AAeyerjiLevelDirector`, `AAeyerjiEncounterDirector`, spawners, boss gate, extraction portal class, and optional portal spawn point.
7. Configure fixed-population zones so initial spawn can act as a world-flow loading blocker where desired.
8. Define which world-state facts are `Run`, `Character`, `Global`, or `Session`.
9. Add designer-facing source matrix covering run facts, objective events, loot sources, reward drops, persistence, and retry/checkpoint behavior.
10. Manually validate dedicated server plus separate client: zone transition, player ready, start run, objective completion, extraction, checkpoint save, return to menu, retry.
11. For `AAeyerjiWorldDirector`, use the debug getters to verify what fact woke `ST_RunDirector` while authoring branches.
12. For normal enemies, decide which archetypes/classes should enable `bSpawnNormalDeathLoot` and whether each uses single-roll or multi-drop config.

## Do Not Duplicate

Do not create another run state enum, world-flow state machine, world-state registry, streaming save object, character checkpoint save path, extraction portal type, or parallel StateTree fact storage. The current architecture already has owners for those responsibilities.
