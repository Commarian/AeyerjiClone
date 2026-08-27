// EnemyAIController.h
#pragma once
#include "CoreMinimal.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AeyerjiObjectiveTypes.h"
#include "Components/StateTreeComponent.h"
#include "EnemyAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
struct FPropertyChangedEvent;
struct FGameplayTag;

/** Debug-facing source of the controller's current combat target. */
UENUM(BlueprintType)
enum class EAeyerjiEnemyTargetSource : uint8
{
	None UMETA(DisplayName="None"),
	HostileActor UMETA(DisplayName="Hostile Actor"),
	DefenseObjective UMETA(DisplayName="Defense Objective")
};

UCLASS()
class AEYERJI_API AEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AEnemyAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** StateTree asset to run for this AI (assigned in default properties or in-editor). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
	TObjectPtr<UStateTree> DefaultStateTree;

	/** The StateTree component running the AI logic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<class UStateTreeComponent> StateTreeComponent;

	/** Enables the shared far-chase sprint cadence used by Move To Attack Range tasks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Chase Cadence")
	bool bEnableChaseSprintCadence = true;

	/** Maximum time an enemy may remain at RunSpeed during one far-chase sprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Chase Cadence", meta=(ClampMin="0.05", Units="s"))
	float ChaseSprintDurationSeconds = 1.5f;

	/** Recovery time at WalkSpeed after a sprint ends or reaches the close-engagement band. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Chase Cadence", meta=(ClampMin="0.0", Units="s"))
	float ChaseSprintRecoverySeconds = 5.0f;

	/** Extra distance beyond AttackRange that must be exceeded before a recovered enemy may sprint again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Chase Cadence", meta=(ClampMin="0.0", Units="cm"))
	float ChaseSprintReengageDistance = 250.0f;

	/** MaxWalkSpeed units changed per second while blending between WalkSpeed and RunSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Chase Cadence", meta=(ClampMin="0.0"))
	float ChaseSpeedChangeRate = 900.0f;

	/**
	 * Lets the controller's stable combat focus own character yaw instead of Detour Crowd's
	 * rapidly changing avoidance velocity. Disable only for enemies whose authored locomotion
	 * must face their instantaneous movement direction.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Facing")
	bool bStabilizeCrowdFacing = true;

	/** Maximum yaw turn rate used while smoothly facing the controller's focus. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Movement|Facing", meta=(EditCondition="bStabilizeCrowdFacing", ClampMin="0.0", Units="deg/s"))
	float StableFacingRotationRate = 540.0f;

	/** Remember the spawn or "home base" location for patrolling. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category = "AI")
	FVector HomeLocation;

	/** The current target actor that this AI is engaged with (if any). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category = "AI")
	TObjectPtr<AActor> CurrentTarget;

	/** Shows whether CurrentTarget came from normal hostile acquisition or survival objective arbitration. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI")
	EAeyerjiEnemyTargetSource CurrentTargetSource = EAeyerjiEnemyTargetSource::None;

	/** Defendable survival objective assigned by the owning spawner/director. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Objective")
	TObjectPtr<AActor> DefenseObjectiveTarget;

	/** Runtime player-vs-objective selection settings assigned by the owning spawner/director. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="AI|Objective")
	FAeyerjiDefenseTargetingSettings DefenseTargetingSettings;

	/** Most recent hostile actor this AI positively tracked, even after CurrentTarget is cleared. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI")
	TWeakObjectPtr<AActor> LastKnownTargetActor;

	/** Cached world-space location used when chasing a target after losing direct perception. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI")
	FVector LastKnownTargetLocation = FVector::ZeroVector;

	/** Timestamp of the last successful last-known-target update. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI")
	double LastKnownTargetTime = -1.0;

	/** True while the controller has a usable last-known-target location cached. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI")
	bool bHasLastKnownTarget = false;

	// Accessor for target (used by tasks/conditions)
	AActor* GetTargetActor() const { return CurrentTarget.Get(); }
	EAeyerjiEnemyTargetSource GetCurrentTargetSource() const { return CurrentTargetSource; }
	AActor* GetDefenseObjectiveTargetActor() const { return DefenseObjectiveTarget.Get(); }
	const FAeyerjiDefenseTargetingSettings& GetDefenseTargetingSettings() const { return DefenseTargetingSettings; }
	// Accessor for home location
	FVector GetHomeLocation() const { return HomeLocation; }
	AActor* GetLastKnownTargetActor() const { return LastKnownTargetActor.Get(); }
	const FVector& GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }
	double GetLastKnownTargetTime() const { return LastKnownTargetTime; }
	bool HasLastKnownTarget() const { return bHasLastKnownTarget; }
	bool IsPermanentRiftPursuit() const { return bPermanentRiftPursuit; }

	void ClearLastKnownTarget();

	/** Clears transient target/perception state before a pooled enemy is checked out again. */
	void ResetForPooledReuse(const FVector& NewHomeLocation);

	/**
	 * Releases scarce Detour Crowd slots while this enemy cannot move and reacquires one on wake.
	 * Falls back to normal path following plus CharacterMovement RVO if Crowd has no valid slot.
	 */
	bool SetPathFollowingGameplayEnabled(bool bEnabled, const TCHAR* Reason);

	/**
	 * Restarts the editor-assigned StateTree when pooling or encounter activation left it stopped.
	 * An already-running paused tree remains paused so encounter LOD retains control. A stopped tree
	 * is restarted and explicitly resumed because UE 5.8 StopLogic preserves its prior pause flag.
	 */
	bool EnsureConfiguredStateTreeRunning(const TCHAR* ActivationReason);

	/** Keeps this controller in combat pursuit; its Rift spawner continuously supplies the nearest live player. */
	void SetPermanentRiftPursuit(bool bEnabled) { if (HasAuthority()) { bPermanentRiftPursuit = bEnabled; } }

    UFUNCTION(BlueprintCallable, Category="Targeting")
	void SetTargetActor(AActor* NewTarget);

	/** Assigns the survival defense objective used by StateTree targeting conditions. */
	UFUNCTION(BlueprintCallable, Category="Targeting")
	void SetDefenseObjectiveTargetActor(AActor* NewTarget);

	/** Assigns the survival defense objective and runtime player threat thresholds. */
	UFUNCTION(BlueprintCallable, Category="Targeting")
	void ConfigureDefenseObjectiveTargeting(AActor* NewTarget, const FAeyerjiDefenseTargetingSettings& TargetingSettings);

	/** Validates and assigns a combat target, then optionally alerts nearby allies. */
	bool TryAcquireTarget(AActor* NewTarget, bool bBroadcastAllyAlert);

	/** Returns true when the defense-objective rules allow this actor to override the objective target. */
	bool ShouldAcquireTargetWithDefenseObjective(AActor* Candidate) const;

	/** Re-applies defense-objective targeting so enemies attack the tree unless a player is an active nearby threat. */
	bool RefreshDefenseObjectiveTarget(bool bSendTargetAcquiredEvent = true, bool bStopCurrentMovement = true);

	/** Temporarily treats a valid hostile damage instigator as the preferred defense target. Server only. */
	void NotifyDamagedBy(AActor* DamageInstigator);

	/** Sends a server-side crowd-control presentation event into the running StateTree. */
	void SendAICrowdControlEvent(const FGameplayTag& EventTag);

	/**
	 * Applies the persistent server-side sprint/recovery cadence for a live chase.
	 * StateTree movement tasks supply target distance and attack range, while this controller
	 * owns the absolute-time phase so Attack/Recover/Pressure transitions cannot reset it.
	 */
	void UpdateChaseSprintCadence(float DistanceToTarget, float AttackRange, float DeltaTime, bool bForceImmediateSpeed);

	/**
	 * Server-only chase exit hook that prevents RunSpeed leaking into Attack/Recover.
	 * An active sprint becomes a recovery phase; existing recovery timestamps are preserved.
	 */
	void EndChaseSprintCadence();

