# Enemy Combat Behavior

## Purpose

This document is the implementation and tuning contract for normal enemy
movement, the shared combat StateTree, and enemy death orientation. It complements
the numerical [Combat Balance](CombatBalance.md) contract.

The intended feel is:

- enemies close distance in short, readable bursts instead of maintaining one
  slow chase speed;
- enemies use their slower engagement pace near attack range;
- a five-second recovery gate prevents immediate repeated sprints;
- another sprint becomes available when the target creates meaningful distance;
- a dying enemy faces its credited killer before Blueprint spawns the detached
  death presentation.

## Live StateTree ownership

The live normal-enemy controller is
`/Game/Systems/EnemyAIController/EnemyAIAeyerji`. It runs
`/Game/AI/StateTree/STEnemyGeneral`.

The combat branch is `Engage Target`. Its relevant states are:

1. `Hard Chase`: sets `RunSpeed`, then runs `Move To Attack Range`.
2. `Attack`: executes the selected attack.
3. `Recover`: uses `Smooth Stop + Pause`.
4. `Pressure`: runs `Move To Attack Range` and chooses Attack or Hard Chase.

The native `Move To Attack Range` task is the shared cadence seam. Both Hard
Chase and Pressure pass through it, so cadence state survives the
Attack/Recover/Pressure loop without duplicating timers in the StateTree asset.
The reusable-looking assets under `/Game/AI/StateTree/SubTrees` are currently
unreferenced and are not part of this live path.

## Dynamic chase cadence

The authoritative enemy AI controller owns the cadence clock. The movement task
only supplies current target distance and engagement range.

Default behavior:

1. A far target permits a 1.5-second sprint.
2. Sprint speed comes from `AeyerjiAttributeSet.RunSpeed`.
3. A sprint ends when its duration expires or the enemy reaches the close
   approach band.
4. Close approach and sprint recovery use
   `AeyerjiAttributeSet.WalkSpeed`.
5. Recovery lasts five seconds.
6. After recovery, another sprint starts only if the target is at least 250 cm
   beyond the enemy's engagement range.
7. `MaxWalkSpeed` blends toward each pace rather than snapping between them.

Cadence state is reset on possession and pooled reuse, but not on every
StateTree state transition. This prevents Attack -> Pressure from granting a
fresh sprint immediately. Leaving the movement task restores the engagement
pace so RunSpeed cannot leak into attacks or recovery.

The StateTree graph remains the high-level decision owner. Native cadence owns
only the pace inside an active approach move.

## Crowd-facing stability

Detour Crowd remains responsible for navigation and collision avoidance, but
its instantaneous avoidance velocity does not directly own character yaw.
Blocked agents can alternate that velocity between two equally valid paths
every update, which previously made their models snap left-right.

`bStabilizeCrowdFacing` makes the controller's stable focus own yaw through
`bUseControllerDesiredRotation`. `StableFacingRotationRate` defaults to 540
degrees per second, so enemies still turn responsively without reflecting every
crowd-solver correction. Disable this only for a special enemy whose authored
locomotion must face its instantaneous movement direction.

The `Focus Target` task retains gameplay focus while the controller still owns
a valid target. A combat StateTree branch transition is not a target-loss event;
clearing focus at every transition would briefly make controller yaw follow the
Detour Crowd move direction and reintroduce left-right twitching. Target loss,
pool reuse, death, and crowd-control states still clear focus explicitly.

## Movement data

Enemy movement is data-driven:

- source rows: `Source/Aeyerji/Data/EnemyAttributes/*Attrs.JSON`;
- movement contract:
  [`EnemyBalanceTargets.json`](../Data/EnemyBalanceTargets.json);
- generated review table:
  [Enemy Balance Matrix](EnemyBalanceMatrix.generated.md);
- live rows: the matching `/Game/Enemy/Attributes/*` DataTables.

`WalkSpeed` is the close-engagement pace. `RunSpeed` is the sprint pace. The
contract requires RunSpeed to be greater than WalkSpeed for every enemy.
Movement speed is not multiplied by monster power.

The current RunSpeed baseline restores the clean role speeds that had been
stored at 70%:

