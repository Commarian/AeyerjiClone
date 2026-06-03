// EnemyAIController.h
#pragma once
#include "CoreMinimal.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Components/StateTreeComponent.h"
#include "EnemyAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
struct FPropertyChangedEvent;

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

	/** Remember the spawn or "home base" location for patrolling. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category = "AI")
	FVector HomeLocation;

	/** The current target actor that this AI is engaged with (if any). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category = "AI")
	TObjectPtr<AActor> CurrentTarget;

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
	AActor* GetTargetActor() const { return CurrentTarget; }
	// Accessor for home location
	FVector GetHomeLocation() const { return HomeLocation; }
	AActor* GetLastKnownTargetActor() const { return LastKnownTargetActor.Get(); }
	const FVector& GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }
	double GetLastKnownTargetTime() const { return LastKnownTargetTime; }
	bool HasLastKnownTarget() const { return bHasLastKnownTarget; }

	void ClearLastKnownTarget();

    UFUNCTION(BlueprintCallable, Category="Targeting")
	void SetTargetActor(AActor* NewTarget) { CurrentTarget = NewTarget;}

	/** Validates and assigns a combat target, then optionally alerts nearby allies. */
	bool TryAcquireTarget(AActor* NewTarget, bool bBroadcastAllyAlert);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetActor;

protected:
	virtual void PostLoad() override;
	virtual void OnPossess(APawn* InPawn) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION()
	void OnPerceptionUpdated(const TArray<AActor*>& Actors);   // keep

	UFUNCTION()
	void OnTargetPerception(AActor* Actor, FAIStimulus Stimulus);  // keep



private:
	bool IsTargetValidForAcquisition(AActor* Candidate) const;
	void RememberTargetLocation(AActor* Target);
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

public:
    /** Reconfigures the live perception component from the authoritative sense configs. */
    UFUNCTION(BlueprintCallable, Category="AI|Perception")
    void ApplyPerceptionSettings();
    
};
