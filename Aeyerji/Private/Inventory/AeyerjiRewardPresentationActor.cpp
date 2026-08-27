// AeyerjiRewardPresentationActor.cpp

#include "Inventory/AeyerjiRewardPresentationActor.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Items/ItemDefinition.h"
#include "Logging/AeyerjiLog.h"
#include "Net/UnrealNetwork.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootService.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 MaxStoredPublicRewardResults = 1024;
	constexpr int32 MaxPrivateRewardBundles = 128;
	constexpr int32 MaxPrivateRewardEntriesPerPlayer = 256;
	constexpr int32 MaxStoredPrivateRewardResults = 4096;
	constexpr int32 MaxRewardVisualMeshes = 128;
	constexpr float MaxRewardDistance = 1000000.f;
	constexpr float MaxRewardDurationSeconds = 86400.f;
	constexpr float MinimumTreasureAutoOpenPollingInterval = 0.05f;
	constexpr float MaximumTreasureAutoOpenPollingInterval = 60.f;

	bool IsFiniteRewardVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteRewardTransform(const FTransform& Transform)
	{
		const FQuat Rotation = Transform.GetRotation();
		return IsFiniteRewardVector(Transform.GetTranslation())
			&& IsFiniteRewardVector(Transform.GetScale3D())
			&& FMath::IsFinite(Rotation.X)
			&& FMath::IsFinite(Rotation.Y)
			&& FMath::IsFinite(Rotation.Z)
			&& FMath::IsFinite(Rotation.W)
			&& Rotation.IsNormalized();
	}

	bool IsValidRewardRarity(const EItemRarity Rarity)
	{
		return IsValidLootDropRarity(Rarity);
	}

	bool IsUsableRewardResult(const FLootDropResult& Result)
	{
		return IsUsableLootDropResult(Result);
	}

	FVector ResolveRewardScatterLocation(
		const FVector& BaseLocation,
		const int32 ResultIndex,
		const int32 ResultCount,
		const float ScatterRadius,
		const float ScatterYawOffset)
	{
		if (ResultCount <= 1 || ScatterRadius <= KINDA_SMALL_NUMBER)
		{
			return BaseLocation;
		}

		const float AngleDegrees = ScatterYawOffset
			+ (360.f * static_cast<float>(ResultIndex) / static_cast<float>(ResultCount));
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
		return BaseLocation + FVector(FMath::Cos(AngleRadians) * ScatterRadius, FMath::Sin(AngleRadians) * ScatterRadius, 0.f);
	}
}

AAeyerjiRewardPresentationActor::AAeyerjiRewardPresentationActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(Root);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	InteractionSphere->SetSphereRadius(ReleaseInteractionRadius > 0.f ? ReleaseInteractionRadius : 350.f);
	InteractionSphere->SetGenerateOverlapEvents(false);
	InteractionSphere->SetCanEverAffectNavigation(false);
	RefreshInteractionCollision();
}

void AAeyerjiRewardPresentationActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SanitizeRuntimeSettings();
	RefreshInteractionCollision();

	if (bSnapToGroundInConstruction)
	{
		SnapPresentationToGround();
	}
}

bool AAeyerjiRewardPresentationActor::CanInteract_Implementation(AAeyerjiPlayerController* Controller)
{
	if (!IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		return false;
	}
	const APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld() || Pawn->GetController() != Controller)
	{
		return false;
	}
	const FVector PawnLocation = Pawn->GetActorLocation();
	const FVector InteractionLocation = GetInteractionLocation_Implementation();
	if (!IsFiniteRewardVector(PawnLocation) || !IsFiniteRewardVector(InteractionLocation))
	{
		return false;
	}
	if (ReleaseInteractionRadius > 0.f
		&& FVector::DistSquared2D(PawnLocation, InteractionLocation) > FMath::Square(ReleaseInteractionRadius))
	{
		return false;
	}

	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	const bool bHasControllerReward = PrivateRewardBundles.IsEmpty()
		? RewardCount > 0
		: GetPendingPrivateRewardCount(PlayerState) > 0;
	return ReleasePolicy == EAeyerjiRewardPresentationReleasePolicy::InteractToRelease
		&& !bReleased
		&& bHasControllerReward;
}

FVector AAeyerjiRewardPresentationActor::GetInteractionLocation_Implementation()
{
	if (bHasInteractionNavigationAnchor && IsFiniteRewardVector(InteractionNavigationAnchor))
	{
		return InteractionNavigationAnchor;
	}

	const FVector Location = InteractionSphere ? InteractionSphere->GetComponentLocation() : GetActorLocation();
	return IsFiniteRewardVector(Location) ? Location : FVector::ZeroVector;
}

float AAeyerjiRewardPresentationActor::GetInteractionRadius_Implementation()
{
	return FMath::Clamp(FMath::IsFinite(ReleaseInteractionRadius) ? ReleaseInteractionRadius : 350.f, 0.f, MaxRewardDistance);
}

