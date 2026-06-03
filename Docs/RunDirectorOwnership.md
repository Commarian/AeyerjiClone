# Run Director Ownership

This document defines where run-related decisions belong. The goal is to keep
runtime authority in C++ while moving designer-facing choices into definition
assets.

## Ownership Model

### AAeyerjiGameState

Owns server-authoritative run lifecycle and replication.

- Allowed to change `EAeyerjiRunState`.
- Publishes `Run.State`, `Run.Zone`, `Run.Result`, and `Run.Event.*`.
- Freezes and persists completed run results.
- Writes permanent world facts when a run milestone is accepted.
- Should not own designer tuning such as boss class, wave list, spawn region, or
  encounter pacing.

### AAeyerjiWorldDirector

Owns persistent-root bootstrap only.

- Starts the initial zone flow.
- Hosts persistent-map level references.
- May seed temporary proof state while testing.
- Should not decide encounter content, boss spawn rules, or reward rules.

### AAeyerjiLevelDirector

Owns one gameplay zone instance.

- Exposes only the zone/run definition asset as designer-facing configuration.
- Resolves the active encounter, spawner groups, boss spawner, boss gate, portal
  spawn point, and run objective rules from that definition at runtime.
- Starts and stops local run mechanics such as timers, gates, checkpoints, and
  extraction portals.
- In `SurvivalRounds` mode, owns the active round/cycle, generates runtime waves
  from the survival mission definition, triggers boss rounds, and publishes
  replicated round state through `AAeyerjiGameState`.
- Emits persistent fact writes requested by its zone/boss definition.
- Should not expose duplicate boss class, spawn profile, spawner sequence,
  teleporter, extraction, or difficulty knobs on the placed actor.

### AAeyerjiEncounterDirector

Owns encounter planning and dynamic adjustment.

- Reads an encounter director definition asset.
- Owns pacing, dynamic spawn groups, kill target progress, fixed population
  profile execution, and enemy LOD policy.
- Decides when more combat pressure is needed.
- Should not own global run lifecycle or completed-run persistence.

### AAeyerjiSpawnerGroup

Owns mechanical spawning only.

- Executes wave data or manual spawn registration.
- Executes generated survival round waves passed through
  `ActivateEncounterWithRuntimeWaves`.
- Owns spawn points, door toggles, live enemy tracking, aggro handoff, and
  elite package application.
- Should not decide run progression, boss progression, persistent unlocks, or
  dynamic pressure policy.

## Definition Assets

### Zone Run Definition

Use one asset per gameplay zone. It should answer:

- Which encounter director definition controls dynamic pacing?
- Which spawner groups are in the sequence?
- Which spawner is the boss spawner?
- Which actor is the boss gate?
- Which fixed population profile is used?
- Which survival mission definition is used when the zone runs round-based
  survival testing?
- What win condition and kill target apply?
- Which boss definition applies?
- Which persistent facts are written when boss/run milestones happen?

Actor references should be expressed as Actor tags, not direct actor picks, so
the definition can survive streaming and level duplication. The practical
migration from the old placed `BP_AeyerjiLevelDirector` fields is:

- `World Spawn Profile` -> `ZoneRunDefinition.WorldSpawnProfile`.
- `World Population Spawner` -> add an Actor tag to the placed
  `AeyerjiSpawnerGroup`, then set `ZoneRunDefinition.WorldPopulationSpawnerActorTag`.
- `Spawner Sequence` -> add Actor tags to each sequence spawner, then list them
  in `ZoneRunDefinition.SpawnerSequenceActorTags`.
- `Boss Enemy Class` -> `BossDefinition.BossPawnClass`.
- Old boss rows in `AllEnemyLootTable` -> copy only designer-owned loot tuning
  into `BossDefinition` fields, and copy the old row `MultiDropConfig` into
  `BossDefinition.BossMultiDropConfig`. Runtime values such as player actor,
  enemy level, player level, world tier, and difficulty are supplied by
  `BossDefinition.MakeBossLootContext(...)`.
- Boss loot source -> `BossDefinition.LootSourceTag`, usually
  `Loot.Source.Boss`, matching a `BP_AeyerjiLootTable` pool source tag.
- `Boss Spawner`, `Boss Gate`, and `Boss Spawn Marker` -> add Actor tags to
  those placed actors, then set them on the boss definition or zone overrides.
- `Boss Spawn Instigator` / `BP_BossTrigger` -> add an Actor tag to the trigger,
  then set `BossDefinition.BossTriggerActorTag` when Blueprint needs to resolve
  or debug the trigger.
- Extraction portal class/spawn point -> `ZoneRunDefinition.EndRunPortalClass`
  and `EndRunPortalSpawnPointTag`.

### Encounter Director Definition

Use one asset per encounter pacing profile. It should answer:

- Which dynamic spawn groups can be selected?
- What pacing thresholds gate new packs?
- How are spawn locations filtered?
- What LOD/sleep settings apply?
- What debug behavior is enabled?

### Encounter Definition

Use the existing `UAeyerjiEncounterDefinition` for authored wave lists. This is
the spawner's input data, not the owner of run progression.

## Persistence Rules

Run-scoped facts are temporary and cleared by `EndRun()`.

- `Run.State`
- `Run.Zone`
- `Run.Result`
- `Run.Event.*`

Permanent knowledge is written as persistent world state.

- Boss defeated facts use global persistent scope.
- Completed-zone facts use global persistent scope.
- Character-specific facts should use character scope and an owner id.

The accepted run milestone should write persistence once, at the point C++ has
accepted the state transition. Do not let StateTree or UI code fake lifecycle
facts.

## Debug Surface

Use `aeyerji.Run.Debug` in the console to print:

- GameState run state, world phase, active zone, transition id.
- Bound LevelDirector, EncounterDirector, and BossSpawner.
- LevelDirector config asset and boss definition.
- Current objective snapshot.
- Run-scoped world facts.
