# Survival Round Reward Choices — Implementation Plan (C++ Only)

**Date:** 2026-07-02  
**Focus:** Server-authoritative between-round upgrade offers for survival mode.  
**Constraint:** C++ backend only. No fancy UI work. Dumb Blueprint hook is acceptable.

## Goal

After a survival round clears:

1. Generate 3 reward options from a small fixed starting set.
2. Replicate a shared offer to all eligible players.
3. Allow each player to select **exactly once** via a reliable server RPC.
4. Apply the chosen upgrade server-authoritatively (scaled for multiplayer).
5. After ~20s timeout, auto-apply a default for any non-selecting players.
6. Advance to the next round (harder via existing scaling).

This creates the core loop: `survive → choose upgrade → become stronger → harder next round`.

## Starting Upgrades (exactly these)

- `TreeMaxHP`
- `TreeRegen`
- `TreeReflectDamage`
- `PlayerXP`

## Current State (as of investigation)

The scaffolding is **largely already present** (and improved during this work):

- `FAeyerjiSurvivalUpgradeOption`, `FAeyerjiSurvivalUpgradeOfferState`, `EAeyerjiSurvivalUpgradeType` in `Public/AeyerjiObjectiveTypes.h`
- Replicated state on `AAeyerjiGameState`
- Offer generation (now defaults to **3** choices)
- Full server-authoritative selection flow with improved logging + validation
- Timeout + auto-apply default
- The 4 starting upgrades and apply logic
- BP hook for UI

**Progress made:**
- Default `UpgradeChoicesPerOffer` changed to **3**
- Hardened selection RPC + full lifecycle logging + early-finish for disconnect edges
- Improved PlayerXP path
- **Defense objective runtime stats ownership improved**: Reflect/Regen/HP upgrades now primarily live on `AAeyerjiSurvivalDefenseObjectiveActor` (with replicated props + authority-only apply methods). Director delegates where possible, with fallback for legacy.
- Tree apply functions cleaned (better comments, GAS notes, clamping, delegation)
- Edge handling: remaining-players early finish during offer if disconnects occur
- String table usage documented in code + plan (all keys already in GlobalStringTable.csv)

## Gaps / Work Needed (to match the exact plan)

1. **Choice count**: Default `UpgradeChoicesPerOffer = 4`. Plan explicitly says generate **3**.
2. **Defense objective runtime stats ownership**:
   - `SurvivalDefenseObjectiveReflectFraction` and `SurvivalDefenseObjectiveRegenPerSecond` live only as private members on LevelDirector.
   - Direct `SetNumericAttributeBase` for HP.
   - No clean public API or replicated summary of "current tree power" for clients / StateTree / BP.
3. **XP apply path hardening**:
   - Direct call on pawn's `UAeyerjiLevelingComponent`.
   - Verify it is server-authoritative, works for all clients' pawns, doesn't duplicate, and interacts correctly with existing leveling/rewards.
4. **Offer / round state robustness**:
   - Ensure `RequiredSelectionCount` / `SelectedCount` correctly drive "all players have chosen" early exit.
   - Handle edge cases: disconnected players, 1-player, late joiners, multiple rapid clears.
5. **Scaling**:
   - Current: `ScaledMagnitude = BaseMagnitude / RequiredSelectionCount`.
   - Confirm this matches desired "fair multiplayer" behavior.
6. **Triggering & flow**:
   - Offer must fire reliably after round clear reward spawn.
   - Must not fire on boss rounds if not intended, or handle per definition.
   - After offer (or auto) → `ScheduleNextSurvivalRound` / `StartNextSurvivalRound`.
7. **Blueprint surface** (minimal):
   - Keep or ensure `BP_ApplySurvivalUpgradeOffer(OfferState)` (or very close name) on the HUD widget.
   - No new UI implementation.
8. **Data / defaults**:
   - Ensure the 4 upgrades are the authoritative starting set (they already are in the default array in the .h).
   - Tune magnitudes if needed for balance (later).
9. **Cleanup / debug**:
   - Reset of upgrade accumulators on `EndRun()`.
   - Logging around offer lifecycle, selections, applications.
   - Validation that offer revision prevents stale selections.

## In-Scope for This Plan (C++ focus)

- Survival round manager/director (`AAeyerjiLevelDirector`)
- Replicated survival round state + offer state (`GameState`, types)
- PlayerController server RPC for selection
- Defense objective runtime stats (tree)
- XP apply path
- Round flow integration