void AAeyerjiRewardPresentationActor::Interact_Implementation(AAeyerjiPlayerController* Controller)
{
	if (ReleasePolicy != EAeyerjiRewardPresentationReleasePolicy::InteractToRelease)
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Interact ignored because ReleasePolicy=%d Controller=%s"),
			static_cast<int32>(ReleasePolicy),
			*GetNameSafe(Controller));
		return;
	}

	if (!HasAuthority() || !CanInteract_Implementation(Controller))
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Interact rejected Authority=%d Controller=%s Released=%d RewardCount=%d"),
			HasAuthority(),
			*GetNameSafe(Controller),
			bReleased ? 1 : 0,
			RewardCount);
		return;
	}

	AActor* Activator = Controller->GetPawn();
	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Interact accepted Controller=%s Activator=%s RewardCount=%d Source=%s"),
		*GetNameSafe(Controller),
		*GetNameSafe(Activator ? Activator : static_cast<AActor*>(Controller)),
		RewardCount,
		*RewardSourceTag.ToString());
	HandleReleaseRequested(Activator ? Activator : static_cast<AActor*>(Controller));
}

void AAeyerjiRewardPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	}

	if (HasAuthority() && !bReleased && PendingLootResults.Num() > 0)
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[Interaction][RewardPresentation] Actor ended before releasing pending loot Target=%s Pending=%d"),
			*GetNameSafe(this),
			PendingLootResults.Num());
	}
	if (HasAuthority() && !bReleased && !PrivateRewardBundles.IsEmpty())
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[Interaction][RewardPresentation] Actor ended with private reward bundles Target=%s Pending=%d"),
			*GetNameSafe(this), RewardCount);
	}

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiRewardPresentationActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiRewardPresentationActor, RewardCount);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, BestRarity);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, RewardSourceTag);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, bReleased);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, bInitialized);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, InteractionNavigationAnchor);
	DOREPLIFETIME(AAeyerjiRewardPresentationActor, bHasInteractionNavigationAnchor);
}

void AAeyerjiRewardPresentationActor::InitializeReward(
	const TArray<FLootDropResult>& InLootResults,
	const EItemDropDistributionMode InDropMode,
	const FGameplayTag InSourceTag,
	AActor* InInstigator,
	const FVector InLootReleaseOffset,
	const float InLifeSpanAfterRelease)
{
	if (!HasAuthority())
	{
		return;
	}
	SanitizeRuntimeSettings();
	if (!IsFiniteRewardVector(InLootReleaseOffset))
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[Interaction][RewardPresentation] InitializeReward replaced non-finite release offset Target=%s"),
			*GetNameSafe(this));
	}
	if (InInstigator && (!IsValid(InInstigator) || InInstigator->GetWorld() != GetWorld()))
	{
		InInstigator = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
	}

	PendingLootResults.Reset(FMath::Min(InLootResults.Num(), MaxStoredPublicRewardResults));
	const int32 ResultCount = FMath::Min(InLootResults.Num(), MaxStoredPublicRewardResults);
	for (int32 ResultIndex = 0; ResultIndex < ResultCount; ++ResultIndex)
	{
		FLootDropResult Result = InLootResults[ResultIndex];
		if (!IsUsableRewardResult(Result))
		{
			continue;
		}
		Result.ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Result.ItemLevel);
		PendingLootResults.Add(MoveTemp(Result));
	}
	PrivateRewardBundles.Reset();
	const UEnum* DropModeEnum = StaticEnum<EItemDropDistributionMode>();
	DropMode = DropModeEnum && DropModeEnum->IsValidEnumValue(static_cast<int64>(InDropMode))
		? InDropMode
		: EItemDropDistributionMode::DropOnlyForInstigator;
	RewardSourceTag = InSourceTag;
	RewardInstigator = InInstigator;
	LootReleaseOffset = IsFiniteRewardVector(InLootReleaseOffset) ? InLootReleaseOffset : FVector::ZeroVector;
	PresentationLifeSpanAfterRelease = FMath::Clamp(
		FMath::IsFinite(InLifeSpanAfterRelease) ? InLifeSpanAfterRelease : 10.f,
		0.f,
		MaxRewardDurationSeconds);
	bReleased = false;
	bReleaseInProgress = false;
	bTreasureAutoOpenRequestPending = false;
	bInitialized = true;

	RefreshRewardSummary();
	RefreshInteractionCollision();
	if (bSnapToGroundOnInitialize)
	{
		SnapPresentationToGround();
	}

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Initialized RewardCount=%d Source=%s Instigator=%s DropMode=%d ReleasePolicy=%d ScatterRadius=%.1f"),
		RewardCount,
		*RewardSourceTag.ToString(),
		*GetNameSafe(RewardInstigator.Get()),
		static_cast<int32>(DropMode),
		static_cast<int32>(ReleasePolicy),
		FMath::Max(0.f, LootReleaseScatterRadius));
	OnRewardInitialized();
	OnRewardInitializedReplicated();
	ForceNetUpdate();
	RefreshTreasureAutoOpenTimer();

	if (ReleasePolicy == EAeyerjiRewardPresentationReleasePolicy::AutoReleaseOnInitialize)
	{
		if (AutoReleaseDelaySeconds <= KINDA_SMALL_NUMBER)
		{
			AutoReleaseStoredLoot();
		}
		else if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				AutoReleaseTimerHandle,
				this,
				&AAeyerjiRewardPresentationActor::AutoReleaseStoredLoot,
				AutoReleaseDelaySeconds,
				false);
		}
	}
}

