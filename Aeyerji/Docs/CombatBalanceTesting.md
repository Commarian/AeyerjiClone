# Combat Balance Testing

## Purpose

This runbook turns the level-one balance contract into repeatable crowd tests.
It is designed for Aeyerji's intended combat fantasy: many enemies remain on
screen, ordinary enemies are modest individual threats, and aggregate pressure
is dangerous without producing arbitrary synchronized deaths.

The transient `AAeyerjiCombatBalanceTestHarness` uses the live
`BP_AeyerjiSpawnerGroup` path. Test enemies therefore receive the normal
archetype defaults, level and world-tier scaling, elite package, GAS setup, AI
possession, StateTree startup, aggro, death flow, and replication. The harness
does not create simplified combat dummies.

The harness is a development test aid, not a replacement for final Rift and
Survival playtests. It never edits assets, removes equipment, changes inventory,
or saves character progression.

## Fast path

1. Restart the editor after enabling the Animation Insights plugin through the
   project descriptor.
2. Open a gameplay map with a valid NavMesh and enough clear space around the
   player. Use an empty arena or a cleared section of `NeonMap`; unrelated live
   encounter enemies will contaminate incoming-damage results.
3. For the canonical test, use a level-one character with no equipment or
   temporary effects. The HUD reports `Baseline=PASS` only when the live ASC has
   level `1`, `760` live HPMax, and `25` AttackDamage. The player attribute table
   authors `750` base HP/HPMax; the remaining `10` comes from the canonical
   level-one derived-stat contribution. The harness warns rather than changing
   the character when those values differ.
4. Start PIE or Standalone and open the console.
5. Run:

   ```text
   AJ_CombatTestPreset Dense24 1 167 1337 3
   ```

6. Fight normally. The pack assembles in a deterministic annulus while locked,
   then releases together three seconds after assembly.
7. Use `AJ_CombatTestStatus` for a detailed local readout.
8. A clear or player death writes reports automatically. Run
   `AJ_CombatTestStop` afterward to export the latest state again and destroy
   only the harness-owned actors.

Reports are written under `Saved/CombatTests`.

### Isolated NeonMap launch

For PIE in the current editor process, open **Window > Output Log** and enter:

```text
aeyerji.CombatTest.Isolated 1
```

Set this before pressing Play or before streaming into `NeonMap`. The value is
process-wide and persists across PIE sessions until the editor closes or you
enter `aeyerji.CombatTest.Isolated 0`. The **Additional Launch Parameters** field
under **Play in Standalone Game** does not apply to ordinary PIE, especially
when **Run Under One Process** is enabled.

For a separately launched editor or game process, pass
`-AeyerjiCombatTestIsolated` before
streaming into `NeonMap` to suppress production world spawning while leaving
streaming, NavMesh, player setup, and explicit combat-test roster spawning intact.
For proximity-region zones this also skips the production Rift plan and exact
population prewarm, allowing the world-flow loading screen to release normally.
The combat-test HUD reports `Isolation WorldSpawning=SUPPRESSED` when the option
is active. For example:

```powershell
& "X:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" `
  "X:\UnrealProjects\Aeyerji\Aeyerji\Aeyerji.uproject" `
  -AeyerjiCombatTestIsolated
```

Both controls must be enabled before the zone loads; they do not remove enemies
that already exist. Use console-variable value `0`, or close a process launched
with the option and launch normally, to restore production spawning on the next
PIE/run. Shipping builds always ignore both controls.

For unattended runtime smoke tests only, pass
`-AeyerjiCombatTestAutoSpawn` on the process command line. In non-shipping builds
this lets an explicit combat-test command replace the front-end spectator with
the normal default pawn before setup. The switch is ignored unless a combat-test
command is issued and is not needed after starting a normal PIE/Standalone run.

## Canonical presets

All presets default to enemy level `1`, Normal world tier `167`, seed `1337`,
and a three-second post-assembly countdown.

| Preset | Population | Composition | Primary use |
| --- | ---: | --- | --- |
| `Sanity1` | 1 | Grunt | Individual role/stat sanity check. |
| `Floor8` | 8 | Mixed | Current Rift minimum-pressure checkpoint. |
| `Region12` | 12 | Mixed | One region-sized population checkpoint. |
| `Ranged12` | 12 | Archer/support | Projectile pressure and synchronized ranged burst. |
| `Grunt20` | 20 | Grunt | Homogeneous diagnostic retained from the original balance plan. |
| `Dense24` | 24 | Mixed | Primary proposed screen-density baseline. |
| `Elite24` | 24 | Mixed with one Grunt Elite | Trash/elite priority and durability separation. |
| `Overwhelmed36` | 36 | Mixed | High-pressure and kill-rate deficit test. |
| `Cap48` | 48 | Mixed | Current Rift maximum-awake gameplay ceiling. |
| `Bombardier12` | 12 | 1 Bombardier, 7 Grunts, 2 Bulwarks, 2 Archers | Opt-in specialist test; requires the new character Blueprint. |

