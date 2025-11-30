// ===============================
// File: AeyerjiPlayerController.h
// ===============================
#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "NiagaraSystem.h"
#include "Avoidance/AeyerjiAvoidanceProfile.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "MouseNavBlueprintLibrary.h"
#include "AeyerjiPlayerController.generated.h"
class APawn;
class UAbilitySystemComponent;


UENUM(BlueprintType)
enum class EAeyerjiMoveLoopMode : uint8
{
	StopOnly        UMETA(DisplayName="Stop Only"),
	FollowOnly      UMETA(DisplayName="Follow Only") // friendly follow: keep looping, idle when close
};

class AAeyerjiLootPickup;
class AEnemyParentNative;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiMoveLoopArrivedSig, AActor*, Target);
UENUM()
enum class ECastFlow : uint8 { Normal, AwaitingGround, AwaitingEnemy, AwaitingFriend };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiFacingReadySig, AActor*, Target);

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


	UFUNCTION(Server, Reliable)
	void Server_AbortMovement();

	void AbortMovement_Local() const;
	void AbortMovement_Both();

    UFUNCTION(BlueprintCallable, Category="Aeyerji|Facing")
    void EnsureLocomotionRotationMode();

	// --- pending-pickup timer (10 Hz) ---
	FTimerHandle PendingPickupTimer;
	
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Loot")
	float PendingPickupInterval = 0.10f; // 10 Hz

	void StartPendingPickup(AAeyerjiLootPickup* Loot);
	void StopPendingPickup();
    void ProcessPendingPickup(); // timer callback

	// Helper: choose a reachable point near the loot
    bool ComputePickupGoal(const AAeyerjiLootPickup* Loot, FVector& OutGoal) const;

    // Optional: Apply an avoidance profile (map-specific tuning)
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Movement|Avoidance")
    void ApplyAvoidanceProfile(const UAeyerjiAvoidanceProfile* Profile);

	// ── Assets ────────────────────────────────────────────────────────────
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputMappingContext> IMC_Default = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_Attack_Click = nullptr;
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_Move_Click = nullptr;
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Input") TObjectPtr<UInputAction> IA_ShowLoot = nullptr; // LeftAlt (Hold)
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|VFX") TObjectPtr<UNiagaraSystem> FX_Cursor = nullptr;
    UPROPERTY(EditAnywhere, Category="Aeyerji|Navigation")
    float MouseNavCacheRefreshInterval = 0.05f;

    // Map-configurable avoidance profile
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Movement|Avoidance")
    TObjectPtr<UAeyerjiAvoidanceProfile> AvoidanceProfile = nullptr;

	UFUNCTION() void OnShowLootPressed();
	UFUNCTION() void OnShowLootReleased();

	// Client→Server RPCs the client is allowed to call
	UFUNCTION(Server, Reliable) void Server_AddPickupIntent(FName LootActorName);
	UFUNCTION(Server, Reliable) void Server_ClearPickupIntent(FName LootActorName);
	UFUNCTION(Server, Reliable) void Server_RequestPickup   (FName LootActorName);

	void BeginAbilityTargeting(const FAeyerjiAbilitySlot& Slot);
	UFUNCTION(Server, Reliable) void Server_ActivateAbilityAtLocation(const FAeyerjiAbilitySlot& Slot, FVector_NetQuantize Target);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD")
	void ShowPopupMessage(const FText& Message, float Duration = 2.f);

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD")
	void BP_ShowPopupMessage(const FText& Message, float Duration);

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

	// Loot
	TWeakObjectPtr<AAeyerjiLootPickup> PendingPickup; // optional UI state
	//TODO add to stats some of these vars
	//small unit
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|TODOPutInStats")
	float PickupAcceptRadius = 22000.f;
	
	/** Local BP “on click” hook. Return true to CONSUME the click (skip default move/attack). */
	UFUNCTION(BlueprintNativeEvent, Category="Aeyerji|Input")
	bool OnClick();

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
	AAeyerjiLootPickup* FindLootPickupByName(FName LootActorName) const;

	// AActor
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	
	/** How close is “close enough” that we should not issue a move? (centimeters) */
	UPROPERTY(EditAnywhere, Category="Aeyerji|Movement")
	float MinMoveDistanceCm = 100.f;
	
	// Input
	void OnAttackClickPressed (const FInputActionValue& Val);

	void OnMoveClickPressed (const FInputActionValue& Val);

	void HandleMoveCommand();

	TWeakObjectPtr<class AAeyerjiLootPickup> HoveredLoot;
	TWeakObjectPtr<class AEnemyParentNative> HoveredEnemy;
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
	float MinTimeBetweenMoves = 0.1; // 100ms minimum between move commands

	void StartHoverPolling();
	void StopHoverPolling();
	void PollHoverUnderCursor();
	
	double LastHoverHitTime = -1.0;
	double LastEnemyHoverHitTime = -1.0;

	// Commands
	void IssueMoveRPC(const FVector& Goal);
	void IssueMoveRPC(AActor* Target);
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void ServerMoveToLocation(const FVector& Goal);
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void ServerMoveToActor   (AActor* Target, const float AcceptanceRadius = 15.f);

	// Helpers
	void SpawnCursorFX(const FVector& Loc) const;
	bool IsAttackableActor(const AActor* Other) const;

	// Cached targeting
	UPROPERTY() FVector CachedGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> CachedTarget;

	// Pending move state for client prediction
	FVector PendingMoveGoal = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PendingMoveTarget;

	// Click/hold semantics
	float LastServerCmdTs = 0.0;
	
	ECastFlow           CastFlow   = ECastFlow::Normal;
	FAeyerjiAbilitySlot PendingSlot;
	struct FAbilityRangePreview
	{
		bool bActive = false;
		float Range = 0.f;
		EAeyerjiTargetMode Mode = EAeyerjiTargetMode::Instant;
	};
	FAbilityRangePreview AbilityRangePreview;
	FTimerHandle AbilityRangePreviewTimer;
	
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
	bool TryGetGroundHit(FHitResult& OutHit) const;
	bool TryGetPawnHit(FHitResult& OutHit) const;
	bool TryGetLootHit(FHitResult& OutHit) const;

	// Flow helpers
	/** Handles pending CastFlow like AwaitingGround; returns true if the click was fully consumed. */
	bool HandleCastFlowClick();

	/** Handles a clicked loot actor: pickup if in range, otherwise enqueue intent & move towards it. Returns true if consumed. */
	bool HandleLootUnderCursor(AAeyerjiLootPickup* Loot, const FHitResult& LootHit);

	/** Visual preview for ability range while awaiting second click (e.g., blink ground targeting). */
	void StartAbilityRangePreview(const FAeyerjiAbilitySlot& Slot);
	void StopAbilityRangePreview();
	void TickAbilityRangePreview();
	float ResolveAbilityPreviewRange(const FAeyerjiAbilitySlot& Slot) const;
	void DrawAbilityRangePreview(float Range, EAeyerjiTargetMode Mode);

	/** Broadcasts pawn-hit hooks and lets BP consume the click; returns true if BP consumed. */
	bool TryConsumePawnHit(const FHitResult& PawnHit);

	/** Clears any pending pickup intent (safe to call when none). */
	void ClearPickupIntentIfAny();

	/** Clears targeting state (CastFlow + PendingSlot). */
	void ClearTargeting();

	/** Sets CachedGoal/Target from a surface hit and dispatches movement. */
    void MoveToGroundFromHit(const FHitResult& SurfaceHit);

    /** Common reset at the start of both click handlers. */
    void ResetForClick();
    void ResetForMoveOnly();

    /** Returns the ability system component for the possessed pawn if any. */
    UAbilitySystemComponent* GetControlledAbilitySystem() const;

    /** Cancels blocking abilities if allowed; returns true when movement should be suppressed. */
    bool HandleMovementBlockedByAbilities();

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
};