void AAeyerjiRewardPresentationActor::SetInteractionNavigationAnchor(const FVector& InNavigationAnchor)
{
	if (!HasAuthority() || !IsFiniteRewardVector(InNavigationAnchor))
	{
		return;
	}

	InteractionNavigationAnchor = InNavigationAnchor;
	bHasInteractionNavigationAnchor = true;
	ForceNetUpdate();
}

void AAeyerjiRewardPresentationActor::ClearInteractionNavigationAnchor()
{
	if (!HasAuthority())
	{
		return;
	}

	InteractionNavigationAnchor = FVector::ZeroVector;
	bHasInteractionNavigationAnchor = false;
	ForceNetUpdate();
}

void AAeyerjiRewardPresentationActor::SetSnapPresentationToGroundOnInitialize(
	const bool bInSnapToGroundOnInitialize)
{
	if (!HasAuthority())
	{
		return;
	}

	bSnapToGroundOnInitialize = bInSnapToGroundOnInitialize;
}

void AAeyerjiRewardPresentationActor::ConfigureTreasureAutomation(
	const bool bEnableAutoOpen,
	const float InAutoOpenRadius,
	const int32 InAutoOpenUnlockLevel,
	const bool bRequireMaxCharacterLevel,
	const float InAutoOpenPollingInterval,
	const bool bEnableAutoCollect,
	const float InAutoCollectRadius)
{
	if (!HasAuthority())
	{
		return;
	}

	bTreasureAutoOpenEnabled = bEnableAutoOpen
		&& ReleasePolicy == EAeyerjiRewardPresentationReleasePolicy::InteractToRelease;
	bTreasureAutoOpenRequestPending = false;
	bTreasureAutoOpenRequiresMaxCharacterLevel = bRequireMaxCharacterLevel;
	TreasureAutoOpenUnlockLevel = FMath::Max(1, InAutoOpenUnlockLevel);
	TreasureAutoOpenRadius = FMath::Clamp(
		FMath::IsFinite(InAutoOpenRadius) ? InAutoOpenRadius : 0.f,
		0.f,
		MaxRewardDistance);
	TreasureAutoOpenPollingInterval = FMath::Clamp(
		FMath::IsFinite(InAutoOpenPollingInterval) ? InAutoOpenPollingInterval : 0.15f,
		MinimumTreasureAutoOpenPollingInterval,
		MaximumTreasureAutoOpenPollingInterval);
	bAutoCollectSpawnedLoot = bEnableAutoCollect;
	TreasureAutoCollectRadius = FMath::Clamp(
		FMath::IsFinite(InAutoCollectRadius) ? InAutoCollectRadius : 140.f,
		1.f,
		MaxRewardDistance);

	if (bEnableAutoOpen && !bTreasureAutoOpenEnabled)
	{
		AJ_LOG(this, TEXT("[Treasure] Auto-open ignored because chest %s does not use InteractToRelease."),
			*GetNameSafe(this));
	}

	RefreshTreasureAutoOpenTimer();
}

void AAeyerjiRewardPresentationActor::AddPrivateRewardBundle(
	APlayerState* PlayerState,
	const TArray<FLootDropResult>& LootResults,
	const FGameplayTag InSourceTag)
{
	if (!HasAuthority() || !IsValid(PlayerState) || PlayerState->GetWorld() != GetWorld()
		|| LootResults.IsEmpty() || !PendingLootResults.IsEmpty())
	{
		return;
	}
	SanitizeRuntimeSettings();
	if (!PrivateRewardBundles.Contains(PlayerState) && PrivateRewardBundles.Num() >= MaxPrivateRewardBundles)
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[Interaction][RewardPresentation] AddPrivateRewardBundle rejected bundle cap Target=%s PlayerState=%s Cap=%d"),
			*GetNameSafe(this), *GetNameSafe(PlayerState), MaxPrivateRewardBundles);
		return;
	}

	int32 TotalStoredEntries = 0;
	for (const TPair<TWeakObjectPtr<APlayerState>, FPrivateRewardBundle>& Pair : PrivateRewardBundles)
	{
		TotalStoredEntries += Pair.Value.Entries.Num();
		if (TotalStoredEntries >= MaxStoredPrivateRewardResults)
		{
			return;
		}
	}

	const bool bWasInitialized = bInitialized;
	FPrivateRewardBundle& Bundle = PrivateRewardBundles.FindOrAdd(PlayerState);
	Bundle.SourceTag = InSourceTag;
	const int32 AvailableForPlayer = MaxPrivateRewardEntriesPerPlayer - Bundle.Entries.Num();
	const int32 AvailableGlobally = MaxStoredPrivateRewardResults - TotalStoredEntries;
	const int32 ResultsToInspect = FMath::Min3(LootResults.Num(), AvailableForPlayer, AvailableGlobally);
	for (int32 ResultIndex = 0; ResultIndex < ResultsToInspect; ++ResultIndex)
	{
		FLootDropResult Result = LootResults[ResultIndex];
		if (!IsUsableRewardResult(Result))
		{
			continue;
		}
		Result.ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Result.ItemLevel);
		FPrivateRewardEntry& Entry = Bundle.Entries.AddDefaulted_GetRef();
		Entry.Result = MoveTemp(Result);
	}
	if (Bundle.Entries.IsEmpty())
	{
		PrivateRewardBundles.Remove(PlayerState);
		return;
	}
	if (!RewardSourceTag.IsValid())
	{
		RewardSourceTag = InSourceTag;
	}
	bInitialized = true;
	bReleased = false;
	bTreasureAutoOpenRequestPending = false;
	RefreshPrivateRewardSummary();
	RefreshInteractionCollision();
	if (!bWasInitialized)
	{
		if (bSnapToGroundOnInitialize)
		{
			SnapPresentationToGround();
		}
		OnRewardInitialized();
		OnRewardInitializedReplicated();
	}
	ForceNetUpdate();
	RefreshTreasureAutoOpenTimer();
}

