# Frontend Assembly Checklist

This guide assembles the existing `/Game/GUI/MainMenu/W_MainMenu` onto the native frontend foundation. Complete it in a copy/changelist where the Blueprint can be saved and tested. Do not create a replacement main-menu widget.

The checklist is intentionally ordered. Finish and test one section before moving to the next. A "page" below means a panel inside one `WidgetSwitcher`; it does not mean a map, level, or separate Widget Blueprint.

## Intended Designer hierarchy

Names are suggestions, but using them will make the Event Graph instructions easier to follow.

```text
W_MainMenu
`-- Root (existing Canvas Panel or Overlay)
    |-- Existing background/art
    |-- MainLayout
    |   |-- ProfileStrip                 (visible on every page)
    |   `-- PageSwitcher                 (WidgetSwitcher)
    |       |-- Page_Landing             (panel; switcher child index 0)
    |       |   `-- LandingButtons
    |       |       |-- Button_Play
    |       |       |-- Button_HostPublicParty
    |       |       |-- Button_PartyBrowser
    |       |       |-- Button_Inventory (disabled placeholder)
    |       |       |-- Button_Loadout   (disabled placeholder)
    |       |       |-- Button_Character (disabled placeholder)
    |       |       `-- Button_Exit
    |       |-- Page_PartyBrowser        (panel; switcher child index 1)
    |       |   |-- BrowserResults
    |       |   |-- Button_Refresh
    |       |   `-- Button_BrowserBack
    |       `-- Page_PartyLobby          (panel; switcher child index 2)
    |           |-- LobbyRoster
    |           |-- LobbyActivityControls
    |           |-- Button_Ready
    |           |-- Button_Launch
    |           `-- Button_Leave
    `-- FeedbackOverlay                  (normally hidden; above all pages)
```

`ProfileStrip` must be outside `PageSwitcher`; otherwise it will disappear when the active page changes. The three disabled placeholder buttons belong inside `Page_Landing` because they are landing-page navigation, not persistent status controls.

## How to use the global string table in UMG

For a `TextBlock`, open its `Text` property in the Details panel and change the text source to a String Table entry. Select `/Game/Localization/GlobalStringTable.GlobalStringTable` and then select the key listed in this guide. Do not type the English label as ordinary text; the table entry is the localized source.

The three disabled landing buttons use:

- Inventory: `Frontend_Nav_Inventory`
- Loadout: `Frontend_Nav_Loadout`
- Character: `Frontend_Nav_Character`

Select the containing `Button` (not only its child `TextBlock`) and untick `Is Enabled`. Leave the buttons visible and do not add `OnClicked` events yet. Their dimmed appearance tells the player that these features are planned but unavailable.

## Before editing the Blueprint

- [x] Compile `AeyerjiEditor`, open the project, and confirm `W_AeyerjiFrontendShell` appears as a valid parent class.
  - Open `W_MainMenu`, choose **File > Reparent Blueprint**, and search for `W_AeyerjiFrontendShell`.
  - If the class is absent, close the editor and rebuild before changing the Blueprint.
- [x] Reimport `/Game/Localization/GlobalStringTable.GlobalStringTable` from `Source/Aeyerji/Data/Strings/GlobalStringTable.csv`.
  - Open the String Table asset and use its reimport action. Save the asset after the new `Frontend_...` keys appear.
- [x] Open `W_MainMenu`, take screenshots of its current Designer hierarchy and Event Graph, and note any portal animation event names.
  - These screenshots are the recovery reference while moving the existing widgets into `Page_Landing`.
- [x] Reparent `W_MainMenu` from `UserWidget` to `W_AeyerjiFrontendShell` and compile immediately.
  - Fix any compile errors before rearranging the Designer. Reparenting is what makes the native command functions and presentation events available in this Blueprint.
- [x] Confirm the existing PlayerController `MainMenuWidgetClass` still points to `W_MainMenu`.
  - Reparenting should not change that class reference, but verify it before testing.

## Remove legacy launch paths

