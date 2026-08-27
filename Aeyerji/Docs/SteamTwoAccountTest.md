# Steam two-account multiplayer test

Use this test for Steam discovery, joining, invites, lobby travel, and recovery. Do not use multi-process PIE or two executables under one Steam account as proof that Steam works.

## What the current project supports

- `OnlineSubsystemSteam` is enabled and selected as the default online provider.
- UE 5.8's `SocketSubsystemSteamIP` plugin supplies the Steam P2P net driver. `GameNetDriver` points to `/Script/SocketSubsystemSteamIP.SteamNetDriver`; the ordinary IP driver is only its fallback.
- The frontend session code creates a Steam lobby when the active subsystem is `STEAM` and a Null/LAN session when it is `NULL`.
- Steam searches submit the Aeyerji game key, network build ID, and Waiting phase to the lobby service, then validate the returned metadata again locally. This avoids App ID 480 results consuming the search limit.
- Waiting sessions keep Unreal's `bAllowJoinInProgress` enabled because UE 5.8 maps that setting directly to Steam's lobby-joinable flag even before gameplay starts. Launching disables it before travel.
- Received Steam lobby invitations are validated and joined through the same build/phase checks as browser results.
- `OpenPartyInviteOverlay` opens the provider invite UI. A `Button_Invite` added to `W_MainMenu` binds to it automatically; Blueprint can also call it directly.
- Seamless gameplay travel retries the client's zone-ready acknowledgement until the replacement PlayerController owns the network connection.
- The command-line flag `-AeyerjiRequireSteam` makes host, search, join, and invite operations fail closed unless the active backend is Steam and local user zero is logged in with a valid ID. `-AeyerjiExpectedSteamAppId=<id>` additionally rejects the wrong App ID and implies the Steam requirement.
- `GameVersion` is configured for Steam game-server/lobby advertising. Keep it synchronized between builds that should be compatible.

At startup, verify a line like:

```text
[SessionBootstrap] Context=Initialize Backend=STEAM AppId=480 Sessions=1 Identity=1 Login=LoggedIn UserId=...
[SteamTestGate] Context=Initialize Required=1 Result=1 Backend=STEAM AppId=480 ExpectedAppId=480 Login=LoggedIn UserId=...
```

For every host/search operation, the decisive backend line is:

```text
[Session] Operation=Host Backend=STEAM LAN=0
```

`Backend=NULL LAN=1` means the test is using the offline LAN fallback, even if Steam DLL load messages appear earlier in the log.

## First prove the packaged build starts

Copy the entire archived `Windows` directory to the second system. A standalone copy of either executable is not a game installation: the cooked content, engine files, Steam DLL, config, and app-local runtime DLLs must remain in their staged relative paths.

The current archive contains:

- the top-level `Windows/Aeyerji.exe` bootstrap executable;
- the actual game at `Windows/Aeyerji/Binaries/Win64/Aeyerji.exe`;
- `Windows/Engine/Extras/Redist/en-us/vc_redist.x64.exe`;
- app-local MSVC, UCRT, and legacy DirectX runtime DLLs beside the actual game executable.

Run the top-level bootstrap executable first. The staged bootstrap does not guarantee that the loose `vc_redist.x64.exe` is installed automatically. On a clean x64 test machine, manually run the included installer once if startup fails or prerequisite installation was blocked, cancelled, or did not receive elevation. Do not download individual DLL files from third-party sites.

This project defaults to D3D11 SM5 and needs Direct3D feature level 11_0. For a VM, enable 3D acceleration, install the hypervisor guest graphics driver/tools, and confirm `dxdiag` lists feature level `11_0` or higher. A VM without a suitable virtual GPU can exit before Steam or game C++ initializes.

For a diagnostic launch:

```powershell
Set-Location <copied-build>\Windows
.\Aeyerji.exe -log -d3d11 -AeyerjiRequireSteam -AeyerjiExpectedSteamAppId=480 -LogCmds="LogSteamShared Verbose,LogOnline Verbose,LogOnlineSession Verbose,LogNet Verbose"
```

Then inspect:

```text
<copied-build>\Windows\Aeyerji\Saved\Logs\Aeyerji.log
```

If no log is created, launch `Aeyerji/Binaries/Win64/Aeyerji.exe -log -d3d11` from a console and inspect Windows Event Viewer -> Windows Logs -> Application. Also check whether Windows Properties offers an `Unblock` checkbox: the development executables are currently unsigned and may be stopped by SmartScreen or Mark-of-the-Web policy.

## Redistributables on an end user's PC

The project packaging settings already enable both `IncludePrerequisites` and `IncludeAppLocalPrerequisites`. That is enough for copied engineering builds when the full staged tree is retained.