int32 AAeyerjiRewardPresentationActor::GetPendingPrivateRewardCount(const APlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return 0;
	}
	const FPrivateRewardBundle* Bundle = PrivateRewardBundles.Find(TWeakObjectPtr<APlayerState>(const_cast<APlayerState*>(PlayerState)));
	if (!Bundle)
	{
		return 0;
	}
	int32 PendingCount = 0;
	for (const FPrivateRewardEntry& Entry : Bundle->Entries)
	{
		PendingCount += Entry.bReleased ? 0 : 1;
	}
	return PendingCount;
}

bool AAeyerjiRewardPresentationActor::ReleaseStoredLoot(AActor* Activator)
{
	const FTransform ReleaseTransform(GetActorRotation(), GetActorLocation() + LootReleaseOffset, GetActorScale3D());
	return ReleaseStoredLootAtTransform(ReleaseTransform, Activator);
}

bool AAeyerjiRewardPresentationActor::ReleaseStoredLootAtTransform(const FTransform& ReleaseTransform, AActor* Activator)
{
	if (!HasAuthority() || bReleased || bReleaseInProgress)
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Release rejected Authority=%d Released=%d InProgress=%d Activator=%s"),
			HasAuthority(),
			bReleased ? 1 : 0,
			bReleaseInProgress ? 1 : 0,
			*GetNameSafe(Activator));
		return false;
	}
	SanitizeRuntimeSettings();
	if (!IsFiniteRewardTransform(ReleaseTransform)
		|| (Activator && (!IsValid(Activator) || Activator->GetWorld() != GetWorld())))
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[Interaction][RewardPresentation] Release rejected invalid transform or cross-world activator Target=%s Activator=%s"),
			*GetNameSafe(this), *GetNameSafe(Activator));
		return false;
	}
	if (!PrivateRewardBundles.IsEmpty())
	{
		return ReleasePrivateRewardAtTransform(ReleaseTransform, Activator);
	}

	bReleaseInProgress = true;
	bTreasureAutoOpenRequestPending = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	}

	if (PendingLootResults.IsEmpty())
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Release found no pending loot Activator=%s"),
			*GetNameSafe(Activator));
		SetReleased();
		bReleaseInProgress = false;
		return false;
	}

	AActor* EffectiveInstigator = Activator ? Activator : RewardInstigator.Get();
	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Releasing loot StoredResults=%d Activator=%s Location=%s Source=%s ScatterRadius=%.1f"),
		PendingLootResults.Num(),
		*GetNameSafe(EffectiveInstigator),
		*ReleaseTransform.GetLocation().ToCompactString(),
		*RewardSourceTag.ToString(),
		FMath::Max(0.f, LootReleaseScatterRadius));
	FAeyerjiLootSpawnSummary SpawnSummary;
	TArray<FLootDropResult> FailedResults;
	FailedResults.Reserve(PendingLootResults.Num());
	const int32 StoredResultCount = PendingLootResults.Num();
	for (int32 ResultIndex = 0; ResultIndex < StoredResultCount; ++ResultIndex)
	{
		const FVector SpawnLocation = ResolveRewardScatterLocation(
			ReleaseTransform.GetLocation(),
			ResultIndex,
			StoredResultCount,
			LootReleaseScatterRadius,
			LootReleaseScatterYawOffset);
		const FAeyerjiLootSpawnSummary ResultSummary = UAeyerjiInventoryBPFL::SpawnLootResults(
			this,
			TArray<FLootDropResult>{PendingLootResults[ResultIndex]},
			SpawnLocation,
			ReleaseTransform.Rotator(),
			/*SeedOverride=*/0,
			DropMode,
			EffectiveInstigator,
			/*LootReleaseScatterRadius=*/0.f,
			/*LootReleaseScatterYawOffset=*/0.f);
		SpawnSummary.RequestedResultCount += ResultSummary.RequestedResultCount;
		SpawnSummary.SpawnedPickupCount += ResultSummary.SpawnedPickupCount;
		SpawnSummary.FailedSpawnCount += ResultSummary.FailedSpawnCount;
		SpawnSummary.SpawnedPickups.Append(ResultSummary.SpawnedPickups);
		if (ResultSummary.LastSpawnedPickup)
		{
			SpawnSummary.LastSpawnedPickup = ResultSummary.LastSpawnedPickup;
		}
		TrackSpawnedLootPickups(ResultSummary.SpawnedPickups);
		ConfigureSpawnedPickupsForAutoCollection(ResultSummary.SpawnedPickups);
		if (ResultSummary.SpawnedPickupCount <= 0)
		{
			FailedResults.Add(PendingLootResults[ResultIndex]);
		}
	}

	PendingLootResults = MoveTemp(FailedResults);
	const bool bAnyPickupSpawned = SpawnSummary.SpawnedPickupCount > 0;
	if (PendingLootResults.IsEmpty())
	{
		SetReleased();
	}
	else
	{
		RefreshRewardSummary();
		RefreshInteractionCollision();
		ForceNetUpdate();
	}
	bReleaseInProgress = false;

	if (bAnyPickupSpawned)
	{
		OnRewardReleased(EffectiveInstigator);
	}

	if (bReleased && PresentationLifeSpanAfterRelease > 0.f)
	{
		SetLifeSpan(PresentationLifeSpanAfterRelease);
	}

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Release complete Activator=%s StoredResults=%d SpawnedPickups=%d FailedSpawns=%d LifeSpan=%.1f"),
		*GetNameSafe(EffectiveInstigator),
		SpawnSummary.RequestedResultCount,
		SpawnSummary.SpawnedPickupCount,
		SpawnSummary.FailedSpawnCount,
		PresentationLifeSpanAfterRelease);
	return bAnyPickupSpawned;
}

