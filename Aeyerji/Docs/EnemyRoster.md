# Enemy roster and implementation readiness

Source inventory reviewed on 2026-09-05. The balance contract is
[EnemyBalanceTargets.json](../Data/EnemyBalanceTargets.json). Existing attribute
numbers remain tuning proposals until checked against the imported asset and
the spawned enemy's ASC.

## What exists

The contract contains 23 enemy definitions: 22 have a declared matching
attribute DataTable asset whose file exists, and `R_Ranged` is a source-only
template. `SurvivalDefenseObj_Tree.json` is explicitly excluded because it is
a mission objective. Generic `AttribsMeleeMob` and `AttribsRangedMob` assets
are not additional enemy definitions.

The user reports Grunt, Bulwark, Archer, Support, and the implemented Grunt
elite as playable. The current source survival rounds and built-in balance
harness reference those five archetypes. The six corresponding character
asset paths below exist, but this filesystem inventory cannot inspect their
Blueprint graphs, inherited defaults, current imports, or cooked behavior.

| Archetype | Implementation evidence | Known asset object path |
| --- | --- | --- |
| Grunt | User reports implemented; survival source and harness reference it | `/Game/Enemy/Map_1_Creeps/M_Grunt/M_Grunt.M_Grunt` |
| Bulwark | User reports implemented; survival source and harness reference it | `/Game/Enemy/Map_1_Creeps/M_Bulwark/M_Bulwark.M_Bulwark` |
| Archer | User reports implemented; survival source and harness reference it | `/Game/Enemy/Map_1_Creeps/R_Archer/R_Archer.R_Archer` |
| Support | User reports implemented; survival source and harness reference it | `/Game/Enemy/Map_1_Creeps/R_Support/R_Support.R_Support` |
| Grunt Elite | User reports an implemented elite; survival source and harness use this path | `/Game/Enemy/Map_1_Creeps/M_Grunt/M_GruntElite.M_GruntElite` |
| Archer Elite | Asset path exists; behavior and encounter use need verification | `/Game/Enemy/Map_1_Creeps/R_Archer/R_ArcherElite.R_ArcherElite` |

There is also `/Game/Enemy/Map_1_Creeps/Elites/M_Grunt.M_Grunt`.
Treat this seventh candidate file as an editor follow-up: inspect its parent,
role tag, attribute defaults, and references before deciding whether it is an
alternate Grunt Elite, another archetype, or unused content. Its filename does
not establish any of those possibilities. Shared base-character, mesh,
material, loot, and reward assets under `Map_1_Creeps` are not counted as
additional implemented archetypes.

## Readiness fields

`RuntimeStatus` retains its legacy meaning for the Python validator and
`Aeyerji.EnemyBalance` automation: `Live` means declared imported-data coverage.
It is not a statement that the enemy can spawn or has its intended behavior.
The additive metadata is documentation; it does not activate enemies or filter
spawners automatically.

| Field/value | Meaning |
| --- | --- |
| `ImplementationStatus: UserReportedImplemented` | User reports the archetype implemented; known character path exists |
| `ImplementationStatus: CharacterAssetUnverified` | A character candidate exists, but implementation has not been verified |
| `ImplementationStatus: NativeFoundationPendingEditor` | Native behavior is supplied; editor character, presentation, wiring, and acceptance remain |
| `ImplementationStatus: DataTableOnly` | Attribute source and declared DataTable exist; character implementation is not established |
| `ImplementationStatus: Concept` | Source-only attribute proposal |
| `CharacterAsset` | Known asset object path, or `null`; not a generated class path |
| `EncounterStatus: NotRuntimeVerified` | This audit provides no current, reproducible runtime acceptance |

All entries start with `EncounterStatus: NotRuntimeVerified`, including the
user's implemented enemies. That records this audit's verification boundary,
not a claim that the existing enemies are broken. Promote readiness only with
evidence of the intended mechanic, correct data ownership, server/client
behavior, pooling cleanup, and a repeatable mixed-pack run.

The 15 `DataTableOnly` proposals are Assassin, Berserker, Brawler, Brute,
Cleaver, Duelist, Enforcer, Guardian, Pack Hunter, Reaver, Skirmisher, Tank,
Vanguard, Caster, and Sniper. Bombardier now has a native foundation and the
source-only Ranged template remains `Concept`.

## Next enemies and combat purpose

| Order | Enemy | Distinct decision it should create | First encounter target, pending playtest |
| --- | --- | --- | --- |
| 1 | [Bombardier](Bombardier.md) | Leave a fixed, readable ground warning before impact | One in an ordinary pack; at most two nearby active zones |
| 2 | Pack Hunter | Control a fragile flanking pack with movement and AOE | One pack of three to six; test actual flanking or leap behavior |
| 3 | Brute | Respect a slow, interruptible heavy attack | One alongside roughly eight to twelve ordinary enemies |

Pack sizes are design targets; Bombardier now enforces at most two active
warnings across the whole world, not two per pack. Keep the visible crowd
large; tune how many enemies can threaten the player at once and how their
mechanics combine. Fast-melee proposals should remain data-only until each
has a distinguishable behavior, silhouette, and counterplay. Likewise,
Bulwark, Tank, Guardian, Brawler, and Enforcer need separate encounter purposes
before all become production characters.

A proposed 24-enemy test after Pack Hunter is implemented is ten Grunts, four
Bulwarks, four Pack Hunters, three Archers, one Support, one Bombardier, and
one elite. For the first Bombardier test, substitute four additional Grunts
for the unimplemented Pack Hunters. This composition is a manual setup target;
the current survival JSON and standard harness fixtures are not changed by
this document. The opt-in `Bombardier12` fixture is provided by the native
implementation; see [Bombardier.md](Bombardier.md).

Measure visible population, combat-active population, active melee attackers,
ranged pressure, support effects, and simultaneous ground warnings separately.
Use the [combat telemetry contract](../../../Docs/CombatTestTelemetry.md) and
record actual damage and attack outcomes. An unoccupied blast is a miss; it is
not proof the player saw and deliberately avoided the warning.

## Source-to-runtime acceptance

1. Inspect the actual character Blueprint, its archetype library/data asset,
   startup abilities/effects, StateTree, animation, and presentation overrides.
2. Reimport changed JSON into the correct attribute DataTable, then save and
   cook the owning assets. Reimporting this roster contract itself is not
   required: its readiness fields are source documentation only.
3. Verify level-one ASC values with progression and equipment controlled. Do
   not infer live HP, attack rate, or damage from the generated matrix alone.
4. Check the intended behavior with a dedicated server and separate clients,
   then death, target loss, pool return/reacquisition, and player respawn.
5. Run the mixed group, inspect the report, and record the tested build,
   scenario, settings, and report path before marking encounter readiness.