- heavy enemies remain slow sprinters;
- normal pack enemies gain a clearly visible burst;
- assassins, pack hunters, skirmishers, and other mobility specialists retain a
  much faster role-specific burst.

Run `python Scripts/validate_enemy_balance.py --write` after changing movement
source values. The check fails if source speed, contract target, or generated
documentation diverges.

## Death facing

Lethal damage carries the credited killer through the attribute set into
`AAeyerjiCharacter::HandleOutOfHealth`.

For enemies, native code resolves a horizontal look-at rotation before
`BP_OnDeath` runs. It stops AI steering, clears movement/gameplay focus, applies
the yaw on the server, and sends the exact resolved yaw in the reliable death
multicast. Clients apply that yaw immediately before their local `BP_OnDeath`.

Sending the resolved rotation is intentional. Actor movement replication is not
ordered against the reliable death RPC, and the killer actor may no longer be a
safe client-side source when the presentation executes. This guarantees that
the Blueprint death geometry uses the same facing on the server, listen server,
and remote clients.

`EnemyParent.HandleAnimationForDeathAndRigidBodies` keeps XP attribution on its
authority-safe native call, then gates only the local visual path with
`IsDedicatedServer`. Standalone, listen-server, and remote-client instances hide
the living mesh and spawn the detached geometry locally; a dedicated server
does neither. The old Blueprint `bIsDead` branch and AI-controller lookup were
removed because the native death guard resets for pooled reuse and AI
controllers do not replicate to remote clients.

`bFaceKillerOnDeath` is enabled by default on the native enemy base. Disable it
only for a special enemy whose authored death sequence requires a fixed
orientation.

`DeathPresentationYawOffsetDegrees` corrects the model-forward axis before
Blueprint creates detached death geometry. It defaults to 90 degrees for the
shared enemy setup. Override it per enemy Blueprint when a mesh uses a
different import orientation; do not add a second competing rotation in the
death graph.

Floating enemy status bars are hidden immediately when the replicated death
state is applied. This visibility step is separate from component destruction:
ordinary enemies may destroy the component during cleanup, while pooled
enemies retain the hidden component and restore it only after HP and other
attributes have reset during checkout. Corpse lifetime therefore cannot leave
a zero-health bar visible.

## Multiplayer ownership

- StateTree execution, target selection, cadence transitions, and speed changes
  run on authority.
- Character movement replicates the resulting motion to clients.
- Clients do not run a competing cadence timer.
- Death facing is resolved once on authority and carried in the reliable death
  presentation RPC.
- Pool checkout resets cadence and death guards before the StateTree resumes.

## Playtest checklist

At level 1, Normal difficulty, without items:

1. Spawn a Grunt at least 1000 cm from the player. Confirm it visibly sprints,
   then slows near engagement.
2. Kite it back out during the next five seconds. Confirm it approaches at
   WalkSpeed instead of immediately sprinting again.
3. Keep distance until the recovery expires. Confirm a second sprint occurs.
4. Repeat with Bulwark or Tank and confirm the burst remains heavier/slower.
5. Repeat with Assassin, Pack Hunter, or Skirmisher and confirm the burst is
   materially faster.
6. Test a ranged enemy both inside and outside its attack range.
7. Crowd several enemies into a narrow approach. Confirm avoidance still
   separates them while their models turn smoothly rather than snapping
   left-right.
8. Kill an approaching enemy from several directions. Confirm the death
   presentation faces the killer on standalone, listen server, and a remote
   client, and confirm its floating status bar disappears immediately.
9. Reuse a pooled enemy and confirm it receives a clean initial cadence and
   death-facing state with a restored full-health status bar.

## Remaining enemy creation

The source balance contract currently covers 23 enemy stat sets, but a live
DataTable does not prove that a production-ready enemy character Blueprint,
mesh, abilities, animation set, spawn entry, and encounter weight all exist.
Creating the remaining enemies should be a separate content pass. Use the
source/live map in the generated balance matrix as the starting inventory, then
verify each candidate through the controller, StateTree, archetype library,
spawner, and animation/death presentation path.