**String / Localization Rule (per project reminder):**
- All user-facing text for upgrade options must use keys into `Source/Aeyerji/Data/Strings/GlobalStringTable.csv`.
- The `FAeyerjiSurvivalUpgradeOption` carries `DisplayKey` and `DescriptionKey` (FNames) — never raw FText or literals.
- The 4 core upgrades (TreeMaxHP, TreeRegen, TreeReflectDamage, PlayerXP) already have matching rows:
  - SurvivalUpgradeTreeMaxHP / Desc
  - SurvivalUpgradeTreeReflectDamage / Desc
  - SurvivalUpgradeTreeRegen / Desc
  - SurvivalUpgradePlayerXP / Desc
- Additional table entries exist: SurvivalUpgradeOfferTitle, SurvivalUpgradeTimeout.
- When editing defaults or adding new options in C++, ensure CSV entries are added/updated for localization.

**Broad localization pass completed (2026-07-04):**
- Centralized helper: `AeyerjiStringLibrary::GetGlobalStringTableText` (Public/GUI/AeyerjiStringLibrary.h).
- Refactored player-facing strings across Source (HUD labels/progress/messages, end-run screen, combat feedback, upgrade errors, action bar toasts, item/lane names, gold labels, stat previews, equipment tooltips, popups).
- All new strings added to `Data/Strings/GlobalStringTable.csv` with stable keys.
- IMPORTANT: After any CSV edit, reimport the string table asset `/Game/Localization/GlobalStringTable` inside the Unreal Editor for changes to take effect.
- Internal strings (logs, component names, data keys, tests) intentionally left hardcoded.

## Out of Scope (per plan)

- Fancy UI implementation (buttons, animations, etc.)
- New data tables or large balance passes
- Persistence of upgrades across full runs (unless later requested)
- StateTree integration for offers

## Detailed Implementation Steps

### Phase 1: Defaults & Generation
1. Change default `UpgradeChoicesPerOffer` to **3** in `UAeyerjiSurvivalMissionDefinition` (and the fallback initializer in `AeyerjiLevelDirector.h`).
2. Review `BuildSurvivalUpgradeOfferOptions`:
   - Ensure it always prefers the 4 starting types when no custom pool is supplied.
   - Guarantee at least the core 4 are considered.
3. Add/ensure the 4 options are the canonical starting set (already in header defaults).

### Phase 2: Replicated State
1. Review `FAeyerjiSurvivalUpgradeOfferState`:
   - Confirm all needed client fields (Options, Revision, Timeout, OfferEndServerTimeSeconds, SelectedCount, RequiredSelectionCount, RoundNumber, bActive).
2. If clients need visibility into cumulative tree power (recommended), consider:
   - Adding lightweight fields to `FAeyerjiSurvivalRoundState` (e.g. `CurrentTreeMaxHPBonus`, `CurrentTreeRegen`, `CurrentTreeReflect`, or a small struct).
   - Or publish via WorldStateSubsystem facts.
   - At minimum, ensure health is replicated through the objective ASC so health bars show benefit.
3. Make sure `PublishSurvivalRoundState` and offer state updates are called at the right moments.

### Phase 3: Selection RPC & Authority
1. Harden `Server_SelectSurvivalUpgrade_Implementation` in PlayerController:
   - Resolve LevelDirector robustly (already has fallback scan).
   - Add good logging.
2. Strengthen `SubmitSurvivalUpgradeChoice`:
   - Double-check "select once" using `SurvivalUpgradeSelectedPlayers`.
   - Validate OptionId belongs to current offer.
   - Update `ActiveSurvivalUpgradeOffer.SelectedCount` and republish immediately.
   - Early-finish when `SelectedCount >= RequiredSelectionCount`.
3. Ensure only one selection per player even across network hiccups (revision + set tracking).

### Phase 4: Apply Upgrades (Defense Objective + XP)
1. **TreeMaxHP**:
   - Current: direct base attribute set + clamp current HP.
   - Improve: Prefer applying a properly configured instant GameplayEffect when possible, or keep direct for simplicity but document.
   - Update cached ASC reference after application if needed.
2. **TreeRegen**:
   - Accumulate rate.
   - Ensure timer is correctly (re)started only on authority.
   - Tick applies to current HP (clamped to new max).
3. **TreeReflectDamage**:
   - Accumulate fraction.
   - `HandleSurvivalDefenseObjectiveDamageTaken` → `ApplySurvivalDefenseObjectiveReflect`.
   - Damage is reflected as physical using existing `UGE_DamagePhysical`.
