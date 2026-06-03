#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "AeyerjiWorldDirector.generated.h"

class UStateTreeComponent;
class UStateTree;
struct FAeyerjiWorldStateEntry;

/**
 * Thin persistent-map bootstrap actor that triggers startup zone streaming on begin play.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiWorldDirector : public AActor
{
	GENERATED_BODY()

public:
	/** Creates a non-ticking bootstrap actor used only for startup orchestration. */
	AAeyerjiWorldDirector();

protected:
	/** Boots startup flow for the persistent root world. */
	virtual void BeginPlay() override;

	/** Clears any scheduled run-director work before the actor leaves the world. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Executes startup flow on the next tick so GameState/world-flow systems are initialized first. */
	void ExecuteDeferredStartupFlow();

	/** Seeds the run-director test state and binds StateTree evaluation only on authority. */
	void HandleRunDirectorStateTreeBeginPlay();

	/** Runs one server-side StateTree evaluation when relevant run facts change. */
	void EvaluateRunDirectorStateTree(FGameplayTag TriggerTag = FGameplayTag(), const FString& Reason = FString());

	/** Loads and assigns the configured StateTree asset before evaluation begins. */
	bool AssignRunDirectorStateTreeAsset();

	/** Reacts to relevant world-state changes by trying the run director StateTree once. */
	void HandleRunDirectorWorldStateChanged(const FAeyerjiWorldStateEntry& Entry);

	/** Subscribes to world-state changes that can unlock run director decisions. */
	void BindRunDirectorWorldStateEvents();

	/** Removes the run director world-state subscription. */
	void UnbindRunDirectorWorldStateEvents();

	/** Returns true while the level 60 test is eligible and has not yet been gated as done. */
	bool ShouldEvaluateRunDirectorLevel60Test() const;

	/** Returns true when a world-state entry should wake the run-director StateTree. */
	bool ShouldEvaluateForWorldStateEntry(const FAeyerjiWorldStateEntry& Entry) const;

public:
	/** Server-owned StateTree runner for run-level decisions and one-shot run events. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run Director")
	TObjectPtr<UStateTreeComponent> RunDirectorStateTree;

	/** Returns the most recent reason this actor tried to evaluate ST_RunDirector. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run Director|Debug")
	FString GetLastRunDirectorEvaluationReason() const { return LastRunDirectorEvaluationReason; }

	/** Returns the gameplay tag that most recently woke ST_RunDirector, if any. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run Director|Debug")
	FGameplayTag GetLastRunDirectorEvaluationTag() const { return LastRunDirectorEvaluationTag; }

	/** Default StateTree asset assigned to RunDirectorStateTree before authority starts logic. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director")
	TSoftObjectPtr<UStateTree> RunDirectorStateTreeAsset;

	/** If true, authority logs run-director evaluation and gate state for this first test. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director")
	bool bLogRunDirectorEvaluation = true;

	/** If true, any Run.* fact can wake the run director; disable to use only WatchedRunDirectorTags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director")
	bool bEvaluateOnAnyRunWorldState = true;

	/** Explicit non-Run facts that should also wake the run director, for example persistent boss/world gates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director")
	FGameplayTagContainer WatchedRunDirectorTags;

	/** Keeps the original Run.Level >= threshold proof gate until ST_RunDirector is fully authored. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director|Proof")
	bool bUseLevelProofGate = true;

	/** Seeds Run.Level on BeginPlay for the original proof path. Disable once real run progression owns Run.Level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director|Proof")
	bool bSeedLevelProofState = true;

	/** Run.Level value seeded by the proof path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director|Proof", meta=(ClampMin="1"))
	int32 ProofRunLevel = 60;

	/** Minimum Run.Level required by the proof gate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run Director|Proof", meta=(ClampMin="1"))
	int32 ProofRunLevelThreshold = 60;

	/** Explicit startup zone id used when entering gameplay from this persistent map. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName StartZoneId = NAME_None;

	/** If true, startup flow prefers subsystem saved LastZoneId over StartZoneId. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bPreferSavedZone = true;

	/** If true, BeginPlay will call EnterStartupZone(). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bEnterZoneOnBeginPlay = true;

	/** If true, startup goes through GameState world-flow transitions instead of direct EnterStartupZone(). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bUseServerWorldFlow = true;

	/** If true, only authority/standalone instances execute BeginPlay bootstrap logic. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bOnlyRunOnAuthority = true;

private:
	/** Native world-state delegate handle for event-driven run director evaluation. */
	FDelegateHandle RunDirectorWorldStateChangedHandle;

	/** Last debug reason captured before starting ST_RunDirector. */
	FString LastRunDirectorEvaluationReason;

	/** Last world-state tag that woke ST_RunDirector. */
	FGameplayTag LastRunDirectorEvaluationTag;
};
