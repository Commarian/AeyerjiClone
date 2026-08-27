// ===============================
// File: AeyerjiPlayerController.h
// ===============================
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "AeyerjiGameState.h"
#include "Avoidance/AeyerjiAvoidanceProfile.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Abilities/AeyerjiTargetingManager.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "MouseNavBlueprintLibrary.h"
#include "AeyerjiPlayerController.generated.h"
class APawn;
class UAbilitySystemComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class AAeyerjiEncounterDirector;
class AAeyerjiLevelDirector;
class AAeyerjiGameState;
class AAeyerjiPlayerState;
class UW_EndRunScreen;
class UW_AeyerjiMissionHUD;
class UW_AeyerjiMinimap;
class UUserWidget;
class UAeyerjiCameraOcclusionFadeComponent;
class UAeyerjiViewDistanceCullComponent;
class AAeyerjiLinkedTeleporter;
struct FGameplayTagContainer;
struct FAbilityEndedData;


UENUM(BlueprintType)
enum class EAeyerjiMoveLoopMode : uint8
{
	StopOnly        UMETA(DisplayName="Stop Only"),
	FollowOnly      UMETA(DisplayName="Follow Only") // friendly follow: keep looping, idle when close
};

UENUM()
enum class EAeyerjiMouseButton : uint8
{
	None,
	Left,
	Right
};

UENUM()
enum class EAeyerjiMouseIntent : uint8
{
	None,
	GroundMove,
	BasicAttack,
	Interaction,
	SuppressedUntilRelease
};

UENUM()
enum class EAeyerjiMousePhase : uint8
{
	None,
	Held,
	ReleasedPendingAttack
};

USTRUCT(BlueprintType)
struct FCursorFollowTurnRateBucket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float MaxAngleDeg = 0.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float TurnRateScalar = 1.f;
};

class AAeyerjiLootPickup;
class AEnemyParentNative;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiMoveLoopArrivedSig, AActor*, Target);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiFacingReadySig, AActor*, Target);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExtractionCountdownUpdatedSignature);

/** BP notify: local client detected a Pawn under the cursor during click */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCursorPawnHitSignature, AActor*, Actor, const FHitResult&, Hit);

/** BP notify (server-side): owning client reported a Pawn click to the server */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerPawnClickedSignature, AActor*, Actor);