- [x] Remove the direct `StartGameplaySession` call from the Play button graph.
- [x] Remove the old `DifficultySlider` launch-selection path; Excursion tier is now an integer selected by the authoritative lobby.
- [x] Remove direct Campaign/Excursion travel calls from `CampaignButton` and `ExcursionButton`.
- [x] Remove any client-authored tier, ready, roster, or travel variables that duplicate the native snapshots.
- [x] Keep the widgets until their replacements are wired so the menu remains easy to compare while assembling.
  - At this point, none of the menu buttons should call `Open Level`, `Server Travel`, or the legacy `StartGameplaySession` node.

## Page shell

- [x] Add one UMG `WidgetSwitcher` with Landing, Party Browser, and Party Lobby pages.
  - In the Designer Palette, drag a `WidgetSwitcher` into the main content area and name it `PageSwitcher`.
  - Add three panel widgets as direct children of the switcher. An `Overlay`, `Canvas Panel`, or `Vertical Box` is fine. Name the children `Page_Landing`, `Page_PartyBrowser`, and `Page_PartyLobby`.
  - Move the existing main-menu controls into `Page_Landing`; do not rebuild working art and layout from scratch.
  - A switcher displays exactly one direct child at a time. The other two pages still exist but are hidden by the switcher.
- [x] Make Landing the initial page and keep page changes presentation-only.
  - Select `PageSwitcher` and set `Active Widget Index` to `0` in the Designer.
  - Page buttons may call `Set Active Widget` or `Set Active Widget Index` on `PageSwitcher`.
  - Changing pages must never start travel, change activity selection, or invent lobby state by itself.
- [x] Add disabled Inventory, Loadout, and Character buttons labelled from the global string table.
  - Put all three buttons inside `Page_Landing`, alongside Play, Party Browser, and Exit.
  - Name them `Button_Inventory`, `Button_Loadout`, and `Button_Character`.
  - Give each button a child `TextBlock` sourced from the three `Frontend_Nav_...` keys listed above.
  - Select each `Button` and untick `Is Enabled`. Do not create click events for them.
- [x] Do not add Stash navigation; no persistent stash backend exists yet.
  - This is a rule, not an editor action. There should be no Stash button on any of the three pages.
- [x] Keep `WBP_PlayerStatusHUD`, `WBP_InventoryBag`, `BP_ActionBar`, and `WBP_CharacterStats` out of the frontend because they require gameplay pawn/ASC state.
  - Do not drag those widgets into `W_MainMenu`. Inventory, Loadout, and Character are only disabled text buttons in this first frontend slice.

### Page-shell test

- [x] Compile and save `W_MainMenu`.
- [x] In the Designer, change `Active Widget Index` between 0, 1, and 2 and confirm each page occupies the same main content area.
- [x] Restore the default index to 0 before continuing.
- [x] Run PIE and confirm the Landing page appears and the three placeholder buttons are visible but cannot be clicked.

## Persistent profile strip

- [x] Add level, XP current/max, XP bar, and gold controls outside the page switcher so they remain visible on all three pages.
  - Create a horizontal or overlay panel named `ProfileStrip` above `PageSwitcher` in `MainLayout`.
  - Add `Text_Level`, `Text_XP`, `Progress_XP`, and `Text_Gold`.
  - Keep this strip limited to persistent profile data. Runtime HP, mana, and GAS resources remain in the gameplay HUD.
- [x] Project `Apply Frontend Snapshot` natively.
  - The shell sets level, current/max XP, XP percent with a zero-maximum guard, and gold directly from the supplied snapshot.
  - Do not recreate this work in the event graph or read profile data from a pawn, PlayerState, or SaveGame.
- [x] Present profile readiness natively.
  - The shell disables `PageSwitcher` while `ProfileState` is not Ready, shows `Frontend_ProfileResolving` in `Text_ProfileStatus`, and re-enables the page content on the next Ready snapshot.
- [x] Present operation state natively.
  - The shell updates `Text_Operation` from localized creating/searching/joining/leaving/launching keys and collapses it for Idle, profile resolution, and failure. Failure text still arrives through `Show Frontend Feedback`.
