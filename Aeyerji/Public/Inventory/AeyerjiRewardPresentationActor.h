// AeyerjiRewardPresentationActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Interaction/AeyerjiInteractable.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Items/LootTypes.h"
#include "TimerManager.h"

#include "AeyerjiRewardPresentationActor.generated.h"

class APlayerState;

UENUM(BlueprintType)
enum class EAeyerjiRewardPresentationReleasePolicy : uint8
{
	InteractToRelease UMETA(DisplayName="Interact To Release"),
	ManualReleaseOnly UMETA(DisplayName="Manual Release Only"),
	AutoReleaseOnInitialize UMETA(DisplayName="Auto Release On Initialize")
};

/**
 * Replicated presentation shell for delayed loot rewards.
 * The server stores exact rolled loot results; clients receive only summary data for visuals/UI.
 */
UCLASS(BlueprintType, Blueprintable)
class AEYERJI_API AAeyerjiRewardPresentationActor : public AActor, public IAeyerjiInteractable
{
	GENERATED_BODY()

public:
	AAeyerjiRewardPresentationActor();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Returns whether this reward still contains loot that the supplied player may release. */
	virtual bool CanInteract_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Returns the world-space center used for pathing and server range validation. */
	virtual FVector GetInteractionLocation_Implementation() override;

	/** Returns the configured reward release interaction radius. */
	virtual float GetInteractionRadius_Implementation() override;

	/** Executes the server-authoritative reward release request. */
	virtual void Interact_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Initializes this reward bundle on the authority after loot has already been rolled. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward")
	void InitializeReward(
		const TArray<FLootDropResult>& InLootResults,
		EItemDropDistributionMode InDropMode,
		FGameplayTag InSourceTag,
		AActor* InInstigator,
		FVector InLootReleaseOffset,
		float InLifeSpanAfterRelease);

	/**
	 * Supplies the nearby NavMesh anchor used for interaction movement and server range checks.
	 * The visual chest remains at its authored transform; clients receive this anchor through replication.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward|Interaction")
	void SetInteractionNavigationAnchor(const FVector& InNavigationAnchor);

	/** Removes the optional navigation anchor so interaction returns to the visual actor location. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward|Interaction")
	void ClearInteractionNavigationAnchor();

	/**
	 * Controls whether InitializeReward may vertically ground-snap this individual presentation.
	 * Rift candidate points pass false before initialization so their visual chest transform remains exactly designer-authored;
	 * the separate navigation anchor still handles player movement and range validation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward|Ground Snap")
	void SetSnapPresentationToGroundOnInitialize(bool bInSnapToGroundOnInitialize);

	/**
	 * Configures Rift treasure proximity opening and optional pickup auto-collection.
	 * Auto-open calls this actor's normal HandleReleaseRequested path, and auto-collect retains each pickup's normal inventory transfer path.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward|Treasure")
	void ConfigureTreasureAutomation(
		bool bEnableAutoOpen,
		float InAutoOpenRadius,
		int32 InAutoOpenUnlockLevel,
		bool bRequireMaxCharacterLevel,
		float InAutoOpenPollingInterval,
		bool bEnableAutoCollect,
		float InAutoCollectRadius);

	/** Adds one already-rolled private bundle to a shared visual cache. Authority only; repeated calls append without rerolling. */
	void AddPrivateRewardBundle(APlayerState* PlayerState, const TArray<FLootDropResult>& LootResults, FGameplayTag InSourceTag);

	/** Number of unreleased private entries for one player. */
	int32 GetPendingPrivateRewardCount(const APlayerState* PlayerState) const;