/** Authoritative click-to-move controller (UE-5.6) */
UCLASS()
class AEYERJI_API AAeyerjiPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AAeyerjiPlayerController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** Prevents engine restart callbacks from replacing the explicit player-pawn camera target. */
	virtual void AutoManageActiveCameraTarget(AActor* SuggestedTarget) override;

	/** Restores the local camera after a spawn or respawn because this controller deliberately disables UE's automatic camera-target management. */
	void RestoreLocalCameraToPossessedPawn();

	/** Rebinds any live inventory bag widgets to the currently possessed player pawn. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Inventory")
	void RebindInventoryWidgetsToCurrentPawn();


	UFUNCTION(Server, Reliable)
	void Server_AbortMovement();

	void AbortMovement_Local() const;
	void AbortMovement_Both();
	void BeginLocalAbilityCastInputLock(float DurationSeconds);
	float GetLocalAbilityCastInputLockDuration() const { return LocalAbilityCastInputLockDuration; }

    UFUNCTION(BlueprintCallable, Category="Aeyerji|Facing")
    void EnsureLocomotionRotationMode();

	// --- pending-teleporter timer (10 Hz) ---
	FTimerHandle PendingTeleporterTimer;

	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Teleporter")
	float PendingTeleporterInterval = 0.10f;

	/** Starts watching for arrival at a clicked teleporter endpoint. */
	void StartPendingTeleporter(AAeyerjiLinkedTeleporter* Teleporter, uint8 EndpointIndex);

	/** Clears the current pending teleporter request, if any. */
	void StopPendingTeleporter();

	/** Timer callback that fires the server use request after movement reaches interaction range. */
	void ProcessPendingTeleporter();

	/** Chooses a reachable point near the selected teleporter endpoint. */
	bool ComputeTeleporterGoal(const AAeyerjiLinkedTeleporter* Teleporter, uint8 EndpointIndex, FVector& OutGoal) const;

	// --- pending generic interaction timer (10 Hz) ---
	FTimerHandle PendingInteractionTimer;

	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Interaction")
	float PendingInteractionInterval = 0.10f;

	/** Starts watching for arrival at a clicked generic interactable actor. */
	void StartPendingInteraction(AActor* InteractableActor);

	/** Clears the current pending generic interaction request, if any. */
	void StopPendingInteraction();

	/** Timer callback that requests server interaction once the pawn reaches interaction range. */
	void ProcessPendingInteraction();

	/** Chooses a reachable point near a generic interactable actor. */
	bool ComputeInteractionGoal(AActor* InteractableActor, FVector& OutGoal) const;

    // Optional: Apply an avoidance profile (map-specific tuning)
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Movement|Avoidance")
    void ApplyAvoidanceProfile(const UAeyerjiAvoidanceProfile* Profile);

	// ── Assets ────────────────────────────────────────────────────────────
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputMappingContext> IMC_Default = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> IMC_ShowLootFallback = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_Attack_Click = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_Move_Click = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_Interact = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input")
	FKey AttackClickPhysicalKey = EKeys::LeftMouseButton;
	/** Cached physical key for IA_Interact, resolved from IMC_Default so same-key mouse binds can de-duplicate. */
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input")
	FKey InteractClickPhysicalKey = EKeys::Invalid;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input")
	FKey MoveClickPhysicalKey = EKeys::RightMouseButton;
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_ShowLoot = nullptr; // LeftAlt (Hold)
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_DropItem = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_CancelAction = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|VFX") TObjectPtr<UNiagaraSystem> FX_Cursor = nullptr;
    UPROPERTY(EditAnywhere, Category="Aeyerji|Navigation")
    float MouseNavCacheRefreshInterval = 0.05f;

    // Map-configurable avoidance profile
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Movement|Avoidance")
    TObjectPtr<UAeyerjiAvoidanceProfile> AvoidanceProfile = nullptr;

	UFUNCTION() void OnShowLootPressed();
	UFUNCTION() void OnShowLootReleased();

	// Client→Server RPCs the client is allowed to call
	UFUNCTION(Server, Reliable) void Server_SetDifficultySlider(float NewDifficultySlider);
	UFUNCTION(Server, Reliable) void Server_SetWorldTier(int32 NewWorldTier);
	UFUNCTION(Server, Reliable) void Server_RequestZoneTransition(FName TargetZoneId);
	UFUNCTION(Server, Reliable) void Server_ReportZoneReady(int32 ReportedTransitionId);

	/** Requests server-authoritative use of a clicked linked teleporter endpoint. */
	UFUNCTION(Server, Reliable) void Server_RequestLinkedTeleporterUse(AActor* TeleporterActor, uint8 EndpointIndex);

	/** Requests server-authoritative use of a generic interactable actor. */
	UFUNCTION(Server, Reliable) void Server_RequestInteractableUse(AActor* InteractableActor);

	/** Requests a server-validated gold repair for the active survival defense objective. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aeyerji|Survival|Defense")
	void Server_RequestDefenseObjectiveRepair(AActor* ObjectiveActor, FName OptionId);

	/** Server-to-owning-client hook that opens the local repair menu for the active defense objective. */
	UFUNCTION(Client, Reliable)
	void Client_ShowDefenseObjectiveRepairMenu(AActor* ObjectiveActor, const TArray<FAeyerjiDefenseRepairOption>& RepairOptions, int64 CurrentGold, float CurrentHealth, float MaxHealth);

	/** Server-to-owning-client hook for localized mission feedback such as repair validation failures. */
	UFUNCTION(Client, Reliable)
	void Client_ShowMissionMessageKey(FName MessageKey, float DisplaySeconds);

	/** Requests a server-validated between-round survival upgrade choice. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aeyerji|Survival|Upgrades")
	void Server_SelectSurvivalUpgrade(FName OptionId, int32 OfferRevision);

	void BeginAbilityTargeting(const FAeyerjiAbilitySlot& Slot);
	UFUNCTION(Server, Reliable) void Server_ActivateAbilityAtLocation(const FAeyerjiAbilitySlot& Slot, FVector_NetQuantize Target);
	UFUNCTION(Server, Reliable) void Server_ActivateAbilityOnActor(const FAeyerjiAbilitySlot& Slot, AActor* TargetActor);
	UFUNCTION(Server, Reliable) void Server_ActivateAbilityInstant(const FAeyerjiAbilitySlot& Slot);
	UFUNCTION(Server, Reliable) void Server_CancelActiveAbilityCast();
	/** Resolves the current melee-valid enemy under the cursor using the shared click/snap rules. */
	bool ResolveAttackTargetUnderCursor(FHitResult& OutHit) const;
	/** Resolves only a directly clicked attack target, without hover/recent/snap forgiveness. */
	bool ResolveDirectAttackTargetUnderCursor(FHitResult& OutHit) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD")
	void ShowPopupMessage(const FText& Message, float Duration = 2.f);

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD")
	void BP_ShowPopupMessage(const FText& Message, float Duration);

	/** Optional native end-of-run widget class used for results, retry, and menu return. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	TSubclassOf<UW_EndRunScreen> EndRunScreenClass = nullptr;

	/** Optional main-menu widget class used as a fallback when a streamed menu zone does not bootstrap UI on its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	TSubclassOf<UUserWidget> MainMenuWidgetClass = nullptr;

	/** Z-order used when the controller adds the fallback main-menu widget to the viewport. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	int32 MainMenuWidgetZOrder = 300;

	/** Optional mission/objective HUD widget class owned locally by the controller. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD", meta=(DisplayName="Mission HUD Class"))
	TSubclassOf<UW_AeyerjiMissionHUD> MissionHUDClass = nullptr;

	/** Z-order used when the controller adds the mission HUD to the viewport. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD", meta=(DisplayName="Mission HUD Z Order"))
	int32 MissionHUDZOrder = 25;

	/** Enables the controller-owned local minimap during gameplay world-flow phases. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD|Minimap")
	bool bEnableMinimap = true;

	/** Optional designer subclass; when unset the fully functional native placeholder minimap is used. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD|Minimap", meta=(DisplayName="Minimap Widget Class"))
	TSubclassOf<UW_AeyerjiMinimap> MinimapWidgetClass = nullptr;

	/** Z-order used when the controller adds the local minimap to the viewport. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|HUD|Minimap", meta=(DisplayName="Minimap Z Order"))
	int32 MinimapZOrder = 20;

	/** Local HUD state: true while the player is standing inside the extraction portal countdown. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	bool bExtractionCountdownActive = false;

	/** Local HUD state: total extraction countdown length in seconds. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	float ExtractionCountdownDurationSeconds = 0.f;

	/** Local HUD state: elapsed extraction countdown time in seconds. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	float ExtractionCountdownElapsedSeconds = 0.f;

	/** Local HUD state: time remaining before the map exits in seconds. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	float ExtractionCountdownSecondsRemaining = 0.f;

	/** Local HUD state: normalized extraction countdown progress in the range [0..1]. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|HUD")
	float ExtractionCountdownProgress = 0.f;

	/** Local-only HUD signal fired whenever the extraction countdown state changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|HUD")
	FOnExtractionCountdownUpdatedSignature OnExtractionCountdownUpdated;

	/** Server-driven UI notify that starts the local extraction countdown display. */
	UFUNCTION(Client, Reliable)
	void Client_BeginExtractionCountdown(float DurationSeconds);

	/** Server-driven UI notify that clears the local extraction countdown display. */
	UFUNCTION(Client, Reliable)
	void Client_ResetExtractionCountdown();

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	AAeyerjiEncounterDirector* GetEncounterDirector() const { return EncounterDirector; }

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	AAeyerjiLevelDirector* GetLevelDirector() const { return LevelDirector; }

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	bool HasActiveRunTimeLimit() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	float GetActiveRunTimeLimitSeconds() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	float GetRunTimerProgress01() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	float GetRunTimerSecondsRemaining() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	bool IsKillTargetRun() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	bool IsBossClearedRun() const;

	/** Deprecated legacy HUD bridge; objective widgets should consume replicated objective snapshots instead. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Deprecated", meta=(DeprecatedFunction, DeprecationMessage="Legacy HUD bridge. Use FAeyerjiObjectiveState from AAeyerjiGameState instead.", BlueprintInternalUseOnly="true"))
	bool IsKillTargetThenBossRun() const;

	/** Re-resolves gameplay-only actor references after streamed zone activation or possession changes. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Director")
	void RefreshZoneRuntimeReferences();

	/** NEW: Client→Server notify of pawn click (optional if you need server-side reaction) */
	UFUNCTION(Server, Reliable)
	void Server_NotifyPawnClicked(AActor* Actor);
	void ReportMouseNavContextToServer(EMouseNavResult Result, const FVector& NavLocation, const FVector& CursorLocation, APawn* ClickedPawn);
	bool GetCachedMouseNavContext(EMouseNavResult& OutResult, FVector& OutNavLocation, FVector& OutCursorLocation, APawn*& OutPawn, float MaxAgeSeconds = 1.0f) const;

	UFUNCTION(Server, Reliable)
	void Server_SetMouseNavContext(EMouseNavResult Result, FVector NavLocation, FVector CursorLocation, APawn* ClickedPawn);


	/** NEW: Local BP “intercept” hook. Return true to CONSUME the click (skip native flow). */
	UFUNCTION(BlueprintNativeEvent, Category="Aeyerji|Input")
	bool OnPreClickPawnHit(AActor* Actor, const FHitResult& Hit);

	/** NEW: Local BP signal (does NOT consume). Fires on the local client before native continues. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Input")
	FOnCursorPawnHitSignature OnCursorPawnHit;

	/** NEW: Server BP signal (when the server receives Server_NotifyPawnClicked). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Input")
	FOnServerPawnClickedSignature OnServerPawnClicked;

	TWeakObjectPtr<AAeyerjiLinkedTeleporter> PendingTeleporter;
	TWeakObjectPtr<AActor> PendingInteractable;
	uint8 PendingTeleporterEndpointIndex = 0;
	
	// Debug
	UFUNCTION(Exec)
	void RefreshLootScalingDebug();

	/** Prints the current desktop resolution, active screen resolution, window mode, and resolution scale. */
	UFUNCTION(Exec)
	void AJ_DisplayInfo();

	/** Sets the current game window resolution and mode, applies it immediately, and saves the result. */
	UFUNCTION(Exec)
	void AJ_SetResolution(int32 Width, int32 Height, int32 WindowMode = 1);

	/** Switches to the current desktop resolution in borderless fullscreen, applies it immediately, and saves the result. */
	UFUNCTION(Exec)
	void AJ_UseDesktopResolution();

	/** Sets the current resolution scale percentage, applies it immediately, and saves the result. */
	UFUNCTION(Exec)
	void AJ_SetResolutionScale(float ScalePercent = 100.f);

	/** Sets the overall scalability level, where 0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic. */
	UFUNCTION(Exec)
	void AJ_SetOverallQuality(int32 QualityLevel = 3);

	/** Sets the local FPS limit, where 0 disables the cap. */
	UFUNCTION(Exec)
	void AJ_SetFPSLimit(float FPSLimit = 0.f);

	/** Sets the engine fixed framerate, where 0 disables the fixed timestep cap. */
	UFUNCTION(Exec)
	void AJ_SetFixedFPS(float FixedFPS = 0.f);

	/** Sets the controlled pawn's AttackDamage base attribute on the server. */
	UFUNCTION(Exec)
	void AJ_SetDamage(float DamageValue);

	/** Sets the controlled pawn's HPMax and current HP to the same value on the server. */
	UFUNCTION(Exec)
	void AJ_SetHP(float HPValue);

	/** Opens the built-in UE console in non-shipping builds when the viewport console is available. */
	UFUNCTION(Exec)
	void AJ_OpenConsole();

	UFUNCTION(Server, Reliable)
	void ServerRefreshLootScalingDebug();

	UFUNCTION(Server, Reliable)
	void ServerAJ_SetDamage(float DamageValue);

	UFUNCTION(Server, Reliable)
	void ServerAJ_SetHP(float HPValue);
	
	/** Attempts to activate the primary attack ability, optionally with an explicit mouse-selected target. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Attack")
	bool ActivatePrimaryAttackAbility(AActor* ExplicitTarget = nullptr);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Movement")
	void StartMoveToActorLoop(AActor* Target,
							  float AcceptanceRadius = 50.f,
							  bool bPreferBehind = true,
							  float BehindDistance = 180.f,
							  float ArcHalfAngleDeg = 70.f);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Movement")
	void StopMoveToActorLoop();

	/** Fires once when we first get within AcceptanceRadius of the target (per approach). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Movement")
	FAeyerjiMoveLoopArrivedSig OnMoveLoopArrived;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Movement")
	void StartFollowActorLoop(AActor* Target,
							  float AcceptanceRadius = 200.f,
							  float BehindDistance = 180.f,
							  float ArcHalfAngleDeg = 70.f);

	/** Fires when we're facing the target within tolerance. Bind in BP to actually attack. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Facing")
	FAeyerjiFacingReadySig OnFacingReady;

	/**
	 * Begin turning to face Target. When within AcceptAngleDeg, broadcasts OnFacingReady(Target).
	 * If bPauseMoveLoopWhileFacing is true, we temporarily stop the move loop while turning.
	 */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Facing")
	void StartFaceActorAndNotify(AActor* Target,
								 float AcceptAngleDeg = 10.f,
								 float MaxTurnRateDegPerSec = 720.f,
								 float TimeoutSec = 0.6f,
								 bool bFireOnTimeout = true,
								 bool bPauseMoveLoopWhileFacing = true);

	/** Cancel the facing loop (e.g., on a new click). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Facing")
	void CancelFaceActor();
	void TickFaceLoop();

protected:
	virtual void OnRep_Pawn() override;
	virtual void OnRep_PlayerState() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// AActor
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Restores the gameplay viewport focus/capture mode so the first click still reaches Enhanced Input. */
	void ApplyGameplayMouseInputMode(bool bFlushInput = false);

	/** Local-only roof occlusion fade helper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAeyerjiCameraOcclusionFadeComponent> CameraOcclusionFade = nullptr;

	/** Local-only view distance culling helper. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Camera", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UAeyerjiViewDistanceCullComponent> ViewDistanceCull = nullptr;

	/** Cached encounter director retained only for legacy HUD bridge functions during migration. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Director", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AAeyerjiEncounterDirector> EncounterDirector = nullptr;

	/** Cached level director retained only for legacy HUD bridge functions during migration. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Director", meta=(AllowPrivateAccess="true"))
	TObjectPtr<AAeyerjiLevelDirector> LevelDirector = nullptr;
	/** Binds to streamed-world delegates when a GameState is available. */
	void BindWorldFlowDelegates();

	/** Refreshes controller-side gameplay references when a gameplay zone becomes active. */
	UFUNCTION()
	void HandleZoneGameplayReady(FName ZoneId, int32 ReadyTransitionId);

	/** Local-only: reacts to world-flow phase changes so gameplay HUD only exists in gameplay zones. */
	UFUNCTION()
	void HandleWorldFlowPhaseChanged(EAeyerjiWorldFlowPhase NewPhase, EAeyerjiWorldFlowPhase OldPhase, int32 TransitionId);

	/** Local-only: reacts to replicated run-state changes for UI messaging. */
	UFUNCTION()
	void HandleRunStateChanged(EAeyerjiRunState NewState, EAeyerjiRunState OldState);

	/** Local-only: opens and populates the end-of-run widget once results are ready. */
	UFUNCTION()
	void HandleRunResultsReady(const FAeyerjiRunResults& Results);

	/** Local-only: pushes the latest replicated objective snapshot into the controller-owned HUD widget. */
	UFUNCTION()
	void HandleObjectiveStateChanged(const FAeyerjiObjectiveState& ObjectiveState);

	/** Local-only: pushes the latest replicated survival-round snapshot into the controller-owned HUD widget. */
	UFUNCTION()
	void HandleSurvivalRoundStateChanged(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Local-only: pushes the latest replicated survival upgrade offer into the controller-owned HUD widget. */
	UFUNCTION()
	void HandleSurvivalUpgradeOfferChanged(const FAeyerjiSurvivalUpgradeOfferState& OfferState);

	/** Local-only: pushes replicated gold changes into the controller-owned HUD widget. */
	UFUNCTION()
	void HandleGoldChanged(int64 NewGold, int64 Delta);

	void ShowEndRunScreen(const FAeyerjiRunResults& Results);
	void HideEndRunScreen(bool bRestoreGameplayInput);
	void EnsureMainMenuWidget(bool bAllowCreate);
	bool ShouldPresentMainMenu() const;
	void ResetMainMenuWidgetInstance();
	void HideMainMenuWidget();
	void EnsureMissionHUD();
	void EnsureMinimap();
	void HideMinimap();
	void ApplyCurrentObjectiveStateFromGameState();
	void ApplyCurrentSurvivalRoundStateFromGameState();
	void ApplyCurrentSurvivalUpgradeOfferFromGameState();
	void ApplyCurrentGoldStateToMissionHUD();
	void ApplyObjectiveStateToMissionHUD(const FAeyerjiObjectiveState& ObjectiveState);
	void ApplySurvivalRoundStateToMissionHUD(const FAeyerjiSurvivalRoundState& SurvivalState);
	void ApplySurvivalUpgradeOfferToMissionHUD(const FAeyerjiSurvivalUpgradeOfferState& OfferState);
	void BindGoldDelegate();
	void UnbindGoldDelegate();
	bool IsGameplayInputSuppressedByModalUI() const;
	void StartExtractionCountdownState(float DurationSeconds);
	void RefreshExtractionCountdownState();
	void ResetExtractionCountdownState();
	void BroadcastExtractionCountdownUpdated();
	
	/** How close is “close enough” that we should not issue a move? (centimeters) */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float MinMoveDistanceCm = 100.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Input", meta=(ClampMin="0.0", Units="s"))
	float LocalAbilityCastInputLockDuration = 0.65f;

	double LocalAbilityCastInputLockEndTime = -1.0;
	
	// Input
	void OnAttackClickPressed (const FInputActionValue& Val);
	void OnAttackClickHeld    (const FInputActionValue& Val);
	void OnAttackClickReleased(const FInputActionValue& Val);

	void OnMoveClickPressed (const FInputActionValue& Val);
	void OnMoveClickHeld    (const FInputActionValue& Val);
	void OnMoveClickReleased(const FInputActionValue& Val);
	void OnInteractClickPressed(const FInputActionValue& Val);
	void BeginMouseCommand(EAeyerjiMouseButton Button);
	void ReleaseMouseCommand(EAeyerjiMouseButton Button);
	void UpdateMouseCommand(float DeltaSeconds);
	void TransitionMouseIntent(EAeyerjiMouseIntent NewIntent, AActor* NewTarget, const FVector& NewGroundGoal, bool bSpawnMoveFx);
	void CancelMouseOwnedMovement();
	void CancelMouseOwnedCombat();
	void CancelMouseOwnedInteraction();
	void ClearMouseCommandData();
	void CancelMouseCommandCompletely();
	bool IsMouseButtonPhysicallyDown(EAeyerjiMouseButton Button) const;
	bool TryResolveDirectHostileUnderCursor(FHitResult& OutHit, AActor*& OutTarget) const;
	AActor* ResolveAttackableActorFromCursorHit(const FHitResult& Hit) const;
	bool IsMouseCommandTargetInBasicAttackRange(AActor* TargetActor) const;
	void EnsureMouseActorChase(AActor* TargetActor);
	void StartMouseGroundMove(const FVector& Goal, bool bSpawnCursorFX);
	void UpdateMouseGroundMove(const FVector& Goal);
	/**
	 * Stops the currently active primary attack when a new hostile is explicitly selected.
	 * GAS still decides whether the current phase is cancellable, so committed hit windows
	 * cannot be interrupted by a late retarget.
	 */
	void CancelPrimaryAttackForRetarget(AActor* NewTarget);
	bool TriggerPrimaryAttackAbility(UAbilitySystemComponent* ASC, AActor* ExplicitTarget);
	FGameplayAbilitySpecHandle FindPrimaryAttackAbilityHandle(UAbilitySystemComponent* ASC) const;
	void OnDropItemPressed(const FInputActionValue& Val);
	void OnCancelActionPressed(const FInputActionValue& Val);
	bool IsAbilityCastInputLocked() const;
	void BindMouseCommandRecoveryDelegates();
	void UnbindMouseCommandRecoveryDelegates();
	void HandleObservedAbilityEnded(const FAbilityEndedData& EndedData);
	void HandleCastingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void ScheduleMouseCommandRecovery();
	void RecoverMouseCommandAfterAbility();
	void CancelMouseCommandRecovery(bool bSuppressCurrentCommandUntilRelease);
	/** Returns true when IA_Interact and IA_Attack_Click resolve to the same physical key. */
	bool IsInteractClickMappedToAttackClick() const;
	/** Returns true for the short duplicate window after one same-key interaction path handled the click. */
	bool WasSameKeyInteractionHandledRecently() const;
	/** Marks the current click as handled so the paired same-key input action does not run twice. */
	void MarkSameKeyInteractionHandled();

	// Build the tag search container for the primary ability (leaf + parents).
	bool BuildPrimaryAttackTagSearch(UAbilitySystemComponent* ASC, FGameplayTagContainer& OutTags) const;

	void HandleMoveCommand(bool bSpawnCursorFX, bool bIsContinuous);

	TWeakObjectPtr<class AAeyerjiLootPickup> HoveredLoot;
	TWeakObjectPtr<class AEnemyParentNative> HoveredEnemy;
	TWeakObjectPtr<AAeyerjiPlayerState> BoundGoldPlayerState;
	FTimerHandle HoverTimer;
	
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Loot|UI")
	float HoverInterval = 0.05f; // 20 Hz
	
	/** Grace period before clearing hover highlight to avoid flicker when traces momentarily miss. */
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Loot|UI")
	float HoverReleaseGrace = 0.25f;

	/** Grace period for enemy hover to avoid flicker (seconds). */
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Enemy|UI")
	float EnemyHoverReleaseGrace = 0.15f;
	
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Movement")
	float MinTimeBetweenMoves = 0.1f; // 100ms minimum between move commands

	void StartHoverPolling();
	void StopHoverPolling();
	void PollHoverUnderCursor();
	
	double LastHoverHitTime = -1.0;
	double LastEnemyHoverHitTime = -1.0;

	// Commands
	void IssueMoveRPC(const FVector& Goal);
	void IssueMoveRPC(AActor* Target);
	UFUNCTION(Server, Reliable)
	void Server_ActivatePrimaryAttackOnActor(AActor* TargetActor, uint32 CommandSerial);
	/** Confirms whether authority accepted the one-click attack request before its command is consumed. */
	UFUNCTION(Client, Reliable)
	void Client_PrimaryAttackActivationResult(AActor* TargetActor, uint32 CommandSerial, bool bActivated);
	/** Mirrors a local primary-attack retarget cancellation on the authority. */
	UFUNCTION(Server, Reliable)
	void Server_CancelPrimaryAttackForRetarget(AActor* NewTarget);
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void ServerMoveToLocation(const FVector& Goal);
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void ServerMoveToActor   (AActor* Target, const float AcceptanceRadius = 15.f);
	/** Sequenced held-cursor sample. Loss is acceptable because a newer sample supersedes it. */
	UFUNCTION(Server, Unreliable)
	void Server_UpdateCursorFollowGoal(FVector_NetQuantize10 Goal, uint32 UpdateId);
	/** Reliably commits the last held-cursor endpoint without stopping the resulting path. */
	UFUNCTION(Server, Reliable)
	void Server_EndCursorFollow(FVector_NetQuantize10 FinalGoal, uint32 UpdateId);
	UFUNCTION(Server, Reliable)
	void Server_ResetCursorFollowTurnRate(uint32 UpdateId);
	UFUNCTION(Server, Reliable)
	void Server_ApplyCursorFollowTurnRate(const FVector& Goal);

	// Helpers
	void SpawnCursorFX(const FVector& Loc) const;
	bool IsAttackableActor(const AActor* Other) const;
	/** Resolves a requested move target to a nav-safe location for the controlled pawn. */
	bool ResolveSafeMoveGoal(const FVector& DesiredGoal, FVector& OutGoal) const;
	/** Authority-side helper that repairs the controlled pawn before issuing a path request. */
	bool EnsureControlledPawnOnSafeNav(bool bImmediateRecover) const;
	void RefreshLootScalingDebug_Internal();
	void EnsureViewportConsole();
	void PrintDisplayDebugMessage(const FString& Message);
	UAbilitySystemComponent* GetCheatTargetAbilitySystemComponent() const;
	void ApplyCheatAttackDamage(float DamageValue);
	void ApplyCheatHP(float HPValue);
	bool AreCheatsAllowed() const;

	// Cached targeting
	UPROPERTY() FVector CachedGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> CachedTarget;
	/** Last hostile selected for a primary attack; prevents a same-target click from cancelling combo input. */
	TWeakObjectPtr<AActor> LastPrimaryAttackTarget;

	// Pending move state for client prediction
	FVector PendingMoveGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PendingMoveTarget;

	// Click/hold semantics
	float LastServerCmdTs = 0.0f;
	double LastClickMoveCommandTime = -1.0;
	FVector LastClickMoveCommandGoal = FVector::ZeroVector;
	bool bCursorFollowActive = false;
	TWeakObjectPtr<AActor> CursorFollowActor;
	TWeakObjectPtr<UStaticMeshComponent> CursorFollowDebugMesh;
	FVector CursorFollowSmoothedGoal = FVector::ZeroVector;
	bool bCursorFollowHasSmoothedGoal = false;
	bool bCursorFollowTurnRateActive = false;
	bool bCursorFollowBucketsSorted = false;
	float SavedCursorFollowYawRate = 0.f;
	double LastCursorFollowRepathTime = -1.0;
	FVector LastCursorFollowRepathGoal = FVector::ZeroVector;
	double LastCursorFollowClientDiagTime = -1.0;
	double LastCursorFollowServerDiagTime = -1.0;
	double LastCursorFollowNetworkSendTime = -1.0;
	FVector LastCursorFollowNetworkGoal = FVector::ZeroVector;
	uint32 NextCursorFollowUpdateId = 0;
	uint32 LastReceivedCursorFollowUpdateId = 0;
	bool bHasCursorFollowNetworkGoal = false;
	bool bHasReceivedCursorFollowUpdateId = false;
	double CursorFollowHoldStartTime = -1.0;
	FVector CursorFollowHoldStartGoal = FVector::ZeroVector;
	bool bCursorFollowHoldPrimed = false;
	bool bCursorFollowHoldActive = false;

	// Attack/move input state.
	bool bMoveClickHeld = false;
	bool bAttackClickHeld = false;

	struct FMouseCommandState
	{
		EAeyerjiMouseButton Owner = EAeyerjiMouseButton::None;
		EAeyerjiMouseIntent Intent = EAeyerjiMouseIntent::None;
		EAeyerjiMousePhase Phase = EAeyerjiMousePhase::None;
		TWeakObjectPtr<AActor> TargetActor;
		TWeakObjectPtr<AActor> IssuedMoveTarget;
		FVector GroundGoal = FVector::ZeroVector;
		bool bAttackCommitted = false;
		bool bAwaitingServerAttackResult = false;
		uint32 CommandSerial = 0;
		double LastAttackAttemptTime = -1.0;
	};

	FMouseCommandState MouseCommand;
	uint32 NextMouseCommandSerial = 1;

	/** True while a mouse command is temporarily suspended by an action-bar ability cast. */
	bool bMouseCommandPausedByAbilityCast = false;
	TWeakObjectPtr<UAbilitySystemComponent> MouseCommandRecoveryASC;
	FDelegateHandle ObservedAbilityEndedHandle;
	FDelegateHandle CastingTagChangedHandle;
	FTimerHandle MouseCommandRecoveryTimerHandle;
	uint32 RecoveryBlockedCommandSerial = 0;
	bool bMouseCommandRecoveryPending = false;
	double LastMouseAttackChaseLogTime = -1.0;
	double LastMouseAttackRangeLogTime = -1.0;
	double LastSameKeyInteractionHandledTime = -1.0;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Input|MouseCommand", meta=(ClampMin="0.0"))
	float BasicAttackRetryInterval = 0.08f;

	/** Keeps melee facing stable briefly after the phase tags clear (seconds). */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat|Melee", meta=(ClampMin="0.0"))
	float PrimaryMeleeRotationLockGraceSeconds = 0.12f;

	/** Percent of AttackRange at which attack-hold movement should stop chasing the current target. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat|Melee", meta=(ClampMin="0.0"))
	float PrimaryAttackMoveStopAtPercentOfRange = 1.0f;

	/** Extra forgiveness added to the melee stop range so hold-attack does not creep forward once the target is hittable. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat|Melee", meta=(ClampMin="0.0", Units="cm"))
	float PrimaryAttackMoveStopExtraBufferCm = 25.f;

	double PrimaryMeleeRotationLockReleaseTime = -1.0;
	bool bPrimaryMeleeRotationLockActive = false;
	bool bPrimaryMeleeMovementBlockActive = false;

	bool bHasQueuedMovementCommand = false;
	bool bQueuedMovementIsActor = false;
	bool bQueuedMovementSpawnCursorFX = false;
	bool bQueuedMovementWasContinuous = false;
	FVector QueuedMovementGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> QueuedMovementTarget;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	TArray<FCursorFollowTurnRateBucket> CursorFollowTurnRateBuckets;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowRepathDistance = 45.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowRepathInterval = 0.025f;

	/** Maximum held-cursor network update rate; local cursor smoothing still runs every frame. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Networking", meta=(ClampMin="0.016"))
	float CursorFollowNetworkUpdateInterval = 0.05f;

	/** Sends a recovery sample even if cursor movement remains below the distance threshold. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Networking", meta=(ClampMin="0.05"))
	float CursorFollowNetworkHeartbeatInterval = 0.20f;

	/** Minimum cursor displacement that merits another network sample. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Networking", meta=(ClampMin="1.0", Units="cm"))
	float CursorFollowNetworkGoalDistance = 45.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowGoalInterpSpeed = 22.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowGoalSnapDistance = 650.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowPathObservationDistance = 80.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowHoldStartDelay = 0.06f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float CursorFollowHoldStartDistance = 40.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Debug")
	bool bDrawCursorFollowProxy = false;
	
	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting")
	FAeyerjiTargetingTunables TargetingTunables;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting")
	bool bEnableTargetSnap = true;

	/** Max cursor distance (pixels) for snapping to nearby enemies. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting", meta=(ClampMin="0.0"))
	float TargetSnapScreenRadiusPx = 50.f;

	/** Max world-space distance (cm) from the click point for snapping. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting", meta=(ClampMin="0.0", Units="cm"))
	float TargetSnapWorldRadiusCm = 300.f;

	/** Scale snap radius based on camera distance; clamped between min/max. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting", meta=(ClampMin="0.1"))
	float TargetSnapZoomScaleMin = 0.75f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting", meta=(ClampMin="0.1"))
	float TargetSnapZoomScaleMax = 1.25f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Targeting", meta=(ClampMin="1.0", Units="cm"))
	float TargetSnapCameraDistanceRef = 1200.f;

	UPROPERTY()
	TObjectPtr<UAeyerjiTargetingManager> TargetingManager = nullptr;
	
	/** How often to recompute the goal (s) */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float MoveLoopInterval = 0.12f;

	/** Search extents used when projecting a point to the navmesh */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	FVector NavProjectExtents = FVector(200.f, 200.f, 500.f);

	/** Current mode for the loop. */
	UPROPERTY(VisibleInstanceOnly, Category="Aeyerji|Movement")
	EAeyerjiMoveLoopMode MoveLoopMode = EAeyerjiMoveLoopMode::StopOnly;

	/** Tracks if we've already broadcast OnMoveLoopArrived for the current approach. */
	bool bMoveLoopArrivedBroadcast = false;

	/** Tick rate for the facing loop (seconds). 0.01 ≈ 100 Hz. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Facing")
	float FaceLoopInterval = 0.01f;
	
	/** Loop state */
	FTimerHandle MoveLoopTimer;
	TWeakObjectPtr<AActor> MoveLoopTarget;
	float MoveLoopAcceptanceRadius = 200.f;
	bool  bMoveLoopPreferBehind = true;
	float MoveLoopBehindDistance = 180.f;
	float MoveLoopArcHalfAngleDeg = 70.f;

	// Facing loop state
	FTimerHandle             FaceLoopTimer;
	TWeakObjectPtr<AActor>   FaceTarget;
	float                    FaceAcceptAngleDeg = 10.f;
	float                    FaceMaxTurnRateDegPerSec = 720.f;
	double                   FaceDeadline = 0.0;
	bool                     bFaceFireOnTimeout = true;
	bool                     bPauseMoveLoopDuringFacing = true;

	/** Per-tick workhorse */
	void TickMoveLoop();

	/** Picks a good goal around/behind target and projects it to the navmesh */
	bool ComputeSmartGoalForTarget(AActor* Target,
								   bool bPreferBehind,
								   float BehindDistance,
								   float ArcHalfAngleDeg,
								   FVector& OutGoal) const;
	
	// Tracing helpers
	bool TraceCursor(ECollisionChannel Channel, FHitResult& OutHit, bool bTraceComplex = false) const;
	bool ShouldIgnoreCursorActor(const AActor* Actor) const;
	bool TryGetGroundHit(FHitResult& OutHit) const;
	bool TryGetPawnHit(FHitResult& OutHit) const;
	bool TryGetLinkedTeleporterHit(FHitResult& OutHit, AAeyerjiLinkedTeleporter*& OutTeleporter, uint8& OutEndpointIndex) const;
	bool TryGetInteractableHit(FHitResult& OutHit, AActor*& OutInteractable) const;
	bool TryDropItemUnderCursor();

	// Flow helpers
	FAeyerjiTargetingClickContext BuildTargetingClickContext() const;

	/** Handles a clicked linked teleporter endpoint: use if in range, otherwise move toward it. Returns true if consumed. */
	bool HandleLinkedTeleporterUnderCursor(AAeyerjiLinkedTeleporter* Teleporter, uint8 EndpointIndex, const FHitResult& TeleporterHit);

	/** Handles a clicked generic interactable: use if in range, otherwise move toward it. Returns true if consumed. */
	bool HandleInteractableUnderCursor(AActor* InteractableActor, const FHitResult& InteractableHit);

	/** Broadcasts pawn-hit hooks and lets BP consume the click; returns true if BP consumed. */
	bool TryConsumePawnHit(const FHitResult& PawnHit);

	/** Clears targeting state (delegates to the targeting manager). */
	void ClearTargeting();
	void EnsureTargetingManagerInitialized();
	void ClearAttackInputIntent();

    /** Sets CachedGoal/Target from a surface hit and dispatches movement. */
    void MoveToGroundFromHit(const FHitResult& SurfaceHit, bool bSpawnCursorFX, bool bIsContinuous);
	AActor* GetOrCreateCursorFollowActor();
	void UpdateContinuousMoveGoal(const FVector& Goal);
	void UpdateCursorFollowTurnRate(const FVector& DesiredGoal);
	void ResetCursorFollowTurnRate();
	void UpdateCursorFollowDebugProxy(AActor* FollowActor);
	void BeginCursorFollowHold(const FVector& Goal);
	void ResetCursorFollowHold();
	bool ShouldRunCursorFollowHold(const FVector& Goal);

    /** Common reset at the start of both click handlers. */
    void ResetForClick();
    void ResetForMoveOnly();

    /** Returns the ability system component for the possessed pawn if any. */
    UAbilitySystemComponent* GetControlledAbilitySystem() const;
    /** True when the controlled pawn is flagged dead (ASC or actor tag). */
    bool IsControlledPawnDead() const;

    /** Cancels blocking abilities if allowed; returns true when movement should be suppressed. */
    bool HandleMovementBlockedByAbilities();

	/** Applies or clears temporary melee rotation locking based on active melee phase tags. */
	void UpdatePrimaryMeleeRotationLock();

	/** Returns true only while an active primary-melee spec owns a primary-melee phase tag on the ASC. */
	bool HasActivePrimaryMeleePhaseTag(const UAbilitySystemComponent* ASC) const;

	/** Temporarily freezes movement-driven/controller-driven yaw changes during melee. */
	void PushPrimaryMeleeRotationLockMode();
	void PopPrimaryMeleeRotationLockMode();

	/** Stores the latest move request while primary melee owns locomotion. */
	void QueueMovementCommand(const FVector& Goal, bool bSpawnCursorFX, bool bIsContinuous);

	/** Stores the latest actor move request while primary melee owns locomotion. */
	void QueueMovementCommand(AActor* Target, bool bIsContinuous);

	/** Clears any deferred movement command waiting for the melee lock to release. */
	void ClearQueuedMovementCommand();

	/** Runs the deferred movement command once primary melee releases locomotion. */
	void FlushQueuedMovementCommandIfAllowed();

	/** Returns true if our pawn's capsule is touching the other actor's capsule (2D), with small buffers. */
	static bool AreCapsulesTouching2D(const APawn* SelfPawn, const AActor* OtherActor,
	                                  float ExtraRadiusBufferCm = 6.f, float ZSlackCm = 30.f);

	/** Utility used by AreCapsulesTouching2D */
	static bool ExtractCapsuleParams(const AActor* Actor, float& OutRadius, float& OutHalfHeight);

	// --- Facing rotation mode save/restore ---
	struct FSavedFacingRotationMode
	{
		bool bValid = false;
		bool bUseControllerRotationYaw = false;
		bool bOrientRotationToMovement = true;
		bool bUseControllerDesiredRotation = false;
		float SavedRotationRateYaw = 360.f;
	};
	FSavedFacingRotationMode SavedFacingMode;

	struct FSavedPrimaryMeleeRotationMode
	{
		bool bValid = false;
		bool bUseControllerRotationYaw = false;
		bool bOrientRotationToMovement = true;
		bool bUseControllerDesiredRotation = false;
		float SavedRotationRateYaw = 360.f;
	};
	FSavedPrimaryMeleeRotationMode SavedPrimaryMeleeRotationMode;

	void PushFacingRotationMode(float DesiredYawRateDegPerSec);
	void PopFacingRotationMode();

	// --- Short-range local avoidance (player-side steering shim) ---
public:
	/** Enable a brief sidestep when a pawn blocks the immediate path. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
	bool bEnableShortAvoidance = true;

    /** How far ahead (cm) to probe for blocking pawns. Lower = less aggressive. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceProbeDistance = 160.f;

    /** Lateral sidestep distance (cm) when avoiding. Lower = milder sidestep. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceSideStepDistance = 220.f;

	/** Multiplier for capsule radius when sweeping (>=1). */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
	float AvoidanceProbeRadiusScale = 1.05f;

    /** Min/Max duration for which the sidestep goal is held. Shorter = less aggressive. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceHoldTimeMin = 0.22f;

    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceHoldTimeMax = 0.40f;

	/** If the blocking pawn is our current target, skip avoidance. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
	bool bSkipAvoidanceWhenBlockingIsCurrentTarget = false;

	/** If the blocking pawn is our current target, bias the sidestep around the target tangent. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance", meta=(EditCondition="!bSkipAvoidanceWhenBlockingIsCurrentTarget"))
    bool bBiasDetourAroundTargetTangent = true;

    /** Debug: draw sweeps and chosen sidestep. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance|Debug")
    bool bAvoidanceDebugDraw = false;

    /** Project sidestep to navmesh. Turn off to use raw candidate. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    bool bAvoidanceProjectToNavmesh = true;

    /** Skip avoidance when already close to goal (cm). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceMinDistanceToGoal = 200.f;

    /** Only avoid if moving at least this speed (cm/s). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceMinSpeedCmPerSec = 60.f;

    /** Cooldown between avoidance triggers (seconds). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceMinTimeBetweenTriggers = 0.35f;

    /** Early release when we got close to the sidestep goal (cm). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceEarlyReleaseDistance = 120.f;

    /** Scale for "nudge" vector if both sidesteps appear blocked. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceBlockedNudgeScale = 0.6f;

    /** Reject sidestep if it turns more than this angle from desired move (deg). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceMaxDetourAngleDeg = 75.f;

    /** Reject sidestep if it increases distance to goal beyond this factor. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Movement|Avoidance")
    float AvoidanceMaxGoalDistanceFactor = 1.15f;

private:
    /**
     * Keeps the player view controller-owned when Blueprint defaults are loaded or a pawn is
     * replaced. The respawn flow explicitly selects the newly possessed pawn as the view target.
     */
    void DisableAutomaticCameraTargetManagement();

    bool HasShowLootMapping(const UInputMappingContext* Context) const;
    void EnsureShowLootBinding();

    void RefreshMouseNavContextCache();
    double LastMouseNavCacheUpdateTime = -1.0;
    struct FMouseNavServerCache
    {
        EMouseNavResult Result = EMouseNavResult::None;
        FVector NavLocation = FVector::ZeroVector;
        FVector CursorLocation = FVector::ZeroVector;
        TWeakObjectPtr<APawn> Pawn;
        double Timestamp = -1.0;

        void Invalidate()
        {
            Result = EMouseNavResult::None;
            NavLocation = FVector::ZeroVector;
            CursorLocation = FVector::ZeroVector;
            Pawn = nullptr;
            Timestamp = -1.0;
        }
    };

    void SetMouseNavContextInternal(EMouseNavResult Result, const FVector& NavLocation, const FVector& CursorLocation, APawn* ClickedPawn);
    mutable FMouseNavServerCache MouseNavServerCache;

    /** If set, we keep issuing the sidestep goal until time elapses. */
    bool   bAvoidanceActive = false;
    FVector ActiveAvoidanceGoal = FVector::ZeroVector;
	double AvoidanceEndTime = 0.0;
	double LastAvoidanceTriggerTime = 0.0;

	/** Adjusts the move goal in-place if a pawn immediately blocks our path. */
	bool AdjustGoalForShortAvoidance(FVector& InOutGoal);

	/** Ensures path following isn't ticking on an invalid or dead pawn. */
	void UpdatePathFollowingForPawnState();
	bool bPathFollowingTickSuppressed = false;
	bool bShowLootFallbackAdded = false;

	/** Last GameState this controller bound to for streamed world-flow events. */
	TWeakObjectPtr<AAeyerjiGameState> BoundWorldFlowGameState;

	/** Lazily-created end-of-run widget owned by this controller. */
	UPROPERTY(Transient)
	TObjectPtr<UW_EndRunScreen> EndRunScreenWidget = nullptr;

	/** Lazily-created fallback main-menu widget owned by this controller. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MainMenuWidget = nullptr;

	/** Lazily-created mission/objective HUD widget owned by this controller. */
	UPROPERTY(Transient)
	TObjectPtr<UW_AeyerjiMissionHUD> MissionHUDWidget = nullptr;

	/** Local-only minimap widget; it reads replicated actor state without creating new network traffic. */
	UPROPERTY(Transient)
	TObjectPtr<UW_AeyerjiMinimap> MinimapWidget = nullptr;

	/** Local start time used to animate the extraction countdown HUD after the server notifies us. */
	double ExtractionCountdownStartTimeSeconds = -1.0;
};
