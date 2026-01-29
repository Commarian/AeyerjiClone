// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "AeyerjiGameState.generated.h"

class AAeyerjiLevelDirector;
class AAeyerjiSpawnerGroup;

UENUM(BlueprintType)
enum class EAeyerjiRunState : uint8
{
	PreRun        UMETA(DisplayName="PreRun"),
	InRun         UMETA(DisplayName="InRun"),
	BossDefeated  UMETA(DisplayName="BossDefeated"),
	RunComplete   UMETA(DisplayName="RunComplete"),
	ReturnToMenu  UMETA(DisplayName="ReturnToMenu")
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRunResults
{
	GENERATED_BODY()

	/** Monotonic counter used to safely gate local "results ready" broadcasts across replication order differences. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 ResultsVersion = 0;

	/** True when the run ended via defeating the boss (as opposed to aborting early). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	bool bBossDefeated = false;

	/** Total run time captured from the LevelDirector timer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float RunTimeSeconds = 0.f;

	/** Total shards collected during the run (if a LevelDirector is present). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 ShardsCollected = 0;

	/** Difficulty slider snapshot (0..1000) used for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float DifficultySlider = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiRunStateChangedSignature, EAeyerjiRunState, NewState, EAeyerjiRunState, OldState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiRunResultsReadySignature, const FAeyerjiRunResults&, Results);

/**
 * Networked run state machine (authoritative on the server, replicated to clients).
 * This is the MVP spine for starting/ending a run, showing results, and returning to the main menu cleanly.
 */
UCLASS(BlueprintType)
class AEYERJI_API AAeyerjiGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AAeyerjiGameState();

	/** Initializes the run state machine and binds to the LevelDirector when running on the server. */
	virtual void BeginPlay() override;

	/** Replication descriptor for RunState and RunResults. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Current run state (replicated). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run")
	EAeyerjiRunState GetRunState() const { return RunState; }

	/** Latest run results snapshot (replicated). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run")
	const FAeyerjiRunResults& GetRunResults() const { return RunResults; }

	/**
	 * Server-only: starts the run and transitions PreRun -> InRun.
	 * If a LevelDirector is present, it also calls StartRun() on it.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_StartRun();

	/**
	 * Server-only: marks the boss as defeated and transitions InRun -> BossDefeated.
	 * This typically comes from the boss spawner clearing.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_NotifyBossDefeated();

	/**
	 * Server-only: finalizes results and transitions BossDefeated/InRun -> RunComplete.
	 * Intended to be called after any "Boss Defeated" stinger.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_MarkRunComplete();

	/**
	 * Server-only: transitions RunComplete -> ReturnToMenu.
	 * Clients will remove UI and travel back to the main menu map.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_ReturnToMenu();

	/**
	 * Server-only: deterministic escape hatch for the MVP.
	 * From any state, ends the run (if needed), marks results, and returns to menu.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_ForceEndRunAndReturnToMenu();

public:
	/** Fired whenever the run state changes (server + clients). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiRunStateChangedSignature OnRunStateChanged;

	/** Fired when a RunComplete results snapshot is available (server + clients). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiRunResultsReadySignature OnRunResultsReady;

	/** Travel URL for the main menu map (e.g. /Game/Maps/MainMenu). If empty, uses GameInstance->ReturnToMainMenu(). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Travel")
	FString MainMenuTravelURL = "/Game/Levels/L_MainMenu";

	/** Optional delay (seconds) between BossDefeated and RunComplete. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Flow", meta=(ClampMin="0.0"))
	float BossDefeatedToCompleteDelay = 1.25f;

	/** Optional delay (seconds) after RunComplete before auto returning to menu. Set 0 to require manual return. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Flow", meta=(ClampMin="0.0"))
	float AutoReturnToMenuDelay = 0.f;

protected:
	/** Replicated run state; drives UI and travel. */
	UPROPERTY(ReplicatedUsing=OnRep_RunState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	EAeyerjiRunState RunState = EAeyerjiRunState::PreRun;

	/** RepNotify for RunState; broadcasts delegates and triggers client travel where appropriate. */
	UFUNCTION()
	void OnRep_RunState(EAeyerjiRunState OldState);

	/** Replicated results snapshot; updated at BossDefeated/RunComplete. */
	UPROPERTY(ReplicatedUsing=OnRep_RunResults, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	FAeyerjiRunResults RunResults;

	/** RepNotify for RunResults; fires OnRunResultsReady on clients when results replicate. */
	UFUNCTION()
	void OnRep_RunResults();

protected:
	/** Binds to LevelDirector delegates (run start and boss spawner clear) if one exists in the world. */
	void BindToLevelDirector();

	/** Captures a results snapshot from the LevelDirector, if present. */
	void SnapshotRunResults(bool bBossDefeated);

	/** Broadcasts OnRunResultsReady once when in RunComplete and results are available. */
	void MaybeBroadcastRunResults();

	/** Moves the state machine to a new state (server-only) and runs state entry side effects. */
	bool SetRunState(EAeyerjiRunState NewState);

	/** Returns true if the transition is allowed for the MVP flow. */
	bool CanTransitionTo(EAeyerjiRunState NewState) const;

	/** Runs state entry effects and broadcasts OnRunStateChanged for local listeners. */
	void HandleRunStateChanged(EAeyerjiRunState OldState);

	/** Client-side cleanup + travel used when entering ReturnToMenu. */
	void CleanupUIAndReturnToMenu();

protected:
	/** Server hook: LevelDirector notified that it started/stopped a run. */
	UFUNCTION()
	void HandleLevelDirectorRunActiveChanged(bool bIsRunning);

	/** Server hook: boss spawner group finished. */
	UFUNCTION()
	void HandleBossSpawnerCleared(AAeyerjiSpawnerGroup* Spawner);

	/** Timer callback to advance BossDefeated -> RunComplete. */
	void HandleBossDefeatedDelayElapsed();

	/** Timer callback to auto-advance RunComplete -> ReturnToMenu. */
	void HandleAutoReturnDelayElapsed();

private:
	TWeakObjectPtr<AAeyerjiLevelDirector> CachedLevelDirector;
	TWeakObjectPtr<AAeyerjiSpawnerGroup> CachedBossSpawner;

	FTimerHandle BossDefeatedDelayHandle;
	FTimerHandle AutoReturnDelayHandle;

	/** Server-only counter used to stamp RunResults.ResultsVersion. */
	int32 NextResultsVersion = 1;

	/** Local gate to prevent duplicate results broadcasts. */
	int32 LastBroadcastResultsVersion = 0;
};
