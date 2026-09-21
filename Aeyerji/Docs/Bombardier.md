# Bombardier implementation and editor setup

The native foundation supplies a fixed ground warning, delayed physical GAS
damage, cancellation, replicated presentation, and combat telemetry. A playable
Bombardier character still needs the editor setup below. No character, montage,
material, archetype asset, or production encounter was created by this source pass.

## Gameplay contract

| Setting | Native default | Owner |
| --- | --- | --- |
| Warning duration | 1.4 seconds; configurable 0.75-2 seconds | `UGA_EnemyBombardment.WarningSeconds` |
| Blast radius | 250 cm; configurable 50-600 cm | `BlastRadius` on ability |
| Blast vertical half-height | 180 cm; configurable 50-500 cm | `AAeyerjiBombardment.BlastHalfHeight` |
| Attack interval | 5 seconds between starts; configurable 3-30 seconds | `AttackIntervalSeconds` on ability |
| Damage | AttackDamage at warning start times DamageScalar (default 1) | Enemy ASC and ability |
| Simultaneous warnings | At most 2 across the entire world, including subclasses | Native actor admission check |
| Source health | 50 HP/HPMax | Existing `R_BombardierAttrs.JSON` |
| Source attack damage/range | 30 / 1200 cm | Existing `R_BombardierAttrs.JSON` |

The JSON numbers are unchanged. World/level scaling and imported asset values
still determine the enemy's live ASC. AttackSpeed does not shorten the warning
or the five-second attack interval. These are deliberate readability limits;
the generic balance matrix's APS column is not the Bombardier's actual cadence.

The ability uses `Ability.Primary.Ranged.Bombardment` and fits the existing
StateTree primary-attack task. It resolves the AI controller's current target,
checks hostility, range and visibility, then traces to WorldStatic ground below
that target's feet. No valid ground means no cast. The locked center does not
follow the target. At impact, the server gathers current occupants, rejects
friendly/unknown-team/dead/non-damageable actors, tests actor centers against
the visible cylinder, and checks visibility from the blast to each target.
Each eligible actor receives at most one physical damage spec.

Armor and the existing GAS damage execution apply normally. Variance, critical
hits, dodge rolls, life steal, on-hit procs and stagger are disabled by default
on this ability. Moving clear is the intended avoidance mechanism. Default
damage uses `UGE_DamagePhysical`; the old Archer projectile effect is not used.

Death and ability cancellation remove pending damage. Stun/stagger/source
inactivation are checked during the warning and again at impact. Pool return
explicitly cancels only this primary ability before reuse. Once synchronous
impact resolution begins it completes once, even if a damage callback cancels
the ability. Completion is sent on success and cancellation so the shared
StateTree does not wait indefinitely.

## Editor setup

1. Load the rebuilt Editor module. Create a duplicate of the working Archer
   character at `/Game/Enemy/Map_1_Creeps/R_Bombardier/R_Bombardier`. Duplicating
   preserves its working controller, movement, skeleton, animation and death
   integration while you choose the Bombardier's final appearance.
2. Create an ability Blueprint derived from `GA_EnemyBombardment`, for example
   `GA_BombardierPrimary`. Assign an optional `CastMontage`; warning timing is
   independent of montage notifies. Use the native defaults for the first run.
   Do not implement Blueprint ActivateAbility or add another damage notify.
3. Add a separate entry to `/Game/Enemy/AeyerjiEnemyArchetypeLibrary` using
   `Enemy.Role.Mob.Ranged.Bombardier`. Start from the Archer entry's working
   presentation/team/init setup. Set AttributeDefaultsTable to
   `/Game/Enemy/Attributes/R_BombardierAttrs`, and replace its primary grant with
   `GA_BombardierPrimary`. Keep stat multipliers at 1 for the baseline. Replace
   any copied Archer-specific role/primary tags with the Bombardier tags.
4. Point the duplicated character's archetype component at that library entry.
   Inspect both startup abilities and archetype GrantedAbilities: there must
   be exactly one primary, the Bombardier ability. Archetype grants do not
   replace an inherited Archer ability automatically. Also check Blueprint
   BeginPlay grants and inherited default arrays.
5. Reimport `Data/EnemyAttributes/R_BombardierAttrs.JSON` into its matching
   DataTable if the editor values differ. Save the new character, ability and
   library entry. Confirm live HP, AttackDamage, AttackRange and role tag.
   Do not reimport `EnemyBalanceTargets.json`; its readiness fields are
   documentation metadata, not a gameplay DataTable.