`Mixed` repeats a transparent six-enemy recipe: three Grunts, one Bulwark, one
Archer, and one Support, then deterministically shuffles the complete roster.
`MixedElite` replaces one ordinary slot with a Grunt Elite. This standardized
mix makes comparisons repeatable; it does not claim to be the final authored
Rift composition.

Follow [Bombardier setup](Bombardier.md) before running `Bombardier12`. Its
`BombardierMixed` composition requires the exact documented character path
and fails setup if the asset is absent. Existing presets do not load that
new class. Bombardment events are written to `_bombardments.csv`; six appended
sample columns and summary counters record warning starts, impacts,
cancellations, active/peak zones and submitted damage applications. An
application is not proof of HP loss; use `_damage.csv` for actual damage to
the observed player. Only casts starting during recording are counted.

## Command reference

Print the in-game reference:

```text
AJ_CombatTestHelp
```

Start a preset:

```text
AJ_CombatTestPreset <Preset> [EnemyLevel=1] [WorldTier=167] [Seed=1337] [AutoEngageDelay=3]
```

Examples:

```text
AJ_CombatTestPreset Sanity1
AJ_CombatTestPreset Dense24 1 167 1337 3
AJ_CombatTestPreset Dense24 10 167 1337 3
AJ_CombatTestPreset Cap48 50 999 9001 -1
```

`AutoEngageDelay=-1` leaves every enemy prepared and combat-locked until:

```text
AJ_CombatTestEngage
```

Build a custom roster:

```text
AJ_CombatTestCustom <Composition> <Count> [EnemyLevel] [WorldTier] [Seed] [MinRadius] [MaxRadius] [AutoEngageDelay] [SpawnInterval]
```

Supported compositions are `Grunt`, `Bulwark`, `Archer`, `Support`, `Ranged`,
`Mixed`, `MixedElite`, and `Elite`. Counts are deliberately limited to `1..48`
to match the current Rift awake ceiling.

Example custom cases:

```text
AJ_CombatTestCustom Grunt 20 1 167 1337 1100 2400 3 0.05
AJ_CombatTestCustom Archer 12 1 167 1337 1600 2800 -1 0.05
AJ_CombatTestCustom MixedElite 24 25 400 1337 1200 2600 3 0.05
```

Operational commands:

```text
AJ_CombatTestStatus
AJ_CombatTestMark "Started moving"
AJ_CombatTestHUD 0
AJ_CombatTestHUD 1
AJ_CombatTestStop
```

Commands entered by a client are sent through that client's authoritative
`AAeyerjiPlayerController`. Enemy creation, scaling, metrics, report writing,
and cleanup remain server-owned. The requesting player is the measured player.

## Rewind Debugger workflow

The project descriptor enables Epic's Animation Insights plugin, which provides
the Rewind Debugger. After restarting the editor:

1. Open **Tools > Debug > Rewind Debugger** and **Rewind Debugger Details**.
2. Optionally enable **Should Auto Record on PIE** in Rewind Debugger settings.
3. Start PIE and begin recording.
4. Assemble without starting combat:

   ```text
   AJ_CombatTestPreset Dense24 1 167 1337 -1
   ```

5. Wait until the harness HUD says `State=Ready`.
6. Add any human-readable marker, then release the pack:

   ```text
   AJ_CombatTestMark "Camera positioned"
   AJ_CombatTestEngage
   ```

7. After the event of interest, pause PIE and eject. Select the player or an
   enemy using the Rewind Debugger eyedropper. Inspect the actor, its attached AI
   controller, movement, animation instance, montage/notifies, and related
   tracks while scrubbing around the `AJCombatTest/*` trace bookmarks.
8. Open `/Game/AI/StateTree/STEnemyGeneral`, then **Window > Debugger**, to inspect
   the selected StateTree instance's active states, tasks, conditions, and
   transitions. The StateTree debugger can also consume client/server trace
   sessions when diagnosing dedicated-server differences.

The harness emits these automatic bookmarks:

- `SpawnBegin`
- `Ready`
- `Engage`
- `FirstDamage`
- `FirstKill`
- `PlayerDead`, `Cleared`, `ManualStop`, or `SessionEnded`

