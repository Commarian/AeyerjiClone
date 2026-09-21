#pragma once

#include "CoreMinimal.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "GameFramework/Actor.h"
#include "Items/InventoryComponent.h"
#include "AeyerjiCombatBalanceTestHarness.generated.h"

class AAeyerjiPlayerController;
class AEnemyParentNative;
class APawn;
class UAbilitySystemComponent;
class UAeyerjiAttributeSet;
class UAeyerjiInventoryComponent;
struct FOnAttributeChangeData;
struct FAeyerjiEnemyXPAward;

DECLARE_LOG_CATEGORY_EXTERN(LogAeyerjiCombatTest, Log, All);

/** Discrete authority observations; damage applications do not imply effective HP loss. */
enum class EAeyerjiBombardmentTelemetryEvent : uint8
{
	Started,
	Impacted,
	Cancelled
};

/** Authority-observed targeting transitions used to measure combat responsiveness. */
enum class EAeyerjiCombatTargetingTelemetryEvent : uint8
{
	PlayerTargetHandoff,
	PlayerPrimaryActivated,
	EnemyDeadTargetCleared,
	InvalidPrimaryActivationPrevented,
	EnemyLeashTargetCleared
};

/** Runtime phase of one deterministic combat-balance test. */
UENUM(BlueprintType)
enum class EAeyerjiCombatTestState : uint8
{
	Spawning,
	Ready,
	Running,
	Completed
};

/**
 * Sanitized server request used to build one deterministic combat-balance test.
 * These values are created by console commands rather than filled from an editor asset.
 */
USTRUCT()
struct AEYERJI_API FAeyerjiCombatTestRequest
{
	GENERATED_BODY()

	FName ScenarioName = TEXT("Dense24");
	FName CompositionName = TEXT("Mixed");
	int32 EnemyCount = 24;
	int32 EnemyLevel = 1;
	int32 WorldTier = 167;
	int32 Seed = 1337;
	float MinimumSpawnRadius = 1200.f;
	float MaximumSpawnRadius = 2600.f;
	float SpawnInterval = 0.05f;
	float AutoEngageDelay = 3.f;
	bool bApplyAggro = true;
};

/**
 * Transient, server-authoritative combat-balance test runner.
 *
 * It deliberately spawns through AAeyerjiSpawnerGroup so GAS initialization,
 * difficulty scaling, elite promotion, AI possession, StateTree startup, death
 * tracking, and replication use the production paths. Replicated summary fields
 * provide a lightweight client HUD while detailed samples are written by authority.
 */
