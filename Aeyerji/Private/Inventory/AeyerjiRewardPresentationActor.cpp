// AeyerjiRewardPresentationActor.cpp

#include "Inventory/AeyerjiRewardPresentationActor.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Logging/AeyerjiLog.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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

	RefreshInteractionCollision();

	if (bSnapToGroundInConstruction)
	{
		SnapPresentationToGround();
	}
}

bool AAeyerjiRewardPresentationActor::CanInteract_Implementation(AAeyerjiPlayerController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	const bool bHasControllerReward = PrivateRewardBundles.IsEmpty()
		? RewardCount > 0
		: GetPendingPrivateRewardCount(PlayerState) > 0;
	return ReleasePolicy == EAeyerjiRewardPresentationReleasePolicy::InteractToRelease
		&& IsValid(Controller)
		&& !bReleased
		&& bHasControllerReward;
}

FVector AAeyerjiRewardPresentationActor::GetInteractionLocation_Implementation()
{
	return InteractionSphere ? InteractionSphere->GetComponentLocation() : GetActorLocation();
}

float AAeyerjiRewardPresentationActor::GetInteractionRadius_Implementation()
{
	return ReleaseInteractionRadius;
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
	}

	PendingLootResults = InLootResults;
	PrivateRewardBundles.Reset();
	DropMode = InDropMode;
	RewardSourceTag = InSourceTag;
	RewardInstigator = InInstigator;
	LootReleaseOffset = InLootReleaseOffset;
	PresentationLifeSpanAfterRelease = FMath::Max(0.f, InLifeSpanAfterRelease);
	bReleased = false;
	bReleaseInProgress = false;
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

void AAeyerjiRewardPresentationActor::AddPrivateRewardBundle(
	APlayerState* PlayerState,
	const TArray<FLootDropResult>& LootResults,
	const FGameplayTag InSourceTag)
{
	if (!HasAuthority() || !IsValid(PlayerState) || LootResults.IsEmpty())
	{
		return;
	}

	const bool bWasInitialized = bInitialized;
	FPrivateRewardBundle& Bundle = PrivateRewardBundles.FindOrAdd(PlayerState);
	Bundle.SourceTag = InSourceTag;
	for (const FLootDropResult& Result : LootResults)
	{
		FPrivateRewardEntry& Entry = Bundle.Entries.AddDefaulted_GetRef();
		Entry.Result = Result;
	}
	if (!RewardSourceTag.IsValid())
	{
		RewardSourceTag = InSourceTag;
	}
	bInitialized = true;
	bReleased = false;
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
	if (!PrivateRewardBundles.IsEmpty())
	{
		return ReleasePrivateRewardAtTransform(ReleaseTransform, Activator);
	}

	bReleaseInProgress = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoReleaseTimerHandle);
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
	const FAeyerjiLootSpawnSummary SpawnSummary = UAeyerjiInventoryBPFL::SpawnLootResults(
		this,
		PendingLootResults,
		ReleaseTransform.GetLocation(),
		ReleaseTransform.Rotator(),
		/*SeedOverride=*/0,
		DropMode,
		EffectiveInstigator,
		LootReleaseScatterRadius,
		LootReleaseScatterYawOffset);

	PendingLootResults.Reset();
	SetReleased();
	bReleaseInProgress = false;

	OnRewardReleased(EffectiveInstigator);

	if (PresentationLifeSpanAfterRelease > 0.f)
	{
		SetLifeSpan(PresentationLifeSpanAfterRelease);
	}

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Release complete Activator=%s StoredResults=%d SpawnedPickups=%d FailedSpawns=%d LifeSpan=%.1f"),
		*GetNameSafe(EffectiveInstigator),
		SpawnSummary.RequestedResultCount,
		SpawnSummary.SpawnedPickupCount,
		SpawnSummary.FailedSpawnCount,
		PresentationLifeSpanAfterRelease);
	return true;
}

APlayerState* AAeyerjiRewardPresentationActor::ResolvePlayerStateFromActivator(AActor* Activator) const
{
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
	if (!Bundle || GetPendingPrivateRewardCount(PlayerState) <= 0)
	{
		return false;
	}

	bReleaseInProgress = true;
	AActor* RecipientActor = PlayerState->GetPawn();
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
			ReleaseTransform.TransformPosition(ScatterOffset),
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
	}
	if (bAnyPickupSpawned)
	{
		OnRewardReleased(RecipientActor ? RecipientActor : Activator);
	}
	return bAnyPickupSpawned;
}

void AAeyerjiRewardPresentationActor::RefreshPrivateRewardSummary()
{
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
			++RewardCount;
			if (static_cast<uint8>(Entry.Result.Rarity) > static_cast<uint8>(BestRarity))
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
	const FVector TraceStart = ActorLocation + FVector(0.f, 0.f, GroundSnapTraceStartHeight);
	const FVector TraceEnd = ActorLocation - FVector(0.f, 0.f, GroundSnapTraceDistance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AeyerjiRewardPresentationGroundSnap), bGroundSnapTraceComplex);
	QueryParams.AddIgnoredActor(this);
	if (AActor* OwnerActor = GetOwner())
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	FHitResult Hit;
	const ECollisionChannel TraceChannel = GroundSnapTraceChannel.GetValue();
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams))
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
	SetActorLocation(NewActorLocation, false, nullptr, ETeleportType::TeleportPhysics);

	AJ_LOG(this, TEXT("[Interaction][RewardPresentation] Ground snapped Hit=%s OldLocation=%s NewLocation=%s DeltaZ=%.2f BoundsFound=%d"),
		*Hit.ImpactPoint.ToCompactString(),
		*ActorLocation.ToCompactString(),
		*NewActorLocation.ToCompactString(),
		DeltaZ,
		bHasVisualBounds ? 1 : 0);

	return true;
}

bool AAeyerjiRewardPresentationActor::GetVisualMeshBounds(FBox& OutBounds) const
{
	OutBounds.Init();

	TArray<UMeshComponent*> MeshComponents;
	GetComponents(MeshComponents);

	bool bHasBounds = false;
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!IsValid(MeshComponent) || MeshComponent->ComponentHasTag(TEXT("IgnoreGroundSnap")))
		{
			continue;
		}

		const FBoxSphereBounds ComponentBounds = MeshComponent->CalcBounds(MeshComponent->GetComponentTransform());
		if (ComponentBounds.SphereRadius <= KINDA_SMALL_NUMBER)
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
	RewardCount = PendingLootResults.Num();
	BestRarity = EItemRarity::Common;

	for (const FLootDropResult& Result : PendingLootResults)
	{
		if (static_cast<uint8>(Result.Rarity) > static_cast<uint8>(BestRarity))
		{
			BestRarity = Result.Rarity;
		}

		if (!RewardSourceTag.IsValid() && Result.SourceTag.IsValid())
		{
			RewardSourceTag = Result.SourceTag;
		}
	}
}

void AAeyerjiRewardPresentationActor::SetReleased()
{
	bReleased = true;
	RewardCount = 0;
	RefreshInteractionCollision();
	ForceNetUpdate();
}