APlayerState* AAeyerjiRewardPresentationActor::ResolvePlayerStateFromActivator(AActor* Activator) const
{
	if (!IsValid(Activator) || Activator->GetWorld() != GetWorld())
	{
		return nullptr;
	}
	if (APlayerState* PlayerState = Cast<APlayerState>(Activator))
	{
		return PlayerState;
	}
	if (const APawn* Pawn = Cast<APawn>(Activator))
	{
		return Pawn->GetPlayerState();
	}
	if (const AController* Controller = Cast<AController>(Activator))
	{
		return Controller->PlayerState;
	}
	return nullptr;
}

bool AAeyerjiRewardPresentationActor::ReleasePrivateRewardAtTransform(
	const FTransform& ReleaseTransform,
	AActor* Activator)
{
	APlayerState* PlayerState = ResolvePlayerStateFromActivator(Activator);
	FPrivateRewardBundle* Bundle = PlayerState
		? PrivateRewardBundles.Find(TWeakObjectPtr<APlayerState>(PlayerState))
		: nullptr;
	if (!IsValid(PlayerState) || PlayerState->GetWorld() != GetWorld()
		|| !Bundle || GetPendingPrivateRewardCount(PlayerState) <= 0
		|| !IsFiniteRewardTransform(ReleaseTransform))
	{
		return false;
	}

	bReleaseInProgress = true;
	bTreasureAutoOpenRequestPending = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	}
	AActor* RecipientActor = PlayerState->GetPawn();
	if (!IsValid(RecipientActor) || RecipientActor->GetWorld() != GetWorld())
	{
		// Seamless travel, death, and disconnect staging can temporarily leave a valid PlayerState
		// without a pawn. The PlayerState remains the authoritative private-reward identity.
		RecipientActor = PlayerState;
	}
	bool bAnyPickupSpawned = false;
	for (int32 EntryIndex = 0; EntryIndex < Bundle->Entries.Num(); ++EntryIndex)
	{
		FPrivateRewardEntry& Entry = Bundle->Entries[EntryIndex];
		if (Entry.bReleased)
		{
			continue;
		}

		const FVector ScatterOffset(45.f * static_cast<float>(EntryIndex), 0.f, 0.f);
		const FAeyerjiLootSpawnSummary SpawnSummary = UAeyerjiInventoryBPFL::SpawnLootResults(
			this,
			TArray<FLootDropResult>{Entry.Result},
			ReleaseTransform.TransformPositionNoScale(ScatterOffset),
			ReleaseTransform.Rotator(),
			/*SeedOverride=*/Entry.Result.Seed,
			EItemDropDistributionMode::DropOnlyForInstigator,
			RecipientActor,
			/*LootReleaseScatterRadius=*/0.f,
			/*LootReleaseScatterYawOffset=*/0.f);
		if (SpawnSummary.SpawnedPickupCount > 0)
		{
			for (AAeyerjiLootPickup* Pickup : SpawnSummary.SpawnedPickups)
			{
				if (IsValid(Pickup))
				{
					Pickup->ReserveForPlayerState(PlayerState);
				}
			}
			TrackSpawnedLootPickups(SpawnSummary.SpawnedPickups);
			ConfigureSpawnedPickupsForAutoCollection(SpawnSummary.SpawnedPickups);
			Entry.bReleased = true;
			bAnyPickupSpawned = true;
		}
	}

	bReleaseInProgress = false;
	RefreshPrivateRewardSummary();
	if (RewardCount <= 0)
	{
		SetReleased();
		if (PresentationLifeSpanAfterRelease > 0.f)
		{
			SetLifeSpan(PresentationLifeSpanAfterRelease);
		}
	}
	else
	{
		RefreshInteractionCollision();
		ForceNetUpdate();
		RefreshTreasureAutoOpenTimer();
	}
	if (bAnyPickupSpawned)
	{
		OnRewardReleased(RecipientActor ? RecipientActor : Activator);
	}
	return bAnyPickupSpawned;
}

