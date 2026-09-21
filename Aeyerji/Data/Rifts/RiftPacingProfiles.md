# Initial Standard Rift pacing profiles

These are experimental whole-run presets using existing native asset properties.
They are not automatically selected by level or switched after the first region.
Start with `Normal_L1_Core`; use `Normal_L1_Intro` if the opening still overwhelms.
`Normal_Crowd_Comparison` differs from Core only in its awake cap, to test whether
the improved composition supports the full 48-enemy crowd.

| Profile | Total budget | Awake cap | Ordinary ranged probability | Elite probability | Elite pool |
| --- | ---: | ---: | ---: | ---: | --- |
| Normal_L1_Intro | 120 | 32 | 20% | 0% | Empty |
| Normal_L1_Core | 120 | 40 | 20% | 4% | GruntElite |
| Normal_Crowd_Comparison | 120 | 48 | 20% | 4% | GruntElite |

Ranged includes Archer and Support. Repeated ordinary pool entries encode weights:
the current Rift planner samples array entries uniformly and retains duplicates.
Probabilities are expectations, not guaranteed region counts or concurrent caps.
The 4% assumes region Elite Chance Bonus = 0 and the Standard multiplier = 1.
An empty elite pool prevents elite selection even when a region adds a bonus.
No elite Archer is included. All existing enemy stats remain in effect.

## Engagement distances come from the original Director

Every import copies these five properties from
`/Game/Systems/EncounterDirectorDefinition` into every generated Director:

- Rift Region Staging Distance
- Rift Enemy Wake Distance
- Rift Enemy Sleep Distance
- Rift Pressure Radius (the population-counting radius, not AI sight range)
- Rift Minimum Spawn Distance From Players

The importer rejects profile overrides for these properties. Future profiles use
the same original spatial tuning. This is a copy at import time, not live asset
inheritance: after changing the original, rerun the importer. Other unlisted
properties are copied when a profile asset is first created, not refreshed on
subsequent imports. Enemy perception/aggro settings are not changed by the script.

If you already imported Core, rerun `ImportRiftPacingProfiles.py` now. It updates
the existing generated Directors in place, restoring these distances from the
original asset and logging the copied values in cm. Keep your current Core
assignment; no map, ZoneRun or enemy Blueprint edits are needed for this fix.
The initial profile version overrode Core's staging/wake/sleep values to
5000/6000/8000 cm; simply deleting JSON fields would not repair existing assets,
which is why the importer explicitly refreshes them.

Awake caps still apply independently: at Core's 40-enemy cap, farther enemies
can remain asleep even inside the restored wake distance. Compare the 48-cap
Crowd preset if engagement is correct below the cap but distant enemies stop
joining when the crowd fills. Matching wake distance does not bypass AI sight
checks, reveal locks or the awake cap.

## Import once, rerun after JSON edits

1. Enable Unreal's **Python Editor Script Plugin** if necessary and restart Editor.
2. Use **Execute Python Script** from the Editor File menu and select
   `Source/Aeyerji/Data/Rifts/ImportRiftPacingProfiles.py`.
3. Confirm `[RiftPacingImport] Complete` in Output Log. The script creates six
   native DataAssets under `/Game/Systems/Rifts/Pacing`: one `ED_` Director and
   one `SG_` spawn group for each profile above. It duplicates existing templates
   so unrelated presentation/performance settings are retained.
4. For later edits to the JSON, rerun this script. This is a custom asset import,
   not a DataTable CSV import or the Content Browser's Reimport command. Existing
   generated assets are checked out and updated only if they carry this script's
   ownership metadata. Review/add the generated assets in Perforce as needed.

The script preflights templates, enemy classes, property names and destination
ownership. An Editor/save failure can still leave some generated assets created;
check the log and rerun after resolving the failure. Original assets are untouched.

## Activate the profile (no Blueprint event graphs)

1. Open `/Game/Systems/NeonMapZoneRun` and assign **Encounter Director Definition**
   to `/Game/Systems/Rifts/Pacing/ED_Normal_L1_Core`.
2. In the same ZoneRun, set **Standard Rift Enemy Budget** = 120,
   **Standard Rift Progress Target Points** = 100, and **Standard Rift Activation
   Distance** = 2500 cm. These values are also listed in the JSON's
   `standard_zone_settings`; the importer deliberately leaves this shared asset
   for your explicit Editor assignment.
3. Open `/Game/Levels/NeonMap`. Inspect eligible ordinary SpawnRegions. A nonempty
   **Rift Encounter Group** overrides the Director's fallback pool. For regions
   intended to use this preset, assign `SG_Normal_L1_Core` or clear the override
   to use the Director's fallback. Preserve deliberately special encounters.
   Set **Elite Chance Bonus** = 0 on the regions being tested. Optionally disable
   **Allow Elites** on the first region for an elite-free opening.
4. Save ZoneRun and the map (including external actor packages if applicable).
   Launch Campaign/Standard Rift with a level-1 character. The profile is shared
   wherever that ZoneRun/Director is used, including Excursions if they use it;
   this setup does not provide mode-specific automatic selection.

Affected existing assets are `NeonMapZoneRun` and any SpawnRegion overrides saved
in `NeonMap`. The original `EncounterDirectorDefinition` and `Map1SpawnGroupDef`
are templates only. No enemy attribute, player attribute, Blueprint ability,
`RiftTierTable`, or `GreaterRiftTiers.csv` reimport is needed. Include new assets
and saved references in the next cook/package.

## Verify the first run

- Confirm the activity log says StandardRift, ActivityLevel=1, EnemyBudget=120,
  ActivationDistance=2500. Region encounter-plan logs should name the selected
  SG asset (or use the Director fallback when the region group is empty).
- Use the existing production combat recorder described in
  `../../Docs/CombatBalanceTesting.md`; label captures with the selected profile
  name, seed and player loadout. These assets do not add automatic profile ID
  telemetry. Existing samples/damage records are needed to assess actual pressure.
- Compare three 60-90 second openings, resetting to the same level/loadout. Keep
  seed and route the same for Core versus Crowd where practical. Check burst
  damage, ability to recover, ranged clustering and actual awake population.
- Complete one full run to verify boss progression. At budget 120, ordinary
  progress 1 and target 100, even an all-ordinary plan meets the 120% reserve gate.
- Verify two-client behavior before release. These are existing server-owned
  controls, but new asset values still require playtesting and packaged validation.

The starting values are hypotheses, not a balance certification. Slower reveal
batches should soften arrival spikes while preserving the full planned population
and the original Director's spatial tuning. There is no maximum staged-region count, ranged
concurrency quota, or automatic opening phase in this data-only implementation.