- [x] Do not cache or mutate a SaveGame object in the widget.
  - The `Snapshot` struct is the only profile input the menu needs. Do not add a SaveGame variable or call load/save functions from `W_MainMenu`.

### Profile-strip test

- [x] Compile and run PIE, then confirm level, XP, and gold match the resolved local profile.
- [x] Temporarily switch among all three pages and confirm the strip never disappears.

## Native shell handoff

`W_AeyerjiFrontendShell` now owns the deterministic frontend behavior. It binds the named buttons below, projects frontend and lobby snapshots into the named controls, gates controls from the replicated snapshot, and prevents duplicate launch-countdown presentation. Do not recreate this logic in `W_MainMenu`'s Event Graph.

The native bindings are optional so a missing widget fails safely, but these exact names activate the corresponding behavior:

- Pages: `Page_Landing`, `Page_PartyBrowser`, `Page_PartyLobby`, and `PageSwitcher`.
- Persistent data: `Text_Level`, `Text_XP`, `Progress_XP`, `Text_Gold`, `Text_ProfileStatus`, and `Text_Operation`.
- Landing/browser actions: `Button_Play`, `Button_HostPublicParty`, `Button_PartyBrowser`, `Button_Refresh`, and `Button_BrowserBack`.
- Lobby actions: `Button_Ready`, `Button_Campaign` or legacy `CampaignButton`, `Button_Excursion` or legacy `ExcursionButton`, `Button_TierPrevious`, `Button_TierNext`, `Button_Launch`, and `Button_Leave`.
- Four roster slots: `MemberSlot_1` through `MemberSlot_4`, with each slot containing `Text_Name_N`, `Text_Level_N`, `Text_HighestTier_N`, `Text_ProfileState_N`, `Text_ReadyState_N`, and `Text_LeaderBadge_N`.

The shell calls the existing presentation events after completing its native projection. Keep `Apply Session Results`, `Show Frontend Feedback`, and `Present Launch Countdown` for the visual work that only the widget can own. Remove manual click/event-graph handlers for the named native actions to avoid duplicate requests.

New localized keys were added to `GlobalStringTable.csv`; reimport `/Game/Localization/GlobalStringTable.GlobalStringTable` before testing.

## Landing page

- [x] Wire Play to open the Party Lobby page for the current local/listen party.
  - Native `Button_Play` handling opens `Page_PartyLobby`; it never starts gameplay travel.
- [x] Bind Host Public Party to `HostPublicParty` natively.
  - Native `Button_HostPublicParty` handling requests the default party name when no optional name field exists, then opens staging while the request progresses.
  - If an `EditableTextBox` is later added for party naming, keep its conversion and submission as the only Blueprint addition for this action; do not add another button click handler.
  - The request's Boolean return means it was accepted, not that Steam or Null/LAN hosting has completed. Failure presentation still arrives through `Show Frontend Feedback`.
- [x] Bind Party Browser navigation and initial search natively.
  - Label `Button_PartyBrowser` with `Frontend_Nav_Browser` in the Designer.
  - Native handling opens `Page_PartyBrowser` and calls `SearchPublicParties` once; do not run search from Tick.
- [x] Keep Exit Game on its existing safe quit path.
  - Preserve the working quit graph. Exit is the only Landing action that should not use the frontend shell.

## Party Browser

The browser page needs a scrollable list of rows. The easiest maintainable setup is a small child Widget Blueprint such as `W_PartyBrowserResultRow`. It is a presentation widget only; it does not talk to the online subsystem directly.

- [x] Finish the browser layout.
  - Inside `Page_PartyBrowser`, add a `ScrollBox` named `BrowserResults`, a Refresh button, and a Back button.
  - Label Refresh with `Frontend_RefreshParties`. Reuse an existing localized Back label if one exists in the project.