void AAeyerjiRewardPresentationActor::RefreshPrivateRewardSummary()
{
	for (auto BundleIt = PrivateRewardBundles.CreateIterator(); BundleIt; ++BundleIt)
	{
		if (!BundleIt.Key().IsValid() || BundleIt.Key()->GetWorld() != GetWorld())
		{
			BundleIt.RemoveCurrent();
		}
	}

	RewardCount = 0;
	BestRarity = EItemRarity::Common;
	for (const TPair<TWeakObjectPtr<APlayerState>, FPrivateRewardBundle>& Pair : PrivateRewardBundles)
	{
		for (const FPrivateRewardEntry& Entry : Pair.Value.Entries)
		{
			if (Entry.bReleased)
			{
				continue;
			}
			RewardCount = FMath::Min(RewardCount + 1, MaxStoredPrivateRewardResults);
			if (IsValidRewardRarity(Entry.Result.Rarity)
				&& static_cast<uint8>(Entry.Result.Rarity) > static_cast<uint8>(BestRarity))
			{
				BestRarity = Entry.Result.Rarity;
			}
		}
	}
}

bool AAeyerjiRewardPresentationActor::SnapPresentationToGround()
{
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	// Runtime transforms must stay authority-owned; clients receive the corrected transform through replication.
	if (World->IsGameWorld() && !HasAuthority())
	{
		return false;
	}

	const FVector ActorLocation = GetActorLocation();
	if (!IsFiniteRewardVector(ActorLocation))
	{
		return false;
	}
	const FVector TraceStart = ActorLocation + FVector(0.f, 0.f, GroundSnapTraceStartHeight);
	const FVector TraceEnd = ActorLocation - FVector(0.f, 0.f, GroundSnapTraceDistance);
	if (!IsFiniteRewardVector(TraceStart) || !IsFiniteRewardVector(TraceEnd))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AeyerjiRewardPresentationGroundSnap), bGroundSnapTraceComplex);
	QueryParams.AddIgnoredActor(this);
	if (AActor* OwnerActor = GetOwner())
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	FHitResult Hit;
	const ECollisionChannel TraceChannel = GroundSnapTraceChannel.GetValue();
	if (TraceChannel >= ECC_MAX
		|| !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams)
		|| !IsFiniteRewardVector(Hit.ImpactPoint))
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Ground snap failed: no blocking hit Location=%s Channel=%d"),
			*ActorLocation.ToCompactString(),
			static_cast<int32>(TraceChannel));
		return false;
	}

	FBox VisualBounds;
	const bool bHasVisualBounds = GetVisualMeshBounds(VisualBounds);
	const float CurrentBottomZ = bHasVisualBounds ? VisualBounds.Min.Z : ActorLocation.Z;
	const float DesiredBottomZ = Hit.ImpactPoint.Z + GroundSnapAdditionalZOffset;
	const float DeltaZ = DesiredBottomZ - CurrentBottomZ;

	if (FMath::IsNearlyZero(DeltaZ, 0.1f))
	{
		return true;
	}

	const FVector NewActorLocation = ActorLocation + FVector(0.f, 0.f, DeltaZ);
	if (!IsFiniteRewardVector(NewActorLocation)
		|| !SetActorLocation(NewActorLocation, false, nullptr, ETeleportType::TeleportPhysics))
	{
		return false;
	}

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Ground snapped Hit=%s OldLocation=%s NewLocation=%s DeltaZ=%.2f BoundsFound=%d"),
		*Hit.ImpactPoint.ToCompactString(),
		*ActorLocation.ToCompactString(),
		*NewActorLocation.ToCompactString(),
		DeltaZ,
		bHasVisualBounds ? 1 : 0);

	return true;
}

void AAeyerjiRewardPresentationActor::DiscardStoredRewardAndSpawnedLoot()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	}

	int32 DestroyedPickupCount = 0;
	for (const TWeakObjectPtr<AAeyerjiLootPickup>& Pickup : SpawnedLootPickups)
	{
		if (AAeyerjiLootPickup* LivePickup = Pickup.Get())
		{
			LivePickup->Destroy();
			++DestroyedPickupCount;
		}
	}
	SpawnedLootPickups.Reset();
	PendingLootResults.Reset();
	PrivateRewardBundles.Reset();
	bReleaseInProgress = false;
	bTreasureAutoOpenRequestPending = false;
	SetReleased();

	AJ_LOG(this, TEXT("[Treasure] Discarded Rift reward presentation Chest=%s DestroyedPickups=%d"),
		*GetNameSafe(this),
		DestroyedPickupCount);
}

