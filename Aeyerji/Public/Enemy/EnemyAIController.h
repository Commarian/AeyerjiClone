// EnemyAIController.h
#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Components/StateTreeComponent.h"
#include "EnemyAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;

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

	// Accessor for target (used by tasks/conditions)
	AActor* GetTargetActor() const { return CurrentTarget; }
	// Accessor for home location
	FVector GetHomeLocation() const { return HomeLocation; }

    UFUNCTION(BlueprintCallable, Category="Targeting")
	void SetTargetActor(AActor* NewTarget) { CurrentTarget = NewTarget;}

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetActor;

protected:
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION()
	void OnPerceptionUpdated(const TArray<AActor*>& Actors);   // keep

	UFUNCTION()
	void OnTargetPerception(AActor* Actor, FAIStimulus Stimulus);  // keep



private:
    /* We own perception so it's never null                                */
    UPROPERTY(VisibleAnywhere, Category="AI")
    TObjectPtr<UAIPerceptionComponent> Perception;
    
    /** Sense configs are editable on BP so designers can tune directly. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception", meta=(AllowPrivateAccess="true"), Instanced)
    TObjectPtr<UAISenseConfig_Sight> SightSenseConfig;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception", meta=(AllowPrivateAccess="true"), Instanced)
    TObjectPtr<UAISenseConfig_Hearing> HearingSenseConfig;
	
    uint8 TeamId = 1; 

public:
    /** Shared sight parameters for all enemies using this controller. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    float SightRadius = 1500.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    float LoseSightRadius = 2500.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    float PeripheralVisionAngleDegrees = 55.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    bool bDetectEnemies = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    bool bDetectFriendlies = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    bool bDetectNeutrals = false;
    
    /** Hearing sense (optional) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception|Hearing")
    float HearingRange = 1800.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception|Hearing")
    float LoSHearingRange = 2400.f;
    
    /** When true, the float properties above override the BP Sense Config subobjects at runtime. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI|Perception")
    bool bOverridePerceptionWithProperties = false;
    
    /** Apply current UPROPERTY values to the sense configs at runtime. */
    UFUNCTION(BlueprintCallable, Category="AI|Perception")
    void ApplyPerceptionSettings();
    
};