Use Rewind and the StateTree debugger for causal inspection: who selected which
target, when a chase transitioned to pressure/attack, which montage and notify
opened a hit window, and whether movement/perception state matched the visible
behavior. Use the harness reports for aggregate quantities that are awkward to
derive by scrubbing dozens of actors.

Trace recordings are retained by Unreal's trace store. Periodically remove old
recordings through the Trace menu's **Open Trace Store Directory** action.

## What the HUD and reports measure

The replicated HUD displays:

- requested, spawned, failed, living, and combat-active population;
- living enemies within `1000`, `2000`, `4000`, and `8000` cm of the measured
  player;
- this client's viewport-projected living population;
- player HP, minimum HP, and captured AttackDamage;
- total incoming damage, average incoming DPS, and maximum damage inside rolling
  `0.5`, `1`, and `3` second windows;
- kills, elapsed time, seed, explicit enemy level, world tier, and result.

`ViewportVisible` means the actor projects inside the local viewport. It is a
stable per-client screen-occupancy signal, but it is not an occlusion query; an
enemy hidden behind a wall can still count.

Each run writes four files with the same timestamped base name:

| Suffix | Contents |
| --- | --- |
| `_summary.txt` | Inputs, baseline state, result, clear/first-event times, DPS, and burst maxima. |
| `_samples.csv` | Population, distance bands, HP, cumulative damage, and kills every 0.25 seconds. |
| `_damage.csv` | Every observed negative player-HP delta with time and best available effect-causer identity. |
| `_markers.csv` | Automatic and manual marker times. |

Damage from unrelated actors or self-effects is still recorded because the
measurement is the player's authoritative HP delta. The source column makes
contamination visible; run in a controlled arena for acceptance captures.

## Test matrix aligned with the balance plan

### Level-one naked acceptance

Run `Sanity1`, `Floor8`, `Region12`, `Dense24`, `Elite24`, `Overwhelmed36`, and
`Cap48` using the same seed. Record:

- time to first kill and clear;
- lowest HP and death result;
- average DPS and all three burst windows;
- alive/active population over time;
- how often viewport population approaches zero;
- whether ordinary enemies normally die before the elite.

Repeat `Dense24` at least three times without changing the seed. A large result
spread with identical inputs points toward attack synchronization, target/path
selection, animation-window timing, or player execution rather than roster RNG.

### Stationary pressure budget

Use `Dense24`, do not move or attack after `Engage`, and measure survival time.
The provisional seven-second target for a canonical 500-HP player corresponds
to about `71` observed pack DPS. This is a design hypothesis to review, not an
automated pass/fail threshold.

### Active-play pressure

Repeat `Dense24` while moving and using starting abilities. The player should be
able to win reliably, kills should visibly lower immediate pressure, and random
trash should not create a lethal untelegraphed burst.

### Synchronization diagnosis

Use `Grunt20` and `Ranged12`. Compare average DPS against peak `0.5`/`1` second
damage. If average pressure is acceptable but a peak is not, scrub around the
matching damage rows and trace bookmarks to inspect StateTree transitions,
montages, and hit notifies before changing raw damage.

### Progression sweep

Use the same composition, count, world tier, and seed at enemy levels `1`, `10`,
`25`, and `50`. Pair each with the intended factual player build for that
checkpoint. Explicit enemy level does not automatically level, gear, or strip
the player.

### Multiplayer

Use a dedicated server with separate clients. The harness measures the
requesting player's HP and replicates global population to all clients; each
client computes its own `ViewportVisible` count. Record both clients when the
party stays together and when it separates, because the current 48-awake cap is
party-global rather than per-screen.

### Performance

Use `Cap48` for quick AI/GAS/animation stress and preserve the same seed. For
production acceptance of the 120-actor prewarm, pooled reveal, network traffic,
and 48-awake cap, continue to use
[Rift Encounter Performance Profile](RiftEncounterPerformanceProfile.md); the
transient harness intentionally does not reproduce the frozen Rift pool plan.

## Safety and cleanup

- Only Development/Editor test commands can start the harness; Shipping rejects
  them.
- Starting a new test first removes the prior harness and only its owned/tagged
  test enemies.
- `AJ_CombatTestStop` never resets a production encounter director or destroys
  unrelated enemies.
- A player baseline mismatch is reported, not silently corrected.
- Navigation projection can move deterministic candidate points around local
  obstacles. Always retain the map, transform, and seed with captured results.
- No `.uasset` or `.umap` setup is required. The Animation Insights plugin
  change requires an editor restart.