	/** Releases stored loot at this actor's transform plus LootReleaseOffset. Authority only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward")
	bool ReleaseStoredLoot(AActor* Activator = nullptr);

	/** Releases stored loot at an explicit transform. Authority only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward")
	bool ReleaseStoredLootAtTransform(const FTransform& ReleaseTransform, AActor* Activator = nullptr);

	/** Default release request handler. Override in Blueprint to play an opening animation before calling ReleaseStoredLoot. */
	UFUNCTION(BlueprintNativeEvent, Category="Aeyerji|Reward")
	void HandleReleaseRequested(AActor* Activator);
	virtual void HandleReleaseRequested_Implementation(AActor* Activator);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	int32 GetRewardCount() const { return RewardCount; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	EItemRarity GetBestRarity() const { return BestRarity; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	FGameplayTag GetRewardSourceTag() const { return RewardSourceTag; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	bool HasReleasedReward() const { return bReleased; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	bool IsRewardInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Reward")
	bool HasPendingReward() const { return !bReleased && RewardCount > 0; }

	/** Moves this presentation actor so the bottom of its visual mesh bounds rests on traced ground. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Reward|Ground Snap")
	bool SnapPresentationToGround();

	/**
	 * Discards unreleased reward state and any still-live pickups spawned by this presentation.
	 * Rift reset uses this authority-only cleanup so stale chests and their released loot cannot survive into a new run.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Reward")
	void DiscardStoredRewardAndSpawnedLoot();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward")
	TObjectPtr<USceneComponent> Root;

	/** Native cursor target used by the generic interaction trace. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Interaction")
	TObjectPtr<class USphereComponent> InteractionSphere;

	/** Server-side distance guard for player-controller release requests. Set to 0 to allow any distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Interaction", meta=(ClampMin="0.0", Units="cm"))
	float ReleaseInteractionRadius = 350.f;

	/** Controls how this presentation releases stored loot. Chests usually use InteractToRelease. Portals usually use ManualReleaseOnly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Release")
	EAeyerjiRewardPresentationReleasePolicy ReleasePolicy = EAeyerjiRewardPresentationReleasePolicy::InteractToRelease;

	/** Delay used only by AutoReleaseOnInitialize. ManualReleaseOnly ignores this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Release", meta=(ClampMin="0.0", Units="s"))
	float AutoReleaseDelaySeconds = 0.f;

	/** Optional radial scatter when multiple stored loot results are released. Zero preserves exact release location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Release", meta=(ClampMin="0.0", Units="cm"))
	float LootReleaseScatterRadius = 0.f;

	/** Angle offset for release scatter, in degrees. Useful for lining portal/chest bursts up with art direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Release", meta=(Units="deg"))
	float LootReleaseScatterYawOffset = 0.f;

	/** Snap this presentation actor to the floor when InitializeReward runs on the server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap")
	bool bSnapToGroundOnInitialize = true;

	/** Optional editor/construction preview snap. Usually keep false unless placed preview actors should auto-snap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap")
	bool bSnapToGroundInConstruction = false;

	/** Trace channel used to find the floor. Set this to the custom Ground trace channel in Blueprint if needed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap")
	TEnumAsByte<ECollisionChannel> GroundSnapTraceChannel = ECC_Visibility;

	/** Height above the current actor location where the floor trace starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap", meta=(ClampMin="0.0", Units="cm"))
	float GroundSnapTraceStartHeight = 250.f;

	/** Distance below the current actor location where the floor trace ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap", meta=(ClampMin="0.0", Units="cm"))
	float GroundSnapTraceDistance = 2500.f;

	/** Small lift to prevent z-fighting or tiny floor penetration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap", meta=(Units="cm"))
	float GroundSnapAdditionalZOffset = 1.f;

	/** Usually false. Only enable if the floor requires complex collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Ground Snap")
	bool bGroundSnapTraceComplex = false;

	/** Cosmetic hook when replicated summary values change on clients. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Reward")
	void OnRewardSummaryChanged();

	/** Cosmetic hook when this reward was initialized on the server. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Reward")
	void OnRewardInitialized();

	/** Cosmetic hook when initialized state is available on both server and clients. Use this for portal-start visuals. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Reward")
	void OnRewardInitializedReplicated();

	/** Cosmetic hook after the authority releases pickups. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Reward")
	void OnRewardReleased(AActor* Activator);

	/** Cosmetic hook when release state replicates to clients. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Reward")
	void OnRewardReleasedReplicated();

	UFUNCTION()
	void OnRep_RewardSummary();

	UFUNCTION()
	void OnRep_Released();

	UFUNCTION()
	void OnRep_Initialized();

private:
	struct FPrivateRewardEntry
	{
		FLootDropResult Result;
		bool bReleased = false;
	};

	struct FPrivateRewardBundle
	{
		TArray<FPrivateRewardEntry> Entries;
		FGameplayTag SourceTag;
	};

	bool GetVisualMeshBounds(FBox& OutBounds) const;
	void AutoReleaseStoredLoot();
	void RefreshInteractionCollision();
	void RefreshRewardSummary();
	void SanitizeRuntimeSettings();
	void SetReleased();
	void RefreshTreasureAutoOpenTimer();
	void EvaluateTreasureAutoOpen();
	bool IsPawnEligibleForTreasureAutoOpen(const APawn* Pawn) const;
	void TrackSpawnedLootPickups(const TArray<class AAeyerjiLootPickup*>& SpawnedPickups);
	void ConfigureSpawnedPickupsForAutoCollection(const TArray<class AAeyerjiLootPickup*>& SpawnedPickups) const;
	APlayerState* ResolvePlayerStateFromActivator(AActor* Activator) const;
	bool ReleasePrivateRewardAtTransform(const FTransform& ReleaseTransform, AActor* Activator);
	void RefreshPrivateRewardSummary();

	FTimerHandle AutoReleaseTimerHandle;
	FTimerHandle TreasureAutoOpenTimerHandle;

	UPROPERTY(Transient)
	TArray<FLootDropResult> PendingLootResults;
	TMap<TWeakObjectPtr<APlayerState>, FPrivateRewardBundle> PrivateRewardBundles;

	UPROPERTY(Transient)
	TObjectPtr<AActor> RewardInstigator = nullptr;

	UPROPERTY(Transient)
	EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator;

	UPROPERTY(Transient)
	FVector LootReleaseOffset = FVector::ZeroVector;

	UPROPERTY(Transient)
	float PresentationLifeSpanAfterRelease = 10.f;

	UPROPERTY(Transient)
	bool bReleaseInProgress = false;

	/** Optional NavMesh interaction point replicated to clients while the visual chest remains at its authored transform. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Interaction", meta=(AllowPrivateAccess="true"))
	FVector InteractionNavigationAnchor = FVector::ZeroVector;

	/** True when InteractionNavigationAnchor replaces the visual actor location for movement/range queries. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Reward|Interaction", meta=(AllowPrivateAccess="true"))
	bool bHasInteractionNavigationAnchor = false;

	UPROPERTY(ReplicatedUsing=OnRep_RewardSummary, BlueprintReadOnly, Category="Aeyerji|Reward", meta=(AllowPrivateAccess="true"))
	int32 RewardCount = 0;

	UPROPERTY(ReplicatedUsing=OnRep_RewardSummary, BlueprintReadOnly, Category="Aeyerji|Reward", meta=(AllowPrivateAccess="true"))
	EItemRarity BestRarity = EItemRarity::Common;

	UPROPERTY(ReplicatedUsing=OnRep_RewardSummary, BlueprintReadOnly, Category="Aeyerji|Reward", meta=(AllowPrivateAccess="true"))
	FGameplayTag RewardSourceTag;

	UPROPERTY(ReplicatedUsing=OnRep_Released, BlueprintReadOnly, Category="Aeyerji|Reward", meta=(AllowPrivateAccess="true"))
	bool bReleased = false;

	UPROPERTY(ReplicatedUsing=OnRep_Initialized, BlueprintReadOnly, Category="Aeyerji|Reward", meta=(AllowPrivateAccess="true"))
	bool bInitialized = false;

	/** Runtime-only Rift automation; disabled by default so existing reward presentation behavior is unchanged. */
	bool bTreasureAutoOpenEnabled = false;
	bool bTreasureAutoOpenRequestPending = false;
	bool bTreasureAutoOpenRequiresMaxCharacterLevel = false;
	bool bAutoCollectSpawnedLoot = false;
	int32 TreasureAutoOpenUnlockLevel = 1;
	float TreasureAutoOpenRadius = 0.f;
	float TreasureAutoOpenPollingInterval = 0.15f;
	float TreasureAutoCollectRadius = 140.f;

	/** Pickups released by this actor and still owned by the current Rift presentation cleanup scope. */
	TArray<TWeakObjectPtr<class AAeyerjiLootPickup>> SpawnedLootPickups;
};