For a real Steam depot, configure Steamworks App Admin -> Installation -> Redistributables and select the current Microsoft Visual C++ v14/2015-2022 redistributable and the DirectX June 2010 redistributable, then publish the installation changes. Steam owns the install detection and runs common redistributables only when necessary. Do not ask players to install Visual Studio or manually hunt for runtime DLLs.

For a non-Steam installer, ship the Unreal bootstrap/prerequisite payload or have the installer run the included x64 redistributable. The required architecture for this build is x64, not ARM64 or x86.

## Required Steam test environment

1. Use two Windows installations, normally two physical PCs. A VM is acceptable only if it has working D3D11 feature level 11_0 graphics, its own Steam client session, and working networking.
2. Sign into a different Steam account on each installation.
3. For a project-owned App ID, both accounts must have a license through developer access, a beta package, or Steam Playtest. App ID 480 is a shared engineering ID and is not a production entitlement test.
4. Run the same packaged Win64 Development build on both machines. Avoid PIE for this acceptance test.
5. Do not pass `-nosteam`. Close stray editor/game processes that may still own a session.
6. Run Steam and the game under the same Windows user/elevation context. Do not run one elevated and the other unelevated.
7. Allow the packaged Aeyerji executable through Windows Firewall on both machines.
8. Enable the Steam overlay for the account and game. Confirm that Shift+Tab opens it before treating the invite test as meaningful.

## App ID choice

For an early engineering smoke test, the existing `SteamDevAppId=480` can be used. App ID 480 is Valve's shared Spacewar development App ID, so it is noisy and is not proof that the project's eventual Steam entitlement/depot configuration works.

With App ID 480, accept lobby invitations while the invited copy of Aeyerji is already running. If it is closed, Steam owns the out-of-game launch and resolves App ID 480 to Spacewar's registered launch configuration rather than to the copied Aeyerji executable. In-game invitation acceptance still reaches Unreal's `GameLobbyJoinRequested_t` path and does not depend on public lobby discovery.

For a real project test, use either:

- the project's own Steam App ID with two developer/test accounts that have access; or
- a Steam Playtest child App ID distributed to both accounts.

Set `SteamDevAppId` in `Config/DefaultEngine.ini` to the chosen test App ID before packaging a Development build. UE 5.8 writes a temporary `steam_appid.txt` for non-Shipping startup beside the actual executable (`Aeyerji/Binaries/Win64`) and removes it during normal shutdown. That directory therefore must be writable for direct Development launches. If direct startup still reports that Steam cannot determine the App ID, place a manual `steam_appid.txt` containing only the numeric ID there. Never upload that development-only file to a Steam depot.

A Shipping target additionally needs the project-owned App ID supplied as `UE_PROJECT_STEAMSHIPPINGID` in the game target/build environment. Do not ship with ID 480. This cannot be finalized until the real App ID is known.

## Package and run

1. Package the `Aeyerji` Win64 Development target.
2. Ensure `/Game/Levels/L_MainMenu`, `/Game/Levels/L_PersistentRoot`, and `/Game/Levels/NeonMap` are cooked. They are currently listed in `DefaultGame.ini`.
3. Copy/install the entire archived `Windows` directory on both systems; do not mix files from different builds.
4. For a copied engineering build, start Steam first and launch with `-AeyerjiRequireSteam -AeyerjiExpectedSteamAppId=<expected id>` as described above.
5. For a real depot test, upload the whole staged `Windows` tree, configure the Windows 64-bit launch executable as the root `Aeyerji.exe`, and add `-AeyerjiRequireSteam -AeyerjiExpectedSteamAppId=<project id>` to that private test launch option. Set the build live on the branch used by both accounts. Both accounts must own the same package and install the same branch/depot build. Publish launch-option, package, and redistributable changes in Steamworks before testing them.
6. Confirm both logs contain `Backend=STEAM`, `AppId=<expected id>`, `Login=LoggedIn`, `SteamTestGate ... Result=1`, and different valid `UserId` values. Stop if either side reports `NULL`.
7. Confirm both builds report the same Aeyerji network build ID in their search/host diagnostics.

After joining, confirm `GameNetDriver` and `PendingNetDriver` identify `SteamNetDriver`/`SteamNetConnection`. A resolved `steam.<SteamID>:7777` address followed by `IpNetDriver`, DNS lookup, or `AddressResolutionFailed` means the Steam P2P driver plugin/class configuration is missing and the IP fallback was selected incorrectly.

Steam lobby search in UE 5.8 uses Steam's default lobby-distance filter. Browser discovery is therefore most reliable when the accounts are in the same or nearby Steam download region. For distant testers, validate the invitation path as well; an invite can reach a lobby that a regional public search does not list.

Steam public discovery and invitation delivery use the Steam backend, not LAN broadcast. A bridged VM adapter is acceptable and does not by itself make a lobby invisible. It can still affect the subsequent connection if the guest firewall, VPN, or network policy blocks Steam P2P/relay traffic.

