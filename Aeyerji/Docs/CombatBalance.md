# Combat Balance

## Purpose

This document defines the first playable enemy-balance baseline for Aeyerji. The
benchmark is level 1, Normal difficulty, no items. It deliberately makes ordinary
enemies quick to clear while preserving a much larger durability gap for elites.

The machine-readable contract is
[`EnemyBalanceTargets.json`](../Data/EnemyBalanceTargets.json). The generated
[`EnemyBalanceMatrix.generated.md`](EnemyBalanceMatrix.generated.md) shows the
resolved source values, hit budgets, damage pressure, scaling projections, and
Unreal asset mappings.

The shared chase cadence, StateTree ownership, movement-speed contract, and
death-facing flow are documented in
[Enemy Combat Behavior](EnemyCombatBehavior.md).

## Canonical benchmark

- Player: level 1 Astral Guardian with 500 HP, 25 basic-attack damage, 1 armor,
  and 12.5% attack-damage variance.
- Difficulty: Normal world tier 167 with a global stat-budget multiplier of 1.
- Equipment: no items, affixes, temporary buffs, or external damage modifiers.
- Damage model: physical basic attacks with no critical hit, armor shred, or
  armor penetration in the deterministic hit-budget calculation.
- Elite package: 4x health, 1.35x damage, and 1.5x range, applied exactly once.

The hit target uses nominal damage. The generated matrix also reports the
best- and worst-roll bounds caused by damage variance.

## Enemy roles

| Role | Nominal player hits | Combat purpose |
|---|---:|---|
| Fodder | 1-2 | Dies rapidly; pressure comes from numbers or utility. |
| Standard | 2 | The default ordinary-enemy durability. |
| Specialist | 2-3 | Gains a small durability allowance for a distinct attack or movement pattern. |
| Durable | 3-4 | Lives longer than other trash, but remains far below an elite. |
| Glass elite | 8 | High offensive pressure with the low end of elite durability. |
| Balanced elite | 10-12 | A sustained priority target that outlives the surrounding trash. |
| Tank elite | 13-15 | A low-damage durability specialist, targeting roughly fourteen hits. |

Damage-pressure bands in the balance contract are review guidance. They are
reported as a warning rather than a validation failure because animation
windows, avoidance, abilities, pack composition, and AI behavior materially
change actual incoming damage.

## Movement baseline

Normal enemies use `WalkSpeed` while close to engagement or recovering and
`RunSpeed` for short chase sprints. The default cadence is a 1.5-second sprint,
a five-second recovery, and a new sprint only when the target is at least
250 cm outside engagement range. Movement remains role-specific and is not
multiplied by monster power.

The balance contract records a `TargetRunSpeed` for every enemy and a common
300 cm/s engagement walk. Validation requires the source values, generated
matrix, and live DataTables to stay in parity.

## Calculation rules

For armor at or below the soft cap:

```text
DamageReduction = Armor / (Armor + ArmorK)
EffectiveHealth = AppliedHP / (1 - DamageReduction)
```

The benchmark uses `ArmorK = 1000` and the same soft-cap/tail values as
`UExecCalc_DamagePhysical::ResolveArmorDamageReduction`. Current low-level armor
therefore changes hit counts only slightly; HP is the primary early-game
durability control.

For level projections, each scaling-table attribute uses the runtime formula:

```text
LevelDelta = max(EnemyLevel - 1, 0)
ScaledValue =
    BaseValue * (1 + PerLevelMultiplier * LevelDelta)
    + PerLevelAdd * LevelDelta
```

The attribute clamp and runtime tier multiplier are applied afterward. The
`BaseLevel`, `MaxLevelAdvantage`, `DifficultyExponent`,
`DifficultyMinMultiplier`, and `DifficultyMaxMultiplier` fields in
`EnemyScaling.json` are legacy compatibility data and are not active inputs to
the current runtime formula.

The level 10, 25, and 50 columns in the generated matrix are enemy-stat
projections, not hit-to-kill commitments. Player damage progression is authored
outside the source files consumed by the generator.

## Ownership and editor synchronization

- Attribute numbers are authored in `Source/Aeyerji/Data/EnemyAttributes`.
- Design targets, roles, source-to-asset mappings, and shared benchmark values
  are authored in `Source/Aeyerji/Data/EnemyBalanceTargets.json`.
- `Source/Aeyerji/Docs/EnemyBalanceMatrix.generated.md` is generated and must not
  be edited manually.
- Unreal DataTables, the archetype library, difficulty tuning, and spawner
  defaults must be updated through Unreal Editor or the Unreal 5.8 MCP server.
  Never edit a `.uasset` directly.
- `R_RangedAttrs.JSON` is intentionally marked `AuthoredOnly`: no matching
  `R_RangedAttrs` DataTable currently exists. The legacy
  `AttribsRangedMob` table is different data and is not its live counterpart.
- Archetype-library stat multipliers remain neutral for this pass. Elite
  promotion is owned by the spawner package, preventing base-table health and
  elite promotion from multiplying the tier gap twice.

Enemy initialization synchronizes current HP to HPMax. Every enemy source must
therefore keep `HP` and `HPMax` equal; otherwise the authored `HP` value is
discarded at runtime.

## Change workflow

1. Change the canonical attribute JSON and, when intent changes, update
   `EnemyBalanceTargets.json`.
2. Generate and validate the source documentation:

   ```powershell
   python Scripts/validate_enemy_balance.py --write
   python Scripts/validate_enemy_balance.py --check
   ```

3. Use Unreal Editor or `unreal_58` to reimport each changed JSON file into the
   mapped DataTable.
4. Confirm Normal world tier 167, the canonical elite package, archetype-library
   table mappings, and the absence of accidental per-spawner elite overrides.
5. Save the changed Unreal assets and rerun the source check.

The validator fails for unmapped enemy JSON files, duplicate contract mappings,
missing combat attributes, HP/HPMax divergence, source HP that differs from its
target, role hit-budget violations, invalid numeric data, or stale generated
documentation.

## Playtest acceptance

Test level 1 Normal without items on a dedicated server with separate clients:

- Fodder dies in one or two successful basic attacks.
- Grunts, Archers, and other Standard enemies take two.
- Bulwarks and other Durable enemies take three to four while dealing less
  pressure than offensive specialists.
- Archer Elites take about eight attacks.
- Grunt Elites take ten to twelve attacks.
- An elite Bulwark or Tank that retains its durable base takes roughly fourteen
  attacks and deals substantially less pressure than a glass elite.
- In a mixed pack, ordinary enemies normally die before the elite unless the
  player deliberately focuses the elite.
- The player can survive the opening combat loop before the first item drops.
- XP gain and time to the first item are recorded, but loot-system redesign is
  outside this balance pass.

Repeat the stat and encounter regression checks at levels 10, 25, and 50 and at
non-Normal difficulty settings. Gameplay state remains server-authoritative;
clients receive replicated GAS attributes and presentation state.