UCLASS(NotPlaceable, Transient)
class AEYERJI_API AAeyerjiCombatBalanceTestHarness : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiCombatBalanceTestHarness();

	static constexpr int32 MaximumTestEnemyCount = 48;
	static const FName TestEnemyActorTag;

	/** Finds the transient harness for this world, including a completed test awaiting cleanup. */
	static AAeyerjiCombatBalanceTestHarness* FindForWorld(const UWorld* World);

	/**
	 * Records a fixed ground warning and its terminal outcome in the source's running world harness.
	 * AttackId must identify one cast uniquely for this source across pooling. Only casts whose start
	 * was observed are counted. Aim/source labels and geometry are frozen at Started, surviving respawn.
	 * CandidateCount is eligible impact targets; DamageApplications counts submitted damage specs,
	 * not hits, HP loss, or avoided zones. Unknown outcome counts use INDEX_NONE.
	 */
	static void RecordBombardmentEvent(
		AActor* Source,
		AActor* AimTarget,
		uint32 AttackId,
		EAeyerjiBombardmentTelemetryEvent Phase,
		const FVector& Center,
		float Radius,
		int32 CandidateCount = INDEX_NONE,
		int32 DamageApplications = INDEX_NONE);

	/** Records one bounded authority-side targeting transition in the active world harness. */
	static void RecordTargetingEvent(
		AActor* Source,
		AActor* PreviousTarget,
		AActor* NewTarget,
		EAeyerjiCombatTargetingTelemetryEvent Event,
		FName Reason,
		uint32 CommandSerial = 0);

	/** Resolves one of the canonical population-ladder presets without touching world state. */
	static bool ResolvePreset(const FString& PresetName, FAeyerjiCombatTestRequest& OutRequest, FString& OutError);

	/** Validates and clamps a custom request to the supported deterministic test envelope. */
	static bool SanitizeRequest(FAeyerjiCombatTestRequest& InOutRequest, FString& OutError);

	/** Starts a preset after removing only an earlier combat-test harness and its tagged actors. */
	static bool StartPreset(
		UWorld* World,
		AAeyerjiPlayerController* RequestingController,
		const FString& PresetName,
		int32 EnemyLevel,
		int32 WorldTier,
		int32 Seed,
		float AutoEngageDelay,
		FString& OutMessage);

	/** Starts a caller-defined composition/count test after request validation. */
	static bool StartCustom(
		UWorld* World,
		AAeyerjiPlayerController* RequestingController,
		FAeyerjiCombatTestRequest Request,
		FString& OutMessage);

	/** Starts a measurement-only session over normal production Rift enemies without spawning or owning them. */
	static bool StartProductionObservation(
		UWorld* World,
		AAeyerjiPlayerController* RequestingController,
		const FString& RunLabel,
		FString& OutMessage);

	/** Shared safe rate calculation used by reports and deterministic automation tests. */
	static float CalculateCountRate(int32 Count, float ElapsedSeconds);

	/** Arrival rate minus kill rate, where positive values mean pressure is accumulating. */
	static float CalculateNetPressure(float ArrivalRatePerSecond, float KillsPerTenSeconds);

	/** Returns frame time above the hitch threshold in milliseconds, sanitized for report use. */
	static float CalculateHitchOverageMilliseconds(float FrameDeltaSeconds, float ThresholdSeconds);

	/** Server-only initialization after the transient harness actor has spawned. */
	bool StartTest(const FAeyerjiCombatTestRequest& Request, AAeyerjiPlayerController* RequestingController, FString& OutError);

	/** Releases all prepared enemies together and begins metric collection. */
	bool Engage(FString& OutMessage);

	/** Adds an explicit trace/report marker without changing combat state. */
	void AddManualMarker(const FString& Label);

	/** Exports the current result and optionally destroys only actors created by this harness. */
	void StopTest(bool bCleanupSpawnedActors = true);

	/** Server/global summary using authoritative population and damage metrics. */
	FString BuildSummary() const;

	/** Client-local summary which adds viewport-visible enemy counts. */
	FString BuildLocalSummary(APlayerController* LocalController) const;

	/** Enables or disables the lightweight local overlay for this process. */
	static void SetLocalHUDEnabled(bool bEnabled);

	/** Returns whether the lightweight local overlay is enabled for this process. */
	static bool IsLocalHUDEnabled();

	/** Returns the canonical preset names in population-ladder order. */
	static FString GetPresetList();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Canonical scenario identifier, such as Dense24 or Elite24. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	FName ScenarioName = NAME_None;

	/** Composition recipe used to build the deterministic roster. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	FName CompositionName = NAME_None;

	/** Current server-owned phase of the test. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	EAeyerjiCombatTestState TestState = EAeyerjiCombatTestState::Spawning;

	/** Explicit enemy level applied by the test spawner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemyLevel = 1;

	/** Explicit world tier used for the test's global stat budget. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 WorldTier = 167;

	/** Deterministic roster and annulus-layout seed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 TestSeed = 1337;

	/** Requested population after input validation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 RequestedEnemyCount = 0;

	/** Enemies successfully created through the production spawner path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 SpawnedEnemyCount = 0;

	/** Spawn requests rejected by class loading, navigation, or actor creation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 FailedSpawnCount = 0;

	/** Living test enemies in the latest authoritative sample. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 AliveEnemyCount = 0;

	/** Living enemies currently allowed to participate in combat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 CombatActiveEnemyCount = 0;

	/** Living enemies within 1000 cm of the tested player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemiesWithin1000 = 0;

	/** Living enemies within 2000 cm of the tested player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemiesWithin2000 = 0;

	/** Living enemies within 4000 cm of the tested player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemiesWithin4000 = 0;

	/** Living enemies within the Rift pressure radius of 8000 cm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemiesWithin8000 = 0;

	/** Kills observed from the authoritative enemy-death delegates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 EnemyKills = 0;

	/** Player pressure-relief rate normalized to ten seconds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PlayerKillsPerTenSeconds = 0.f;

	/** Enemies first observed combat-active within 2000 cm after engagement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 ThreateningArrivalCount = 0;

	/** First-time threatening arrivals divided by elapsed combat seconds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float ThreateningArrivalsPerSecond = 0.f;

	/** Living, combat-active enemies currently within 2000 cm. This is measured, never capped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 ThreateningEnemyCount = 0;

	/** Unique authoritative damage instigators that hit the player during the latest rolling second. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 UniqueHittersOneSecond = 0;

	/** Seconds since combat was released; zero while assembling the test. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float ElapsedCombatSeconds = 0.f;

	/** Latest authoritative player health. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PlayerHealth = 0.f;

	/** Player maximum health captured when the test was prepared. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PlayerMaxHealth = 0.f;

	/** Player basic attack damage captured when the test was prepared. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PlayerAttackDamage = 0.f;

	/** Lowest authoritative player health observed after engagement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float LowestPlayerHealth = 0.f;

	/** Total negative HP delta observed after engagement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float TotalIncomingDamage = 0.f;

	/** Total incoming damage divided by elapsed combat time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float AverageIncomingDPS = 0.f;

	/** Total effective HP restored during the recording, excluding respawn initialization. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float TotalHealing = 0.f;

	/** Total effective healing divided by elapsed recording time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float AverageHealingPerSecond = 0.f;

	/** Player deaths observed during a continuous production Rift recording. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 PlayerDeathCount = 0;

	/** Replacement player pawns rebound after death during a continuous production Rift recording. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	int32 PlayerRespawnCount = 0;

	/** Latest authoritative player progression level. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 CurrentPlayerLevel = 1;

	/** Latest current and required XP for the active player pawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerXP = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerXPMax = 0.f;

	/** Live combat stats after level and equipment modifiers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerArmor = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerDodgeChance = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerHPRegen = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float CurrentPlayerRunSpeed = 0.f;

	/** Current active-enemy live GAS level range and average. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ActiveEnemyLevelMin = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ActiveEnemyLevelMax = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float ActiveEnemyLevelAverage = 0.f;

	/** Frozen spawn/scaling level range retained for comparison with live enemy progression. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ActiveEnemySpawnLevelMin = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ActiveEnemySpawnLevelMax = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	float ActiveEnemySpawnLevelAverage = 0.f;

	/** Current inventory/equipment counts and run-local acquisition/equip events. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 InventoryItemCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 EquippedItemCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ItemsAddedDuringRun = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test|Progression")
	int32 ItemsEquippedDuringRun = 0;

	/** Maximum damage observed inside any rolling 0.5-second window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PeakDamageHalfSecond = 0.f;

	/** Maximum damage observed inside any rolling 1-second window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PeakDamageOneSecond = 0.f;

	/** Maximum damage observed inside any rolling 3-second window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float PeakDamageThreeSeconds = 0.f;

	/** Deterministic annulus center captured from the tested player at setup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	FVector TestCenter = FVector::ZeroVector;

	/** Inner edge of the deterministic spawn annulus. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float MinimumSpawnRadius = 0.f;

	/** Outer edge of the deterministic spawn annulus. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	float MaximumSpawnRadius = 0.f;

	/** False when the canonical level-one naked player stats do not match the live ASC. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	bool bCanonicalPlayerBaseline = false;

	/** Result reason such as Cleared, PlayerDead, ManualStop, or SetupFailed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	FName CompletionReason = NAME_None;

	/** True when this actor observes normal Rift enemies and must never clean them up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Combat Test")
	bool bObservingProductionRift = false;

protected:
	virtual void BeginPlay() override;

private:
	friend class FAeyerjiBombardmentTelemetryTest;
	friend class FAeyerjiTargetingTelemetryTest;

	struct FBombardmentEvent
	{
		float TimeSeconds = 0.f;
		EAeyerjiBombardmentTelemetryEvent Phase = EAeyerjiBombardmentTelemetryEvent::Started;
		uint32 AttackId = 0;
		FString Source;
		FString Archetype;
		FString AimTarget;
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		int32 CandidateCount = INDEX_NONE;
		int32 DamageApplications = INDEX_NONE;
		int32 ActiveZones = 0;
		int32 PeakActiveZones = 0;
		int32 TotalStarts = 0;
		int32 TotalImpacts = 0;
		int32 TotalCancellations = 0;
		int32 TotalDamageApplications = 0;
	};

	struct FDamageEvent
	{
		float TimeSeconds = 0.f;
		float Amount = 0.f;
		FString Source;
		FString Archetype;
		FString DamageType;
		TWeakObjectPtr<AActor> SourceActor;
	};

	struct FHealingEvent
	{
		float TimeSeconds = 0.f;
		float Amount = 0.f;
		FString SourceType;
		FString Instigator;
		FString SourceObject;
	};

	struct FPopulationSample
	{
		float TimeSeconds = 0.f;
		int32 Alive = 0;
		int32 CombatActive = 0;
		int32 Within1000 = 0;
		int32 Within2000 = 0;
		int32 Within4000 = 0;
		int32 Within8000 = 0;
		int32 Threatening = 0;
		int32 UniqueHittersOneSecond = 0;
		int32 ThreateningArrivals = 0;
		float PlayerHP = 0.f;
		float TotalDamage = 0.f;
		float TotalHealing = 0.f;
		int32 Kills = 0;
		int32 PlayerDeaths = 0;
		int32 PlayerLevel = 1;
		float PlayerXP = 0.f;
		float PlayerXPMax = 0.f;
		float PlayerHPMax = 0.f;
		float PlayerAttackDamage = 0.f;
		float PlayerArmor = 0.f;
		float PlayerDodgeChance = 0.f;
		float PlayerHPRegen = 0.f;
		float PlayerRunSpeed = 0.f;
		int32 EnemyLevelMin = 0;
		int32 EnemyLevelMax = 0;
		float EnemyLevelAverage = 0.f;
		int32 EnemySpawnLevelMin = 0;
		int32 EnemySpawnLevelMax = 0;
		float EnemySpawnLevelAverage = 0.f;
		int32 InventoryItems = 0;
		int32 EquippedItems = 0;
		float AuthorityFrameDeltaMs = 0.f;
		float SampleIntervalMs = 0.f;
		int32 BombardmentActiveZones = 0;
		int32 BombardmentPeakActiveZones = 0;
		int32 BombardmentStarts = 0;
		int32 BombardmentImpacts = 0;
		int32 BombardmentCancellations = 0;
		int32 BombardmentDamageApplications = 0;
	};

	struct FHitchEvent
	{
		float TimeSeconds = 0.f;
		float AuthorityFrameDeltaMs = 0.f;
		float ThresholdOverageMs = 0.f;
		int32 Alive = 0;
		int32 CombatActive = 0;
		int32 Threatening = 0;
		int32 Kills = 0;
		int32 PlayerLevel = 1;
		int32 InventoryItems = 0;
		int32 EquippedItems = 0;
	};

	struct FKillRewardEvent
	{
		float TimeSeconds = 0.f;
		int32 KillIndex = 0;
		FString Enemy;
		FString Archetype;
		int32 EnemyLevel = 1;
		float BaseXP = 0.f;
		float ScaledXPBeforeRoleMultiplier = 0.f;
		float RoleMultiplier = 1.f;
		float KillerMultiplier = 1.f;
		float AwardedXP = 0.f;
		int32 PlayerLevelAfter = 1;
		float PlayerXPAfter = 0.f;
		float PlayerXPMaxAfter = 0.f;
		bool bRecipientWasKiller = false;
		TWeakObjectPtr<AActor> EnemyActor;
	};

	struct FProgressionEvent
	{
		float TimeSeconds = 0.f;
		FString Event;
		int32 PlayerLevel = 1;
		float PlayerXP = 0.f;
		float PlayerXPMax = 0.f;
		float PlayerHPMax = 0.f;
		float PlayerAttackDamage = 0.f;
		float PlayerArmor = 0.f;
		float PlayerDodgeChance = 0.f;
		float PlayerHPRegen = 0.f;
		float PlayerRunSpeed = 0.f;
		int32 EnemyLevelMin = 0;
		int32 EnemyLevelMax = 0;
		float EnemyLevelAverage = 0.f;
		int32 EnemySpawnLevelMin = 0;
		int32 EnemySpawnLevelMax = 0;
		float EnemySpawnLevelAverage = 0.f;
		int32 Kills = 0;
		int32 InventoryItems = 0;
		int32 EquippedItems = 0;
	};

	struct FItemEvent
	{
		float TimeSeconds = 0.f;
		FString Change;
		FString Definition;
		int32 ItemLevel = 0;
		FString Rarity;
		FString Slot;
		int32 SlotIndex = INDEX_NONE;
		int32 PlayerLevel = 1;
	};

	struct FMarkerEvent
	{
		float TimeSeconds = 0.f;
		FString Label;
	};

	struct FCombatTargetingEvent
	{
		float TimeSeconds = 0.f;
		FString Event;
		FString Source;
		FString PreviousTarget;
		FString NewTarget;
		FString Reason;
		uint32 CommandSerial = 0;
	};

	bool BuildSpawnRoster(FString& OutError);
	bool StartProductionObservationInternal(AAeyerjiPlayerController* RequestingController, const FString& RunLabel, FString& OutError);
	void RefreshObservedProductionEnemies();
	void RefreshObservedProductionPlayer();
	void UpdateProgressionSnapshot(bool bRecordChanges);
	void RecordProgressionEvent(const FString& Event);
	void BindPlayerInventory();
	void UnbindPlayerInventory();
	bool CapturePlayerBaseline(FString& OutError);
	void SpawnNextEnemy();
	FVector ResolveSpawnLocation(int32 SpawnIndex, int32 PlacementAttempt = 0) const;
	void FinishSpawning();
	void UpdateAuthoritativeSnapshot(bool bStoreSample);
	void UpdateLocalHUD(float DeltaSeconds);
	void CompleteTest(FName Reason);
	void EmitMarker(const FString& Label);
	void WriteReports();
	void ResetBombardmentTelemetry();
	FString BuildBombardmentCSV() const;
	FString BuildBombardmentSummary() const;
	void ResetTargetingTelemetry();
	FString BuildTargetingCSV() const;
	FString BuildTargetingSummary() const;
	void CleanupSpawnedActors();
	void BindPlayerHealth();
	void UnbindPlayerHealth();
	void HandlePlayerHealthChanged(const FOnAttributeChangeData& Data);

	/** Receives the authoritative post-mitigation damage event with its original GAS instigator. */
	UFUNCTION()
	void HandlePlayerDamageTaken(AActor* Victim, AActor* DamageInstigator, float DamageTaken, FGameplayTag DamageType);

	/** Receives exact effective healing after HP clamping, with its authoritative source category. */
	UFUNCTION()
	void HandlePlayerHealingReceived(AActor* Recipient, AActor* HealingInstigator, float HealingReceived, FName HealingSourceType, UObject* SourceObject);

	/** Records authoritative inventory acquisition and equipment transitions. */
	UFUNCTION()
	void HandleInventoryItemStateChanged(const FInventoryItemChangeEvent& EventData);

	UFUNCTION()
	void HandleEnemyDied(AActor* DeadEnemy);
	void HandleEnemyXPAwarded(const FAeyerjiEnemyXPAward& Award);
	void RecordAuthorityFrame(float DeltaSeconds);
	int32 FindRecentKillRewardEvent(const AActor* EnemyActor, float EventTime) const;

	FString ResolveDamageArchetype(const AActor* SourceActor) const;
	float CalculateRollingDamage(float WindowSeconds, float EventTime) const;
	FString GetStateString() const;
	int32 CountLocallyVisibleEnemies(APlayerController* LocalController, int32& OutLocalActive) const;

	UPROPERTY(Transient)
	TObjectPtr<AAeyerjiSpawnerGroup> TestSpawner = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AAeyerjiPlayerController> TestedController = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<APawn> TestedPawn = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<APawn>> SpawnedEnemies;

	TArray<struct FEnemySet> PendingEnemySets;
	TArray<FDamageEvent> DamageEvents;
	TArray<FHealingEvent> HealingEvents;
	TArray<FPopulationSample> PopulationSamples;
	TArray<FHitchEvent> HitchEvents;
	TArray<FKillRewardEvent> KillRewardEvents;
	TArray<FProgressionEvent> ProgressionEvents;
	TArray<FItemEvent> ItemEvents;
	TArray<FMarkerEvent> MarkerEvents;
	/** Authority-only targeting transitions; client input observations never overwrite these totals. */
	TArray<FCombatTargetingEvent> TargetingEvents;
	/** Authority-only record storage; no player delegate or respawn rebind is required. */
	TArray<FBombardmentEvent> BombardmentEvents;
	TMap<uint64, FBombardmentEvent> ActiveBombardments;
	int32 BombardmentStarts = 0;
	int32 BombardmentImpacts = 0;
	int32 BombardmentCancellations = 0;
	int32 BombardmentDamageApplications = 0;
	int32 BombardmentPeakActiveZones = 0;
	int32 BombardmentDroppedEvents = 0;
	int32 BombardmentUntrackedStarts = 0;
	int32 PlayerTargetHandoffs = 0;
	int32 DeadTargetHandoffs = 0;
	int32 PlayerPrimaryActivations = 0;
	int32 EnemyDeadTargetClears = 0;
	int32 EnemyLeashTargetClears = 0;
	int32 InvalidPrimaryActivationsPrevented = 0;
	int32 TargetingDroppedEvents = 0;
	TWeakObjectPtr<UAbilitySystemComponent> TestedAbilitySystem;
	TWeakObjectPtr<UAeyerjiAttributeSet> TestedAttributeSet;
	TWeakObjectPtr<UAeyerjiInventoryComponent> TestedInventory;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle EnemyXPAwardedHandle;
	TSet<TWeakObjectPtr<APawn>> ArrivedThreateningEnemies;
	TMap<FString, float> DamageByArchetype;
	TMap<FString, float> HealingBySource;
	/** Last observed live GAS level per enemy, used to emit authoritative level-change markers. */
	TMap<TWeakObjectPtr<AEnemyParentNative>, int32> LastObservedEnemyLevels;
	FAeyerjiCombatTestRequest ActiveRequest;
	int32 NextSpawnIndex = 0;
	double NextSpawnWorldTime = 0.0;
	double ReadyWorldTime = 0.0;
	double CombatStartWorldTime = 0.0;
	double NextSampleWorldTime = 0.0;
	float LatestAuthorityFrameDeltaMs = 0.f;
	float LastPopulationSampleElapsedSeconds = -1.f;
	float MaximumAuthorityFrameDeltaMs = 0.f;
	float TotalAuthorityHitchOverageMs = 0.f;
	int32 AuthorityHitchCount = 0;
	float LocalHUDAccumulator = 0.f;
	float TimeToFirstDamage = -1.f;
	float TimeToFirstKill = -1.f;
	float ClearTime = -1.f;
	FString BaselineDescription;
	FString ReportBasePath;
	bool bFirstDamageMarked = false;
	bool bFirstKillMarked = false;
	bool bCleanupPerformed = false;
	bool bReportWritten = false;
	bool bAwaitingPlayerRespawn = false;
	bool bHasProgressionSnapshot = false;
	float LastPlayerDeathElapsedTime = -1.f;
	int32 LastProgressionLevel = 1;
	float LastProgressionXP = 0.f;
	float LastProgressionXPMax = 0.f;
	float LastProgressionHPMax = 0.f;
	float LastProgressionAttackDamage = 0.f;
	float LastProgressionArmor = 0.f;
	float LastProgressionDodgeChance = 0.f;
	float LastProgressionHPRegen = 0.f;
	float LastProgressionRunSpeed = 0.f;
};
