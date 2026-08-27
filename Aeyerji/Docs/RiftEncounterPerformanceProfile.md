# Rift Encounter Performance Profile

Use this runbook after the `NeonMap` SpawnRegions have valid **Rift Progression Index** values and the normal two-client smoke test reaches gameplay.

## Capture setup

Launch the server or listen host with:

```text
-trace=cpu,counters,frame,bookmark,net -statnamedevents
```

Capture at least:

1. Transition into `NeonMap` and the complete 120-actor prewarm.
2. First 4-enemy ambient stage.
3. One complete 8-enemy reinforcement reveal.
4. A 48-awake-enemy combat interval.
5. Leaving and returning to a region with sleeping enemies.

The C++ trace events to inspect are:

- `Aeyerji_FreshEnemyConstruction`
- `Aeyerji_EnemyPoolPrewarmExact`
- `Aeyerji_PooledEnemyCheckout`
- `Aeyerji_RiftNavigationPlacement`
- `Aeyerji_EnemyGASScalingRegistration`
- `Aeyerji_EnemyGASEliteRegistration`
- `Aeyerji_EnemyControllerStateTreeActivation`
- `Aeyerji_RiftRegionStaging`
- `Aeyerji_RiftReinforcementReveal`

The Rift counters are:

- `Aeyerji/Rift/AwakeEnemies`
- `Aeyerji/Rift/SleepingEnemies`
- `Aeyerji/Rift/RevealingEnemies`
- `Aeyerji/Rift/StagedPopulation`
- `Aeyerji/Rift/PooledEnemies`
- `Aeyerji/Rift/FreshConstructions`
- `Aeyerji/Rift/EmergencySpawns`

At run completion, the log must report:

```text
[RiftRun][PoolAcceptance] ... FreshSpawnsDuringGameplay=0
```

## Four controlled cases

Record server game-thread time, reveal-frame time, awake count, and network traffic for each case.

| Case | Content setup | Purpose |
| --- | --- | --- |
| Lightweight pawn | Minimal skeletal or capsule enemy using the same encounter plan | Establish encounter/pool overhead without Paragon content |
| Paragon pawn, AI disabled | Current placeholder mesh/animation with brain, perception, and movement disabled | Isolate skeletal animation/render/content cost |
| Paragon pawn, AI enabled | Current normal Rift enemy | Measure AI, GAS, StateTree, movement, animation, and replication together |
| Dedicated server | Same 120 plan and 48-awake cap on `AeyerjiServer` | Remove client rendering and isolate authority simulation/networking |

Do not compare cases with different region indices, run seed, enemy budget, or participant route. Use the same fixed run seed for all four captures.

## Acceptance targets

- Full planned population is prewarmed before `TransitionLoading` clears.
- `Aeyerji/Rift/EmergencySpawns` remains `0`.
- Staging/reveal work remains below `2 ms` per server tick.
- No reveal creates an attributable frame above `33 ms`.
- The lightweight case sustains a 60 Hz server with 120 instantiated and 48 awake enemies.
- Paragon results are recorded separately as placeholder-content cost rather than treated as encounter-system cost.