6. Optionally create a Blueprint child of `AeyerjiBombardment` for presentation,
   then select it in the ability's BombardmentClass. The native actor already
   draws a white segmented boundary and a shrinking countdown using engine
   cube instances. WarningMaterial can supply an emissive material. Use
   `BP_OnBombardmentStateChanged` to start/stop your warning and spawn impact
   effects; use `BP_UpdateWarning` for countdown animation. All these hooks
   are cosmetic and skipped on a dedicated server. Handle a terminal phase
   arriving without a prior warning (late relevance) and actor destruction.
7. Tune the final telegraph to terrain: the native fallback is a horizontal
   ring, so it can intersect slopes. Use an authored ground decal/VFX for the
   final presentation and verify its radius matches damage. Do not hide the
   native warning until the replacement is readable in a dense fight.

The replicated snapshot contains center, radius, warning duration, server
impact time and phase. Remote clients derive countdown from GameState server
time. The two-warning cap allows warnings to be always relevant independently
of caster relevance. There is no client-side damage timer. A terminal actor
remains for 0.5 seconds for presentation, then is destroyed. Cosmetic effects
that need longer should finish in their own effect component/system.

## First mixed test

Use an isolated arena/PIE world as described in [CombatBalanceTesting.md](CombatBalanceTesting.md).
Then run:

```text
AJ_CombatTestPreset Bombardier12 1 167 1337 3
AJ_CombatTestStatus
AJ_CombatTestStop
```

`Bombardier12` contains one Bombardier, seven Grunts, two Bulwarks and two
Archers. The seeded roster shuffle and production spawner path are retained.
The preset reports setup failure if the expected Bombardier Blueprint is
missing; it never silently substitutes an Archer. Other presets and production
encounters retain their existing compositions. Add production encounter
entries only after this first test, initially one Bombardier per ordinary pack.
The implemented cap limits warnings, not the number of Bombardier pawns.

Check that stepping outside the ring avoids damage, stepping in late takes
damage, walls/floors prevent inappropriate hits, and repeated capsule/mesh
overlaps do not duplicate damage. Kill, stun, and pool-return the caster during
the warning, then reuse it. Test two remote clients and listen-server
presentation, including latency/late relevance. The fixed telegraph duration
does not guarantee a full 1.4 seconds on a delayed client's screen; tune with
real latency before production acceptance.

## Reports and validation

The combat telemetry skill's integration checklist informed this implementation:
`_bombardments.csv` records Started/Impacted/Cancelled with source/archetype,
original aim target, cast ID, center/radius, candidates, submitted damage
applications, active/peak zones and cumulative counters. Source/aim labels and
geometry are frozen at start. Unknown counts use -1. Only warnings whose start
was observed during recording contribute; starting observation mid-warning
does not invent a start event. Recording is world-scoped and survives player
respawn without player-bound delegates.

Six appended `_samples.csv` fields mirror active/peak zones, starts, impacts,
cancellations and damage applications. Summary totals include dropped event
rows and untracked starts. Event storage caps at 8192 rows and active tracking
at 64; cumulative counters continue after row truncation. DamageApplications
means a submitted GAS spec, not HP lost or a confirmed hit. Existing
`_damage.csv` contains actual incoming damage to the observed player. Empty
blasts do not prove intentional player avoidance. ActiveZonesAtStop may be
nonzero when recording stops mid-warning.

Automation lives in `Private/Tests/AeyerjiBombardmentTest.cpp` under
`Aeyerji.CombatTest.Bombardment`. Tests use isolated native physics worlds and
do not establish that the future character Blueprint, imported data, VFX,
multiplayer presentation or packaged content are ready. Build/test results
from the implementation pass are reported separately in the handoff.

### Verified 2026-09-06

- UE 5.8.2 `AeyerjiEditor Win64 Development` build succeeded.
- `Aeyerji.CombatTest`: 5 passed, 0 failed, 0 warnings. The new Gameplay test
  exercises actual GAS damage, movement out of the blast, repeated resolution,
  friendly filtering, stun cancellation, ability activation and pool return.
  Telemetry tests verify deduplication, loss of the observed player, bounded
  storage, quoted CSV fields and the actual production report writer.
- Report: `Saved/Automation/Bombardier/index.json`; log:
  `Saved/Logs/Bombardier-Automation.log`.
- Synthetic report-writing fixture:
  `Saved/CombatTests/AJCT_20260906-120431_BombardmentAutomation_L01_WT167_N00_S1337_*`.
  Its bombardment CSV has 18 columns and samples CSV has 40, with aligned
  rows and matching summary counters. This is an automation artifact, not a
  production combat or visual acceptance run.
- `python Scripts/validate_enemy_balance.py --check` passed after regenerating
  the balance matrix. The 13 existing pressure-band warnings remain for tuning.
- Editor character setup, final telegraph readability, separate-client
  replication/latency and a cooked mixed-pack run remain unverified.