- [x] Create `W_PartyBrowserResultRow` if no suitable result-row widget already exists.
  - Add text fields for party name, host, player count/capacity, ping, activity, and Excursion tier, plus a Join button.
  - Add an integer variable `ResultId` and a Boolean variable `bJoinable`; neither should be saved.
  - Add an Event Dispatcher such as `OnJoinRequested` with one integer parameter named `ResultId`.
  - The row's Join button broadcasts `OnJoinRequested(ResultId)`. The parent `W_MainMenu` performs the actual shell call.
- [x] Bind Refresh and Back natively.
  - `Button_Refresh` calls `SearchPublicParties`; `Button_BrowserBack` returns to Landing without a network request.
  - Do not add matching Blueprint click handlers or run search from Tick.
- [x] Implement `Apply Session Results` by clearing and rebuilding the result list.
  - In `W_MainMenu`, add `Event Apply Session Results`.
  - Call `BrowserResults.ClearChildren`, then use `ForEachLoop` on `Results`.
  - For each item, create `W_PartyBrowserResultRow`, copy the supplied display fields and `ResultId`, bind its `OnJoinRequested` dispatcher, and add it to `BrowserResults`.
- [x] Display party name, host, player count/capacity, ping, activity, tier, and joinability.
  - Read all values by breaking `AeyerjiSessionSearchResultView`.
  - Show the tier only when `ActivityType` is Excursion; Standard Rift uses tier 0.
- [x] Store only each row's supplied `ResultId`; do not retain the result after another refresh.
  - Rebuilding the rows on every `Apply Session Results` event naturally discards expired IDs.
  - Never use array index as a replacement for the supplied ID.
- [x] Disable Join for rows where `bJoinable` is false.
  - Set the row's Join button `Is Enabled` directly from `bJoinable`.
- [x] Wire Join to `JoinPublicParty(ResultId)`.
  - In `W_MainMenu`, handle the row dispatcher and pass its integer directly to `Join Public Party`.
  - Do not call Client Travel or Open Level; successful session join owns connection travel.
- [x] Bind Back to Landing without issuing a network operation.
  - Native `Button_BrowserBack` handling selects `Page_Landing`; verify it in PIE.

## Party Lobby

The lobby page presents `FAeyerjiLobbySnapshot`. The server owns every value in that snapshot; Blueprint only displays it and sends requests through the inherited wrapper functions.

- [ ] Finish the four fixed roster-slot layout.
  - Keep `MemberSlot_1` through `MemberSlot_4` and their six named `Text_*_N` children from the native-shell handoff section. Arrange and style them in the Designer.
  - Do not add a roster loop or visibility graph. Native code collapses unused slots, fills supplied member values by array position, and shows each populated slot.
- [x] Project lobby snapshots and local-control state natively.
  - The shell identifies the local member by `PlayerState.PlayerId` for presentation only, updates Ready/leader/profile labels, and gates Ready, activity, tier, and Launch controls from the replicated snapshot.
  - Authority remains server-side; do not duplicate leader or launch validation in Blueprint.
- [x] Bind Ready/Unready natively.
  - Native handling proposes the inverse of the last replicated local ready value and waits for the next snapshot before changing visuals.
  - Set the `Text_Ready` child up in the Designer; its localized Ready/Unready text is provided by native code.
- [x] Bind activity selection natively.
  - `Button_Campaign` and legacy `CampaignButton` request `StandardRift`; `Button_Excursion` and legacy `ExcursionButton` request `Excursion`.
  - Keep the visible labels sourced from the existing `Campaign` and `Excursion` string-table keys. Remove any old travel nodes.
- [ ] Finish the tier-control layout.
  - Keep `Button_TierPrevious`, `Text_SelectedTier`, and `Button_TierNext` in `LobbyActivityControls` and style them in the Designer.
  - Native code displays the replicated tier, collapses the controls for Standard Rift, clamps proposals to `1..CommonExcursionTierCap`, and waits for snapshot confirmation before display changes.
- [x] Bind leader-only Launch and Leave natively.
  - `Button_Launch` only calls `LaunchPartyActivity`; its enabled state is native presentation feedback derived from leader, verified/ready members, and Waiting phase.
  - `Button_Leave` calls `LeaveCurrentParty`. The authoritative destroy/travel flow returns the rebuilt widget to its Landing default; Blueprint must not travel manually.