bool AAeyerjiRewardPresentationActor::GetVisualMeshBounds(FBox& OutBounds) const
{
	OutBounds.Init();

	TArray<UMeshComponent*> MeshComponents;
	GetComponents(MeshComponents);

	bool bHasBounds = false;
	const int32 MeshCount = FMath::Min(MeshComponents.Num(), MaxRewardVisualMeshes);
	for (int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
	{
		UMeshComponent* MeshComponent = MeshComponents[MeshIndex];
		if (!IsValid(MeshComponent) || MeshComponent->ComponentHasTag(TEXT("IgnoreGroundSnap")))
		{
			continue;
		}

		const FBoxSphereBounds ComponentBounds = MeshComponent->CalcBounds(MeshComponent->GetComponentTransform());
		if (!FMath::IsFinite(ComponentBounds.SphereRadius)
			|| !IsFiniteRewardVector(ComponentBounds.Origin)
			|| !IsFiniteRewardVector(ComponentBounds.BoxExtent)
			|| ComponentBounds.SphereRadius <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FBox ComponentBox = ComponentBounds.GetBox();
		if (!bHasBounds)
		{
			OutBounds = ComponentBox;
			bHasBounds = true;
		}
		else
		{
			OutBounds += ComponentBox;
		}
	}

	return bHasBounds;
}

void AAeyerjiRewardPresentationActor::HandleReleaseRequested_Implementation(AActor* Activator)
{
	if (ReleasePolicy != EAeyerjiRewardPresentationReleasePolicy::InteractToRelease)
	{
		AJ_LOG(this, TEXT("[Interaction][RewardPresentation] HandleReleaseRequested ignored because ReleasePolicy=%d Activator=%s"),
			static_cast<int32>(ReleasePolicy),
			*GetNameSafe(Activator));
		return;
	}

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] HandleReleaseRequested Activator=%s"),
		*GetNameSafe(Activator));
	ReleaseStoredLoot(Activator);
}

void AAeyerjiRewardPresentationActor::AutoReleaseStoredLoot()
{
	if (!HasAuthority() || bReleased || bReleaseInProgress)
	{
		return;
	}

	ReleaseStoredLoot(RewardInstigator.Get());
}

void AAeyerjiRewardPresentationActor::RefreshTreasureAutoOpenTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	if (!HasAuthority()
		|| !bTreasureAutoOpenEnabled
		|| bTreasureAutoOpenRequestPending
		|| bReleased
		|| !bInitialized
		|| ReleasePolicy != EAeyerjiRewardPresentationReleasePolicy::InteractToRelease
		|| RewardCount <= 0
		|| TreasureAutoOpenRadius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		TreasureAutoOpenTimerHandle,
		this,
		&AAeyerjiRewardPresentationActor::EvaluateTreasureAutoOpen,
		TreasureAutoOpenPollingInterval,
		true);
	EvaluateTreasureAutoOpen();
}

void AAeyerjiRewardPresentationActor::EvaluateTreasureAutoOpen()
{
	UWorld* World = GetWorld();
	if (!World
		|| !HasAuthority()
		|| !bTreasureAutoOpenEnabled
		|| bTreasureAutoOpenRequestPending
		|| bReleased
		|| !bInitialized
		|| RewardCount <= 0
		|| ReleasePolicy != EAeyerjiRewardPresentationReleasePolicy::InteractToRelease)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
		}
		return;
	}

	const FVector InteractionLocation = GetInteractionLocation_Implementation();
	if (!IsFiniteRewardVector(InteractionLocation))
	{
		return;
	}

	const float RadiusSquared = FMath::Square(TreasureAutoOpenRadius);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AAeyerjiPlayerController* Controller = Cast<AAeyerjiPlayerController>(It->Get());
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!IsValid(Pawn)
			|| Pawn->GetWorld() != World
			|| !IsPawnEligibleForTreasureAutoOpen(Pawn)
			|| FVector::DistSquared2D(Pawn->GetActorLocation(), InteractionLocation) > RadiusSquared)
		{
			continue;
		}

		bTreasureAutoOpenRequestPending = true;
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
		AJ_LOG(this, TEXT("[Treasure] Auto-open accepted Chest=%s Player=%s RequiredLevel=%d Radius=%.1f"),
			*GetNameSafe(this),
			*GetNameSafe(Pawn),
			bTreasureAutoOpenRequiresMaxCharacterLevel
				? UAeyerjiDifficultySettings::GetMaxGameplayLevel()
				: TreasureAutoOpenUnlockLevel,
			TreasureAutoOpenRadius);
		HandleReleaseRequested(Pawn);
		return;
	}
}

bool AAeyerjiRewardPresentationActor::IsPawnEligibleForTreasureAutoOpen(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld())
	{
		return false;
	}

	const UAbilitySystemComponent* AbilitySystemComponent =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true);
	const FGameplayAttribute LevelAttribute = UAeyerjiAttributeSet::GetLevelAttribute();
	if (!AbilitySystemComponent || !AbilitySystemComponent->HasAttributeSetForAttribute(LevelAttribute))
	{
		return false;
	}

	const float RawLevel = AbilitySystemComponent->GetNumericAttribute(LevelAttribute);
	if (!FMath::IsFinite(RawLevel))
	{
		return false;
	}

	const int32 CharacterLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(RawLevel));
	const int32 RequiredLevel = bTreasureAutoOpenRequiresMaxCharacterLevel
		? UAeyerjiDifficultySettings::GetMaxGameplayLevel()
		: FMath::Max(1, TreasureAutoOpenUnlockLevel);
	return CharacterLevel >= RequiredLevel;
}

void AAeyerjiRewardPresentationActor::TrackSpawnedLootPickups(
	const TArray<AAeyerjiLootPickup*>& InSpawnedPickups)
{
	for (AAeyerjiLootPickup* Pickup : InSpawnedPickups)
	{
		if (IsValid(Pickup) && Pickup->GetWorld() == GetWorld())
		{
			SpawnedLootPickups.AddUnique(TWeakObjectPtr<AAeyerjiLootPickup>(Pickup));
		}
	}
}