4. **PlayerXP**:
   - Locate `UAeyerjiLevelingComponent` on the authoritative pawn for that `PlayerState`.
   - Call `AddXP(ScaledMagnitude)`.
   - Verify `AddXP` implementation handles replication, level-up events, stat refresh, etc.
   - Handle case where pawn may be dead / respawning (queue or apply to PlayerState?).

5. Improve ownership:
   - Consider moving cumulative stats (`ReflectFraction`, `RegenPerSecond`, maybe HP bonus tracking) into `AAeyerjiSurvivalDefenseObjectiveActor` or a new `UAeyerjiSurvivalDefenseStatsComponent`.
   - Director becomes the applicator + round orchestrator; objective owns the numbers.

### Phase 5: Flow & Lifecycle
1. Ensure after offer completion (all selected or timeout):
   - `FinishSurvivalUpgradeOffer` clears state, then `ScheduleNextSurvivalRound`.
2. Verify round clear path (normal + boss) always goes through the offer decision when `bEnableRoundUpgradeChoices`.
3. Handle `SurvivalMissionDefinition->bEnableRoundUpgradeChoices == false` → direct schedule.
4. Reset all accumulators cleanly in `EndRun()`, `ClearSurvivalDefenseObjective()`, etc.
5. Add server-time based client countdown support (already using `OfferEndServerTimeSeconds`).

### Phase 6: Blueprint / Dumb UI Hook
1. Confirm signature in `W_AeyerjiMissionHUD`:
   - `UFUNCTION(BlueprintImplementableEvent, Category=...) void BP_ApplySurvivalUpgradeOffer(const FAeyerjiSurvivalUpgradeOfferState& OfferState);`
   - Native `ApplySurvivalUpgradeOffer` calls the BP version and wires button → `Server_Select...`
2. Document that editor team can now bind buttons in the widget BP to call the server select.

### Phase 7: Polish & Verification
1. Add clear `UE_LOG` with category (e.g. `LogAeyerji`) for:
   - Offer generation
   - Player selection received + applied
   - Timeout auto-apply
   - Early finish when everyone chose
2. Add console command or debug function on LevelDirector/GameState to force an upgrade offer.
3. Handle 0 or 1 player cases gracefully (RequiredSelectionCount).
4. Write or update unit tests if test framework exists (`Private/Tests/` has several Aeyerji*Test files).
5. Update relevant audit docs (RunSystemAudit.md) with the new system.

## Files Likely to Touch

- `Source/Aeyerji/Public/AeyerjiObjectiveTypes.h`
- `Source/Aeyerji/Public/Director/AeyerjiLevelDirector.h`
- `Source/Aeyerji/Private/Director/AeyerjiLevelDirector.cpp`
- `Source/Aeyerji/Public/World/AeyerjiSurvivalDefenseObjectiveActor.h` + `.cpp` (recommended for stat ownership)
- `Source/Aeyerji/AeyerjiGameState.h` + `.cpp` (minor if adding fields)
- `Source/Aeyerji/AeyerjiPlayerController.h` + `.cpp`
- `Source/Aeyerji/Public/Progression/AeyerjiLevelingComponent.h` + `.cpp` (if XP path needs changes)
- `Source/Aeyerji/Source/Aeyerji/Docs/SurvivalRoundRewardChoicesPlan.md` (this file)
- Possibly string table entries (already have some)

## Definition of Done (per plan)

- [ ] On survival round clear → 3 options generated and replicated.
- [ ] Every eligible player can select exactly once via server RPC.
- [ ] Selection applies the correct upgrade type.
- [ ] Timeout (~20s) auto-picks default for remaining players.
- [ ] All players' choices (or defaults) applied before next round starts.
- [ ] Next round begins (with existing enemy scaling making it harder).
- [ ] Works in multiplayer (listen + dedicated).
- [ ] No client trust for application.
- [ ] `BP_ApplySurvivalUpgradeOffer(OfferState)` (or equivalent) exposed for Blueprint HUD.
- [ ] Clean C++ only changes; builds successfully.
- [ ] Basic logging + debug surface.

## Risks / Open Questions

- Should cumulative tree bonuses be saved in run results or world state? (Probably not for this phase.)
- Is dividing magnitude by player count the right scaling, or should some upgrades be full strength per player?
- Does the defense objective need to replicate its "upgraded" stats beyond health?
- Interaction with repair menu on the tree (existing feature).

---

Follow this plan for implementation. Prioritize making the 4 upgrades and the select → apply → continue loop solid in C++.

Next action after plan approval: pick the first few steps and implement + build check.