#if WITH_DEV_AUTOMATION_TESTS
	/** Deterministic policy seam used by automation without requiring a ticking world. */
	float ResolveChaseCadenceSpeedForAutomation(
		float DistanceToTarget,
		float AttackRange,
		double CurrentTimeSeconds,
		float WalkSpeed,
		float RunSpeed);
	void ResetChaseSprintCadenceForAutomation();
	void EndChaseSprintCadenceForAutomation(double CurrentTimeSeconds);
	bool IsChaseSprintingForAutomation() const { return bChaseSprintActive; }
	double GetChaseSprintRecoveryEndTimeForAutomation() const { return ChaseSprintRecoveryEndTime; }
#endif

protected:
	virtual void PostLoad() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION()
	void OnPerceptionUpdated(const TArray<AActor*>& Actors);   // keep

	UFUNCTION()
	void OnTargetPerception(AActor* Actor, FAIStimulus Stimulus);  // keep



private:
	/** Stops, assigns, and starts the configured tree in a lifecycle-safe order. */
	void RestartConfiguredStateTree(const TCHAR* StopReason);
	bool IsTargetValidForAcquisition(AActor* Candidate) const;
	AActor* FindBestDefenseThreatTarget() const;
	bool IsRecentDamageThreatValid() const;
	void AssignCurrentTarget(AActor* NewTarget, EAeyerjiEnemyTargetSource NewSource, bool bSendTargetAcquiredEvent, bool bStopCurrentMovement);
	void RememberTargetLocation(AActor* Target);
	/** Applies the controller-facing policy after possession so Blueprint pawn defaults cannot reintroduce crowd-yaw jitter. */
	void ApplyStableCrowdFacingPolicy();
	/** Clears the transient cadence whenever a controller is possessed or an enemy is recycled. */
	void ResetChaseSprintCadence();
	/** Converts the current sprint into a recovery phase at the supplied authoritative time. */
	void BeginChaseSprintRecovery(double CurrentTimeSeconds);
	/** Advances the deterministic cadence state and returns the requested attribute-derived speed. */
	float ResolveChaseCadenceSpeed(
		float DistanceToTarget,
		float AttackRange,
		double CurrentTimeSeconds,
		float WalkSpeed,
		float RunSpeed);
	/** Migrates old property-driven perception defaults into the authoritative sense configs. */
	void ApplyLegacyPerceptionPropertyOverrides();
	/** Mirrors the authoritative configs into hidden legacy fields so older serialized assets stay coherent. */
	void SyncDeprecatedPerceptionPropertiesFromConfigs();

    /* We own perception so it's never null                                */
    UPROPERTY(VisibleAnywhere, Category="AI")
    TObjectPtr<UAIPerceptionComponent> Perception;
    
    /** Authoritative sight config edited by Blueprint subclasses. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception", meta=(AllowPrivateAccess="true"), Instanced)
    TObjectPtr<UAISenseConfig_Sight> SightSenseConfig;

    /** Authoritative hearing config edited by Blueprint subclasses. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception", meta=(AllowPrivateAccess="true"), Instanced)
    TObjectPtr<UAISenseConfig_Hearing> HearingSenseConfig;
	
    uint8 TeamId = 1; 

    /** Deprecated legacy settings retained only so older Blueprint defaults can migrate cleanly. */
    UPROPERTY()
    float SightRadius = 1500.f;

    UPROPERTY()
    float LoseSightRadius = 2500.f;

    UPROPERTY()
    float PeripheralVisionAngleDegrees = 55.f;

    UPROPERTY()
    bool bDetectEnemies = true;

    UPROPERTY()
    bool bDetectFriendlies = false;

    UPROPERTY()
    bool bDetectNeutrals = false;
    
    UPROPERTY()
    float HearingRange = 1800.f;

    UPROPERTY()
    float LoSHearingRange = 2400.f;
    
    /** Deprecated migration flag from the old property-driven perception path. */
    UPROPERTY()
    bool bOverridePerceptionWithProperties = false;

	/** Short-lived attacker override used so defense enemies can peel to players that directly damage them. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> RecentDamageThreat;

	/** Runtime-only pursuit mode; target loss never falls back to patrol while a valid participant remains. */
	bool bPermanentRiftPursuit = false;

	/** Server world time when RecentDamageThreat stops overriding normal objective targeting. */
	UPROPERTY(Transient)
	double RecentDamageThreatExpiryTime = -1.0;

	/** True only during the bounded RunSpeed phase of the shared chase cadence. */
	UPROPERTY(Transient)
	bool bChaseSprintActive = false;

	/** Server world time when the current RunSpeed phase must end. */
	UPROPERTY(Transient)
	double ChaseSprintEndTime = -1.0;

	/** Server world time before which another far-chase sprint cannot begin. */
	UPROPERTY(Transient)
	double ChaseSprintRecoveryEndTime = -1.0;

public:
    /** Reconfigures the live perception component from the authoritative sense configs. */
    UFUNCTION(BlueprintCallable, Category="AI|Perception")
    void ApplyPerceptionSettings();
    
};