## Feedback and launch presentation

- [ ] Implement `Show Frontend Feedback` using the supplied localized `FText` and an existing non-modal error panel/toast.
  - Add `Event Show Frontend Feedback` in `W_MainMenu`.
  - Set a text field directly from the supplied `Message`; do not rebuild or translate the failure text in Blueprint.
  - Show `FeedbackOverlay` and hide it after a short UI animation or timer. The failure enum may choose styling, but the supplied message is the displayed content.
- [ ] Implement `Present Launch Countdown` using `LaunchAtServerTimeSeconds` and synchronized GameState server time.
  - Add `Event Present Launch Countdown`.
  - Obtain synchronized server time from GameState and calculate `max(0, LaunchAtServerTimeSeconds - CurrentServerTime)` for display/animation timing.
  - Do not use the client's wall clock.
- [ ] Play the existing portal animation once when the phase enters Launching.
  - Trigger the existing portal animation from `Present Launch Countdown`, not from the Launch button click.
- [x] Prevent repeated launch presentation in the native shell.
  - The shell invokes `Present Launch Countdown` only when the launch timestamp changes and resets its guard when the lobby leaves Launching. Do not add a Blueprint duplicate guard.
- [x] Let native authority perform travel when the countdown ends.
  - The animation may fade, pulse, or close panels. It must not call `Open Level`, `Client Travel`, `Server Travel`, or `StartGameplaySession`.

## Return-from-run refresh

- [ ] Complete a run and return through the existing authoritative Return to Menu path.
- [ ] Confirm connected members return together and the party page rebuilds.
  - `W_MainMenu` will receive fresh frontend and lobby snapshot events after travel. It should not need Tick or a manual SaveGame load.
- [ ] Confirm every member is unready after return.
- [ ] Confirm the session is joinable again.
- [ ] Confirm new gold, level, XP, and Excursion progression appear without Blueprint polling or reconstructing the SaveGame.

## Validation matrix

Run the small PIE checks while assembling; leave Steam testing until the widget logic works with Null/LAN.

- [ ] Solo/offline: profile resolves, ready works, Campaign launches through lobby validation, and return refreshes the profile strip.
- [ ] Two-player Null/LAN: host, search, join, verified roster, manual ready, leader-only selection, launch, and return together.
- [ ] Four-player Null/LAN: full roster works and a fifth connection is rejected.
- [ ] Disconnect: readiness clears, the lowest stable PlayerId becomes leader, and the remaining roster updates.
- [ ] Shared selection: any activity or tier change clears all readiness.
- [ ] Profile revision: a changed member profile clears that member's readiness.
- [ ] Excursion validation: undefined, locked, and minimum-level tiers cannot launch.
- [ ] Join-in-progress: browser filtering and server rejection block Launching/InGameplay parties.
- [ ] Steam (separate accounts): discovery excludes unrelated App ID 480 lobbies, direct join works, invite acceptance works, and leave/destroy returns offline.
- [ ] Steam recovery: host loss, network failure, and travel failure show localized feedback and allow returning to the menu.
- [ ] Exact handoff: the selected activity/tier logged in `[LobbyLaunch]` matches the frozen gameplay `[RiftRun][Activity]` snapshot.

## Recommended stopping points

Native routing, snapshot projection, control gating, and launch dedupe are already supplied by `W_AeyerjiFrontendShell`. The remaining safe editor milestones are:

1. Reimport the string table, compile `W_MainMenu`, and remove duplicate native-button graph handlers.
2. Finish the Landing/Browser/Lobby layout and apply string-table labels.
3. Finish `W_PartyBrowserResultRow` plus `Apply Session Results` and Join dispatching.
4. Style the four native-filled roster slots and tier controls.
5. Implement feedback toast and launch-countdown/portal visuals.
6. Run the multiplayer validation matrix.

Compile and save `W_MainMenu` at each stopping point. Fix Blueprint compile errors before moving on so a later failure has a small search area.