void AAeyerjiRewardPresentationActor::ConfigureSpawnedPickupsForAutoCollection(
	const TArray<AAeyerjiLootPickup*>& InSpawnedPickups) const
{
	if (!HasAuthority() || !bAutoCollectSpawnedLoot)
	{
		return;
	}

	for (AAeyerjiLootPickup* Pickup : InSpawnedPickups)
	{
		if (IsValid(Pickup) && Pickup->GetWorld() == GetWorld())
		{
			Pickup->ConfigureRuntimeAutoPickup(true, TreasureAutoCollectRadius);
		}
	}
}

void AAeyerjiRewardPresentationActor::RefreshInteractionCollision()
{
	if (!InteractionSphere)
	{
		return;
	}

	const bool bCanBeInteractedWith = ReleasePolicy == EAeyerjiRewardPresentationReleasePolicy::InteractToRelease
		&& !bReleased
		&& RewardCount > 0;
	InteractionSphere->SetCollisionEnabled(bCanBeInteractedWith ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	InteractionSphere->SetSphereRadius(ReleaseInteractionRadius > 0.f ? ReleaseInteractionRadius : 350.f, true);
}

void AAeyerjiRewardPresentationActor::OnRep_RewardSummary()
{
	RefreshInteractionCollision();
	OnRewardSummaryChanged();
}

void AAeyerjiRewardPresentationActor::OnRep_Released()
{
	RefreshInteractionCollision();
	OnRewardSummaryChanged();
	if (bReleased)
	{
		OnRewardReleasedReplicated();
	}
}

void AAeyerjiRewardPresentationActor::OnRep_Initialized()
{
	RefreshInteractionCollision();
	OnRewardSummaryChanged();

	if (bInitialized)
	{
		OnRewardInitializedReplicated();
	}
}

void AAeyerjiRewardPresentationActor::RefreshRewardSummary()
{
	RewardCount = FMath::Min(PendingLootResults.Num(), MaxStoredPublicRewardResults);
	BestRarity = EItemRarity::Common;

	for (const FLootDropResult& Result : PendingLootResults)
	{
		if (IsValidRewardRarity(Result.Rarity)
			&& static_cast<uint8>(Result.Rarity) > static_cast<uint8>(BestRarity))
		{
			BestRarity = Result.Rarity;
		}

		if (!RewardSourceTag.IsValid() && Result.SourceTag.IsValid())
		{
			RewardSourceTag = Result.SourceTag;
		}
	}
}

void AAeyerjiRewardPresentationActor::SanitizeRuntimeSettings()
{
	ReleaseInteractionRadius = FMath::Clamp(
		FMath::IsFinite(ReleaseInteractionRadius) ? ReleaseInteractionRadius : 350.f,
		0.f,
		MaxRewardDistance);
	AutoReleaseDelaySeconds = FMath::Clamp(
		FMath::IsFinite(AutoReleaseDelaySeconds) ? AutoReleaseDelaySeconds : 0.f,
		0.f,
		MaxRewardDurationSeconds);
	LootReleaseScatterRadius = FMath::Clamp(
		FMath::IsFinite(LootReleaseScatterRadius) ? LootReleaseScatterRadius : 0.f,
		0.f,
		MaxRewardDistance);
	LootReleaseScatterYawOffset = FMath::IsFinite(LootReleaseScatterYawOffset)
		? FMath::UnwindDegrees(LootReleaseScatterYawOffset)
		: 0.f;
	GroundSnapTraceStartHeight = FMath::Clamp(
		FMath::IsFinite(GroundSnapTraceStartHeight) ? GroundSnapTraceStartHeight : 250.f,
		0.f,
		MaxRewardDistance);
	GroundSnapTraceDistance = FMath::Clamp(
		FMath::IsFinite(GroundSnapTraceDistance) ? GroundSnapTraceDistance : 2500.f,
		0.f,
		MaxRewardDistance);
	GroundSnapAdditionalZOffset = FMath::Clamp(
		FMath::IsFinite(GroundSnapAdditionalZOffset) ? GroundSnapAdditionalZOffset : 1.f,
		-MaxRewardDistance,
		MaxRewardDistance);
	PresentationLifeSpanAfterRelease = FMath::Clamp(
		FMath::IsFinite(PresentationLifeSpanAfterRelease) ? PresentationLifeSpanAfterRelease : 10.f,
		0.f,
		MaxRewardDurationSeconds);

	const UEnum* ReleasePolicyEnum = StaticEnum<EAeyerjiRewardPresentationReleasePolicy>();
	if (!ReleasePolicyEnum || !ReleasePolicyEnum->IsValidEnumValue(static_cast<int64>(ReleasePolicy)))
	{
		ReleasePolicy = EAeyerjiRewardPresentationReleasePolicy::InteractToRelease;
	}
	if (GroundSnapTraceChannel.GetValue() >= ECC_MAX)
	{
		GroundSnapTraceChannel = ECC_Visibility;
	}
}

void AAeyerjiRewardPresentationActor::SetReleased()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(TreasureAutoOpenTimerHandle);
	}
	bTreasureAutoOpenRequestPending = false;
	bReleased = true;
	RewardCount = 0;
	RefreshInteractionCollision();
	ForceNetUpdate();
}