## Profile identity checks

Steam identity is authoritative for profile ownership. Raw `SaveSlotOverride` values are now honored only by the NULL/offline development path so two authenticated accounts cannot collapse onto a shared test slot.

On both Steam machines, verify that:

- `[SessionBootstrap] UserId`, the lobby's expected owner, and the submitted profile owner identify that machine's Steam account;
- the two account owner keys differ;
- no Steam run reports `ExpectedOwner=Testxxxx`, `Owner=Testxxxx`, or writes `Profile_Testxxxx.sav` because of an old Blueprint save-slot override.

Old authenticated saves written under a shared override are intentionally not auto-migrated: their ownership is ambiguous. NULL/LAN development keeps the existing override behavior.

The save manager also uses the Steam Remote Storage user-cloud API. If cloud persistence is part of acceptance, enable Steam Cloud for the real App ID with nonzero per-user byte and file quotas and publish the setting. Auto-Cloud root mappings are not required for this API path. Cloud failure remains separate from session acceptance: if enumerate/read never calls back, profile resolution now logs `Reason=CloudTimeout` after 15 seconds and continues with local/default data rather than blocking Ready forever.

## Steam authentication hardening

This test proves that the game selected the Steam online subsystem, used Steam lobby discovery/invites, received distinct Steam IDs, and connected through the resolved Steam session address. It does not yet prove license/VAC authentication at the game-server packet-handler layer.

When the project has its own App ID and licensed test accounts, enable and test Unreal's Steam authentication handler deliberately:

```ini
[PacketHandlerComponents]
+Components=OnlineSubsystemSteam.SteamAuthComponentModuleInterface
```

That setting authenticates joining users and, by default, rejects authentication failures. Do not enable it casually for the shared App ID 480 smoke test; validate success, license failures, and reconnect behavior on the project-owned or Playtest App ID first.

## Acceptance pass

Run every connection direction because either account may become listen host:

1. PC A hosts; PC B refreshes, sees the party, and joins.
2. The host log names a valid Steam `SessionId` and owner. The browser log contains `Operation=SearchAccepted` with the same session/owner, not merely a nonzero result count.
3. Both profiles become verified without a Ready click being needed to refresh the roster, and their profile owner keys match their distinct Steam IDs.
4. Both players ready; the leader launches Campaign; both travel through `L_PersistentRoot` into the same gameplay zone.
5. Confirm the client logs `Dispatched zone-ready report through <current controller>` and the server receives its ready acknowledgement without a transition timeout.
6. Return to the menu together, leave/destroy the party, then reverse roles.
7. PC B hosts; PC A discovers and joins.
8. While hosting, use `Button_Invite` or call `OpenPartyInviteOverlay`, visibly open the Steam overlay, invite the other account, and accept it. Repeat in the opposite direction. `OpenInviteOverlay ... Result=1` means only that Steam accepted the UI request; the visible overlay and received invitation are the acceptance evidence.
9. Close the host during Waiting and during gameplay; verify the remaining client receives localized feedback and can return to the menu.

For the App ID 480 invitation pass, launch Aeyerji on both machines before hosting, keep the receiving copy open at the main menu, and make sure the accounts are Steam friends. The host's Designer button only needs the exact name `Button_Invite`; native C++ binds it automatically, so do not add a duplicate Blueprint click handler. After acceptance, retain these lines from the receiver:

```text
[Session] Operation=InviteAccepted Result=1 ... InviteValid=1
[Session] Operation=InviteMetadata Game=Aeyerji ... Phase=Waiting
[Session] Operation=InviteJoin Action=JoinDirectly
[Session] Operation=JoinComplete Result=0 Resolved=1 ...
```

If no `InviteAccepted` line appears, the problem is before Aeyerji's join logic: confirm the receiver was already running, the Steam overlay was enabled, the invitation was accepted inside Steam, and no lobby search was active at that exact moment. UE's Steam integration can ignore an accepted lobby invite while another Steam lobby search is still in progress.

The local Null/LAN editor-to-standalone test can advertise the editor listen host as port `0`, producing a one-way timeout. That path does not validate or invalidate Steam lobby discovery and should not block this two-machine Steam pass.

## Evidence to retain

Collect `Aeyerji/Saved/Logs/Aeyerji.log` from both installations and retain lines containing:

```text
[SessionBootstrap]
[SteamTestGate]
[Session]
[Lobby]
[LobbyClient]
[LobbyLaunch]
[RiftRun][Activity]
AAeyerjiGameState: Dispatched zone-ready report
```

For a successful launch, `[LobbyLaunch]` should name `L_PersistentRoot` and `Zone.Neon`, both clients should log the same frozen `[RiftRun][Activity]` selection, and no world-transition timeout should occur.
