// EnemyParentNative.cpp
#include "Enemy/EnemyParentNative.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemGlobals.h"
#include "Logging/AeyerjiLog.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiRewardAttributeSet.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Enemy/AeyerjiEnemyArchetypeData.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/AeyerjiEnemyTraitComponent.h"
#include "AeyerjiGameplayTags.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/OutlineHighlightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrainComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Inventory/AeyerjiGoldPickup.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Progression/AeyerjiRewardConfigComponent.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "TimerManager.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	const FGameplayTag& EnemyParentDeadStateTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
		return Tag;
	}

	constexpr double AllyAlertRepeatCooldownSeconds = 0.5;
	constexpr float MaxAllyAlertRadius = 10000.f;
	constexpr float MaxEncounterRevealDurationSeconds = 60.f;
	constexpr int64 MaxSingleGoldDrop = MAX_int32;

	APlayerState* ResolveGoldRecipientPlayerState(AActor* RewardInstigator)
	{
		if (APawn* Pawn = Cast<APawn>(RewardInstigator))
		{
			return Pawn->GetPlayerState();
		}

		if (AController* Controller = Cast<AController>(RewardInstigator))
		{
			return Controller->PlayerState;
		}

		return nullptr;
	}
}

AEnemyParentNative::AEnemyParentNative()
{
	PrimaryActorTick.bCanEverTick = false;          // Creeps usually tick via AI only
	/* Network */
	bReplicates = true;
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);

	DefaultTeamTag = FGameplayTag::RequestGameplayTag(TEXT("Team.Enemy"));

	OutlineHighlight = CreateDefaultSubobject<UOutlineHighlightComponent>(TEXT("OutlineHighlight"));
	if (OutlineHighlight)
	{
		OutlineHighlight->bAffectAllPrimitivesIfNoExplicitTargets = false;
	}

	if (UCapsuleComponent* RootCapsule = GetCapsuleComponent())
	{
		RootCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
	}

	ArchetypeComponent = CreateDefaultSubobject<UAeyerjiEnemyArchetypeComponent>(TEXT("EnemyArchetypeComponent"));
	LevelingComponent = CreateDefaultSubobject<UAeyerjiLevelingComponent>(TEXT("AeyerjiLeveling"));
	RewardConfigComponent = CreateDefaultSubobject<UAeyerjiRewardConfigComponent>(TEXT("AeyerjiRewardConfig"));

	if (AbilitySystemAeyerji)
	{
		// Non-owned AI only need tags, cues, and minimal effect state on remote clients.
		AbilitySystemAeyerji->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // interact traces
		MeshComp->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block); // enemy click traces
		MeshComp->SetGenerateOverlapEvents(false);
		MeshComp->SetCanEverAffectNavigation(false);
	}
}

void AEnemyParentNative::SetGenericTeamId(const FGenericTeamId& NewID)
{
	TeamId = NewID.GetId();

	if (IGenericTeamAgentInterface* ControllerTeamAgent = Cast<IGenericTeamAgentInterface>(GetController()))
	{
		ControllerTeamAgent->SetGenericTeamId(NewID);
	}
}

void AEnemyParentNative::PostLoad()
{
	Super::PostLoad();
}

#if WITH_EDITOR
void AEnemyParentNative::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AEnemyParentNative, HighlightChannel))
	{
		HighlightChannel = FMath::Clamp(HighlightChannel, 0, 255);

		// Refresh targets so the channel update propagates to the stencil map immediately in editor.
		RefreshEnemyHighlightTargets();
	}
}
#endif
/* ------------------------------------------------------------------ */

void AEnemyParentNative::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (ArchetypeComponent)
	{
		if (!ArchetypeComponent->HasArchetypeData() && ArchetypeData)
		{
			ArchetypeComponent->SetArchetypeData(ArchetypeData, /*bApplyImmediately=*/false);
		}
		ArchetypeComponent->ApplyArchetypeVisuals(/*bAllowInEditor=*/true, /*bForce=*/true);
	}
	RefreshEnemyHighlightTargets();
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (ArchetypeComponent)
	{
		if (!ArchetypeComponent->HasArchetypeData() && ArchetypeData)
		{
			ArchetypeComponent->SetArchetypeData(ArchetypeData, /*bApplyImmediately=*/false);
		}
		ArchetypeComponent->ApplyArchetypeVisuals(/*bAllowInEditor=*/false, /*bForce=*/false);
	}
}

void AEnemyParentNative::BeginPlay()
{
	Super::BeginPlay();
	if (ArchetypeComponent)
	{
		// AAeyerjiCharacter refreshes legacy capsule sizing during Super::BeginPlay. Reassert
		// the centralized archetype value afterward for server collision and every client.
		ArchetypeComponent->ApplyArchetypeCollisionOverrides(/*bAllowInEditor=*/false);
	}
	InitAbilityActorInfo();
	GiveStartupAbilitiesAndEffects();
	ApplyDefaultTeamTags();
	if (HasAuthority() && AttributeSetAeyerji)
	{
		AttributeSetAeyerji->OnDamageTaken.RemoveDynamic(this, &AEnemyParentNative::HandleEnemyDamageTaken);
		AttributeSetAeyerji->OnDamageTaken.AddDynamic(this, &AEnemyParentNative::HandleEnemyDamageTaken);
	}
	if (ArchetypeComponent)
	{
		if (!bApplyArchetypeOnBeginPlay)
		{
			ArchetypeComponent->SetAutoApplyOnBeginPlay(false);
		}
		if (!ArchetypeComponent->HasArchetypeData() && ArchetypeData)
		{
			ArchetypeComponent->SetArchetypeData(ArchetypeData, /*bApplyImmediately=*/false);
		}
	}
	RefreshEnemyHighlightTargets();
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->OnBeginCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshBeginCursorOver);
		MeshComp->OnEndCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshEndCursorOver);
	}
	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (IsValid(Primitive))
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->OnBeginCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshBeginCursorOver);
			Primitive->OnEndCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshEndCursorOver);
		}
	}
	UpdateEnemyHighlightState();

	if (!HasAuthority() && ActiveTeamTag.IsValid())
	{
		ApplyActiveTeamTagToASC(LastAppliedTeamTag);
		LastAppliedTeamTag = ActiveTeamTag;
	}

	ApplyCrowdPerformanceSettings();
}

void AEnemyParentNative::NotifyActorBeginCursorOver()
{
	Super::NotifyActorBeginCursorOver();
	++HoverHighlightRefCount;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::NotifyActorEndCursorOver()
{
	Super::NotifyActorEndCursorOver();
	HoverHighlightRefCount = FMath::Max(0, HoverHighlightRefCount - 1);
	UpdateEnemyHighlightState();
}

/* ------------------------------------------------------------------ */

bool AEnemyParentNative::ResolveDeathFacingRotation(
	const FVector& EnemyLocation,
	const FVector& KillerLocation,
	FRotator& OutFacingRotation,
	const float YawOffsetDegrees)
{
	const auto IsFiniteVector = [](const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	};
	if (!IsFiniteVector(EnemyLocation) || !IsFiniteVector(KillerLocation))
	{
		return false;
	}

	FVector FacingDirection = KillerLocation - EnemyLocation;
	FacingDirection.Z = 0.f;
	if (!FacingDirection.Normalize())
	{
		return false;
	}

	OutFacingRotation = FacingDirection.Rotation();
	OutFacingRotation.Pitch = 0.f;
	OutFacingRotation.Roll = 0.f;
	const float SafeYawOffset = FMath::IsFinite(YawOffsetDegrees)
		? FMath::Clamp(YawOffsetDegrees, -180.f, 180.f)
		: 0.f;
	OutFacingRotation.Yaw = FRotator::NormalizeAxis(OutFacingRotation.Yaw + SafeYawOffset);
	return true;
}

bool AEnemyParentNative::PrepareDeathPresentation(
	AActor* Killer,
	FRotator& OutFacingRotation)
{
	OutFacingRotation = GetActorRotation();
	if (!bFaceKillerOnDeath || !IsValid(Killer))
	{
		return false;
	}

	AActor* FacingTarget = Killer;
	if (const AController* KillerController = Cast<AController>(Killer))
	{
		FacingTarget = KillerController->GetPawn();
	}
	if (!IsValid(FacingTarget)
		|| !ResolveDeathFacingRotation(
			GetActorLocation(),
			FacingTarget->GetActorLocation(),
			OutFacingRotation,
			DeathPresentationYawOffsetDegrees))
	{
		return false;
	}

	// Freeze authoritative AI steering before Blueprint reads the mesh transform to
	// create its detached death presentation. The normal death shutdown follows.
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		AIController->ClearFocus(EAIFocusPriority::Move);
		AIController->SetControlRotation(OutFacingRotation);
	}

	SetActorRotation(OutFacingRotation, ETeleportType::None);
	ForceNetUpdate();
	return true;
}

void AEnemyParentNative::OnDeath_Implementation(AActor* Killer, const float DamageTaken)
{
	static_cast<void>(DamageTaken);
	if (HasAuthority())
	{
		TrySpawnEnemyDeathRewards(Killer);
		TrySpawnEnemyGoldDrop(Killer);
	}

	// Broadcast immediately so encounter logic can react before delayed cleanup runs.
	OnEnemyDied.Broadcast(this);
}

FAeyerjiDeathStateOptions AEnemyParentNative::BuildDeathStateOptionsForOutOfHealth() const
{
	FAeyerjiDeathStateOptions Options = Super::BuildDeathStateOptionsForOutOfHealth();
	if (bPoolManagedBySpawner)
	{
		// Pooled enemies keep their components intact so activation can restore them later.
		Options.bDetachAttachments = false;
		Options.bRemoveFloatingWidgets = false;
		Options.bRegisterCorpseForCleanup = false;
	}
	return Options;
}

void AEnemyParentNative::SetOwningSpawnerPool(AAeyerjiSpawnerGroup* InSpawner, bool bInPoolManaged)
{
	if (!HasAuthority())
	{
		return;
	}

	OwningSpawnerPool = InSpawner;
	bPoolManagedBySpawner = bInPoolManaged;
}

bool AEnemyParentNative::TryReturnToOwningSpawnerPool()
{
	if (!HasAuthority() || !bPoolManagedBySpawner)
	{
		return false;
	}

	AAeyerjiSpawnerGroup* Spawner = OwningSpawnerPool.Get();
	return Spawner ? Spawner->ReturnEnemyToPool(this) : false;
}

void AEnemyParentNative::PrepareForPooledActivation()
{
	if (!HasAuthority())
	{
		return;
	}

	bDeathRewardsRolled = false;
	bDeathGoldRolled = false;
	LastAlertedTarget.Reset();
	LastAlertBroadcastTime = -1.0;
	HoverHighlightRefCount = 0;
	bBrainPausedByEncounterReveal = false;
	bTookDamageSincePooledActivation = false;
	ClearTransientGameplayEffectsForPooledReuse();
	ResetDeathStateForReuse();
	// Death/reveal locking can preserve a prior false damage flag. A fresh checkout is living and damageable.
	bCanBeDamagedBeforeEncounterLock = CanBeDamaged();
	SetEnemyHighlighted(bHighlightOnSpawn);
	GetWorldTimerManager().ClearTimer(EncounterRevealTimerHandle);
	EncounterPresentationState.Phase = EAeyerjiEnemyEncounterPhase::Active;
	EncounterPresentationState.RevealStyle = EAeyerjiEnemyRevealStyle::Immediate;
	EncounterPresentationState.RevealDurationSeconds = 0.f;
	EncounterPresentationState.Revision++;
	ApplyEncounterGameplayLock(false);
	BP_OnPooledEnemyActivated();
	LogMovementActivationState(TEXT("PooledReuse"));
}

void AEnemyParentNative::PrepareForPooledDeactivation()
{
	if (!HasAuthority())
	{
		return;
	}

	LastAlertedTarget.Reset();
	LastAlertBroadcastTime = -1.0;
	HoverHighlightRefCount = 0;
	SetEnemyHighlighted(false);
	SetPooledEncounterInactive();
	BP_OnPooledEnemyDeactivated();
}

void AEnemyParentNative::BeginEncounterReveal(
	const EAeyerjiEnemyRevealStyle RevealStyle,
	const float RevealDurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(EncounterRevealTimerHandle);
	const EAeyerjiEnemyRevealStyle SafeRevealStyle = StaticEnum<EAeyerjiEnemyRevealStyle>()->IsValidEnumValue(static_cast<int64>(RevealStyle))
		? RevealStyle
		: EAeyerjiEnemyRevealStyle::Immediate;
	const float SafeDuration = FMath::IsFinite(RevealDurationSeconds)
		? FMath::Clamp(RevealDurationSeconds, 0.f, MaxEncounterRevealDurationSeconds)
		: 0.f;
	if (SafeRevealStyle == EAeyerjiEnemyRevealStyle::Immediate || SafeDuration <= 0.f)
	{
		CompleteEncounterReveal();
		return;
	}

	EncounterPresentationState.Phase = EAeyerjiEnemyEncounterPhase::Revealing;
	EncounterPresentationState.RevealStyle = SafeRevealStyle;
	EncounterPresentationState.RevealDurationSeconds = SafeDuration;
	EncounterPresentationState.Revision++;
	ApplyEncounterPresentationState();
	ForceNetUpdate();

	GetWorldTimerManager().SetTimer(
		EncounterRevealTimerHandle,
		this,
		&AEnemyParentNative::CompleteEncounterReveal,
		SafeDuration,
		false);
}

void AEnemyParentNative::CompleteEncounterReveal()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(EncounterRevealTimerHandle);
	EncounterPresentationState.Phase = EAeyerjiEnemyEncounterPhase::Active;
	EncounterPresentationState.RevealDurationSeconds = 0.f;
	EncounterPresentationState.Revision++;
	ApplyEncounterPresentationState();
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		// UE 5.8 ResumeLogic does not restart a StateTree that was stopped while the actor was pooled.
		EnemyController->EnsureConfiguredStateTreeRunning(TEXT("EncounterRevealComplete"));
	}
	LogMovementActivationState(TEXT("EncounterRevealComplete"));
	ForceNetUpdate();
}

void AEnemyParentNative::SetPooledEncounterInactive()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(EncounterRevealTimerHandle);
	EncounterPresentationState.Phase = EAeyerjiEnemyEncounterPhase::PooledInactive;
	EncounterPresentationState.RevealStyle = EAeyerjiEnemyRevealStyle::Immediate;
	EncounterPresentationState.RevealDurationSeconds = 0.f;
	EncounterPresentationState.Revision++;
	ApplyEncounterPresentationState();
	ForceNetUpdate();
}

void AEnemyParentNative::ApplyEncounterGameplayLock(const bool bLocked)
{
	if (bLocked && !bEncounterGameplayLocked)
	{
		bCanBeDamagedBeforeEncounterLock = CanBeDamaged();
		bEncounterGameplayLocked = true;
	}
	else if (!bLocked && bEncounterGameplayLocked)
	{
		bEncounterGameplayLocked = false;
	}
	SetCanBeDamaged(bLocked ? false : bCanBeDamagedBeforeEncounterLock);
	SetActorEnableCollision(!bLocked);

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		Capsule->SetCollisionEnabled(bLocked ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
		Capsule->SetGenerateOverlapEvents(!bLocked);
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		// Revealing enemies remain visible and animated while gameplay collision is locked.
		MeshComponent->SetVisibility(EncounterPresentationState.Phase != EAeyerjiEnemyEncounterPhase::PooledInactive, true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetComponentTickEnabled(EncounterPresentationState.Phase != EAeyerjiEnemyEncounterPhase::PooledInactive);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		if (bLocked)
		{
			Movement->SetComponentTickEnabled(false);
			Movement->DisableMovement();
		}
		else
		{
			// Pool/death Blueprint hooks may deactivate or detach CharacterMovement while
			// inactive. Restore every prerequisite before StateTree path-following resumes.
			Movement->Activate(true);
			if (UCapsuleComponent* Capsule = GetCapsuleComponent();
				Capsule && Movement->UpdatedComponent.Get() != Capsule)
			{
				Movement->SetUpdatedComponent(Capsule);
			}
			Movement->SetComponentTickEnabled(true);
			Movement->SetMovementMode(MOVE_Walking);
			if (UAeyerjiCharacterMovementComponent* AeyerjiMovement =
				Cast<UAeyerjiCharacterMovementComponent>(Movement))
			{
				AeyerjiMovement->ForceRootedStateRefresh();
			}
		}
	}

	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		if (bLocked)
		{
			AIController->StopMovement();
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
			if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(AIController))
			{
				EnemyController->SetTargetActor(nullptr);
				EnemyController->ClearLastKnownTarget();
			}
		}

		if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(AIController))
		{
			// Hidden/revealing enemies must not occupy Detour Crowd's limited agent slots.
			EnemyController->SetPathFollowingGameplayEnabled(
				!bLocked,
				bLocked ? TEXT("EncounterGameplayLock") : TEXT("EncounterGameplayUnlock"));
		}

		if (UBrainComponent* Brain = AIController->GetBrainComponent())
		{
			if (bLocked && !Brain->IsPaused())
			{
				Brain->PauseLogic(TEXT("EncounterReveal"));
				bBrainPausedByEncounterReveal = true;
			}
			else if (!bLocked && bBrainPausedByEncounterReveal)
			{
				Brain->ResumeLogic(TEXT("EncounterRevealComplete"));
				bBrainPausedByEncounterReveal = false;
			}
		}

		if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
		{
			Perception->SetComponentTickEnabled(!bLocked);
			if (!bLocked)
			{
				// Players already inside sight range may not generate a new stimulus after the reveal unlocks.
				Perception->RequestStimuliListenerUpdate();
			}
		}
	}
}

void AEnemyParentNative::LogMovementActivationState(const TCHAR* Phase) const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	const FGameplayTag RootedTag =
		FGameplayTag::RequestGameplayTag(TEXT("Player.States.Rooted"), /*ErrorIfNotFound=*/false);
	const int32 RootedTagCount = AbilitySystemAeyerji && RootedTag.IsValid()
		? AbilitySystemAeyerji->GetTagCount(RootedTag)
		: 0;

	AJ_LOG_VERBOSITY(Verbose, this,
		TEXT("[EnemyActivation] MovementState Phase=%s Pawn=%s Active=%d Tick=%d Registered=%d Mode=%d Updated=%s Capsule=%s CapsuleCollision=%d RootPhysics=%d MeshPhysics=%d MaxWalkSpeed=%.1f RootedTagCount=%d"),
		Phase ? Phase : TEXT("Unknown"),
		*GetNameSafe(this),
		Movement && Movement->IsActive() ? 1 : 0,
		Movement && Movement->IsComponentTickEnabled() ? 1 : 0,
		Movement && Movement->IsRegistered() ? 1 : 0,
		Movement ? static_cast<int32>(Movement->MovementMode) : -1,
		Movement ? *GetNameSafe(Movement->UpdatedComponent) : TEXT("None"),
		*GetNameSafe(Capsule),
		Capsule ? static_cast<int32>(Capsule->GetCollisionEnabled()) : -1,
		Capsule && Capsule->IsSimulatingPhysics() ? 1 : 0,
		MeshComponent && MeshComponent->IsSimulatingPhysics() ? 1 : 0,
		Movement ? Movement->MaxWalkSpeed : -1.f,
		RootedTagCount);
}

void AEnemyParentNative::ApplyEncounterPresentationState()
{
	switch (EncounterPresentationState.Phase)
	{
	case EAeyerjiEnemyEncounterPhase::PooledInactive:
		ApplyEncounterGameplayLock(true);
		break;
	case EAeyerjiEnemyEncounterPhase::Revealing:
		ApplyEncounterGameplayLock(true);
		BP_OnEncounterReveal(
			EncounterPresentationState.RevealStyle,
			EncounterPresentationState.RevealDurationSeconds);
		break;
	case EAeyerjiEnemyEncounterPhase::Active:
	default:
		ApplyEncounterGameplayLock(false);
		break;
	}
}

void AEnemyParentNative::ClearTransientGameplayEffectsForPooledReuse()
{
	if (!HasAuthority() || !AbilitySystemAeyerji)
	{
		return;
	}

	AbilitySystemAeyerji->CancelAllAbilities();
	const TArray<FActiveGameplayEffectHandle> ActiveHandles =
		AbilitySystemAeyerji->GetActiveEffects(FGameplayEffectQuery());
	for (const FActiveGameplayEffectHandle& Handle : ActiveHandles)
	{
		const FActiveGameplayEffect* ActiveEffect = AbilitySystemAeyerji->GetActiveGameplayEffect(Handle);
		if (!ActiveEffect || !ActiveEffect->Spec.Def)
		{
			continue;
		}

		const AActor* OriginalInstigator = ActiveEffect->Spec.GetContext().GetOriginalInstigator();
		const bool bExternallyAuthored = OriginalInstigator && OriginalInstigator != this;
		const bool bFiniteDuration = ActiveEffect->Spec.Def->DurationPolicy != EGameplayEffectDurationType::Infinite;
		if (bExternallyAuthored || bFiniteDuration)
		{
			AbilitySystemAeyerji->RemoveActiveGameplayEffect(Handle);
		}
	}
}

void AEnemyParentNative::OnRep_EncounterPresentationState()
{
	ApplyEncounterPresentationState();
}

bool AEnemyParentNative::TrySpawnEnemyDeathRewards(AActor* RewardInstigator)
{
	if (!HasAuthority() || !bSpawnNormalDeathLoot || bDeathRewardsRolled)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	ULootService* LootService = GameInstance ? GameInstance->GetSubsystem<ULootService>() : nullptr;
	if (!LootService)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LootReward] EnemyDeathReward skipped Enemy=%s Reason=MissingLootService"),
			*GetNameSafe(this));
		return false;
	}

	if (DeathLootDropMode == EItemDropDistributionMode::DropOnlyForInstigator
		&& !IsValid(RewardInstigator))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LootReward] EnemyDeathReward skipped Enemy=%s Reason=MissingCreditedInstigator"),
			*GetNameSafe(this));
		return false;
	}

	FLootContext RuntimeContext = DeathLootContext;
	if (!RuntimeContext.PlayerActor.IsValid())
	{
		RuntimeContext.PlayerActor = RewardInstigator;
	}
	RuntimeContext.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(CachedScaledLevel > 0 ? CachedScaledLevel : RuntimeContext.EnemyLevel);
	if (RuntimeContext.PlayerLevel > 0)
	{
		RuntimeContext.PlayerLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(RuntimeContext.PlayerLevel);
	}
	RuntimeContext.DifficultyScale = CachedDifficultyScale > 0.f
		? CachedDifficultyScale
		: (FMath::IsFinite(RuntimeContext.DifficultyScale) ? FMath::Max(0.f, RuntimeContext.DifficultyScale) : 0.f);
	const double CombinedRewardQuality =
		static_cast<double>(RuntimeContext.RewardQualityMultiplier) * CachedRewardQualityMultiplier;
	RuntimeContext.RewardQualityMultiplier = FMath::IsFinite(CombinedRewardQuality)
		? static_cast<float>(FMath::Clamp(CombinedRewardQuality, 0.0, 100.0))
		: 0.f;
	if (!RuntimeContext.SourceTag.IsValid())
	{
		RuntimeContext.SourceTag = CachedScalingSourceTag.IsValid()
			? CachedScalingSourceTag
			: AeyerjiTags::Loot_Source_NormalEnemy;
	}

	const FVector SpawnLocation = GetActorLocation();
	const FRotator SpawnRotation = GetActorRotation();
	if (SpawnLocation.ContainsNaN() || SpawnRotation.ContainsNaN())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LootReward] EnemyDeathReward skipped Enemy=%s Reason=InvalidSpawnTransform"),
			*GetNameSafe(this));
		return false;
	}
	bDeathRewardsRolled = true;

	if (bUseDeathLootMultiDrop)
	{
		UAeyerjiInventoryBPFL::SpawnMultiDropFromContext(
			this,
			RuntimeContext,
			DeathLootMultiDropConfig,
			SpawnLocation,
			SpawnRotation,
			DeathLootDropMode,
			RewardInstigator);

		UE_LOG(LogTemp, Display,
			TEXT("[LootReward] EnemyDeathReward multi Enemy=%s SourceTag=%s Level=%d Instigator=%s Location=%s Buckets=%d"),
			*GetNameSafe(this),
			*RuntimeContext.SourceTag.ToString(),
			RuntimeContext.EnemyLevel,
			*GetNameSafe(RewardInstigator),
			*SpawnLocation.ToCompactString(),
			DeathLootMultiDropConfig.Buckets.Num());
		return true;
	}

	const FLootDropResult Result = LootService->RollLoot(RuntimeContext);
	if (!Result.ItemDefinition && Result.ItemDefinitionKey.IsNone())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[LootReward] EnemyDeathReward none Enemy=%s SourceTag=%s EnemyLevel=%d PlayerLevel=%d Instigator=%s"),
			*GetNameSafe(this),
			*RuntimeContext.SourceTag.ToString(),
			RuntimeContext.EnemyLevel,
			RuntimeContext.PlayerLevel,
			*GetNameSafe(RewardInstigator));
		return false;
	}

	AAeyerjiLootPickup* SpawnedPickup = UAeyerjiInventoryBPFL::SpawnLootFromResult(
		this,
		Result,
		SpawnLocation,
		SpawnRotation,
		/*SeedOverride=*/0,
		DeathLootDropMode,
		RewardInstigator);

	UE_LOG(LogTemp, Display,
		TEXT("[LootReward] EnemyDeathReward single Enemy=%s SourceTag=%s Level=%d Rarity=%d DefinitionKey=%s Instigator=%s Pickup=%s"),
		*GetNameSafe(this),
		*RuntimeContext.SourceTag.ToString(),
		RuntimeContext.EnemyLevel,
		static_cast<int32>(Result.Rarity),
		*Result.ItemDefinitionKey.ToString(),
		*GetNameSafe(RewardInstigator),
		*GetNameSafe(SpawnedPickup));

	return SpawnedPickup != nullptr;
}

bool AEnemyParentNative::TrySpawnEnemyGoldDrop(AActor* RewardInstigator)
{
	if (!HasAuthority() || !DeathGoldDropConfig.bEnabled || bDeathGoldRolled)
	{
		return false;
	}

	bDeathGoldRolled = true;

	const float SafeDropChance = FMath::IsFinite(DeathGoldDropConfig.DropChance)
		? FMath::Clamp(DeathGoldDropConfig.DropChance, 0.f, 1.f)
		: 0.f;
	if (SafeDropChance <= 0.f || FMath::FRand() > SafeDropChance)
	{
		return false;
	}

	if (DeathGoldDropConfig.DropOwnershipMode == EItemDropDistributionMode::DropOnlyForInstigator
		&& !IsValid(RewardInstigator))
	{
		return false;
	}

	const int32 EnemyLevel = FMath::Max(0, CachedScaledLevel);
	double Amount = static_cast<double>(FMath::Max<int64>(0, DeathGoldDropConfig.BaseAmount));
	if (DeathGoldDropConfig.Variance > 0)
	{
		const int32 Variance = static_cast<int32>(FMath::Min<int64>(DeathGoldDropConfig.Variance, MAX_int32));
		Amount += static_cast<double>(FMath::RandRange(-Variance, Variance));
	}
	const double PerLevelScalar = FMath::IsFinite(DeathGoldDropConfig.PerLevelScalar)
		? FMath::Max(0.0, static_cast<double>(DeathGoldDropConfig.PerLevelScalar))
		: 0.0;
	Amount += static_cast<double>(EnemyLevel) * PerLevelScalar;

	float SourceMultiplier = 1.f;
	if (CachedScalingSourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_Boss))
	{
		SourceMultiplier = FMath::IsFinite(DeathGoldDropConfig.BossMultiplier) ? FMath::Max(0.f, DeathGoldDropConfig.BossMultiplier) : 0.f;
	}
	else if (CachedScalingSourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_MiniBoss))
	{
		SourceMultiplier = FMath::IsFinite(DeathGoldDropConfig.MiniBossMultiplier) ? FMath::Max(0.f, DeathGoldDropConfig.MiniBossMultiplier) : 0.f;
	}
	else if (CachedScalingSourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_Elite))
	{
		SourceMultiplier = FMath::IsFinite(DeathGoldDropConfig.EliteMultiplier) ? FMath::Max(0.f, DeathGoldDropConfig.EliteMultiplier) : 0.f;
	}

	const double ResolvedAmount = Amount * static_cast<double>(SourceMultiplier);
	if (!FMath::IsFinite(ResolvedAmount) || ResolvedAmount < 0.5)
	{
		return false;
	}
	const int64 GoldToDrop = FMath::RoundToInt64(FMath::Min(ResolvedAmount, static_cast<double>(MaxSingleGoldDrop)));
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation());
	if (!SpawnTransform.IsValid())
	{
		return false;
	}
	int32 SpawnedCount = 0;

	if (DeathGoldDropConfig.DropOwnershipMode == EItemDropDistributionMode::DropOnlyForInstigator)
	{
		APlayerState* EligiblePS = ResolveGoldRecipientPlayerState(RewardInstigator);
		if (!EligiblePS)
		{
			return false;
		}

		SpawnedCount += AAeyerjiGoldPickup::SpawnGold(*World, GoldToDrop, SpawnTransform, DeathGoldDropConfig.PickupClass, EligiblePS) ? 1 : 0;
	}
	else if (AGameStateBase* GameState = World->GetGameState())
	{
		for (APlayerState* RecipientPlayerState : GameState->PlayerArray)
		{
			if (!RecipientPlayerState || !RecipientPlayerState->GetPawn())
			{
				continue;
			}

			SpawnedCount += AAeyerjiGoldPickup::SpawnGold(*World, GoldToDrop, SpawnTransform, DeathGoldDropConfig.PickupClass, RecipientPlayerState) ? 1 : 0;
		}
	}

	AJ_LOG(this,
		TEXT("[Currency] EnemyGoldDrop Enemy=%s Gold=%lld Spawned=%d Mode=%d Instigator=%s SourceTag=%s"),
		*GetNameSafe(this),
		GoldToDrop,
		SpawnedCount,
		static_cast<int32>(DeathGoldDropConfig.DropOwnershipMode),
		*GetNameSafe(RewardInstigator),
		*CachedScalingSourceTag.ToString());

	return SpawnedCount > 0;
}

void AEnemyParentNative::ApplyCrowdPerformanceSettings()
{
	if (!bEnableCrowdPerformanceSettings || bIgnoreCrowdPerformanceSettings)
	{
		return;
	}

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (bEnableUpdateRateOptimizations)
	{
		MeshComp->bEnableUpdateRateOptimizations = true;
	}

	if (bOnlyTickPoseWhenRendered)
	{
		// Listen-server authority must continue montage evaluation so gameplay notifies do not depend on rendering.
		MeshComp->VisibilityBasedAnimTickOption = HasAuthority()
			? EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered
			: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	if (CrowdMinLOD > 0)
	{
		MeshComp->OverrideMinLOD(CrowdMinLOD);
	}

	if (CrowdForcedLOD > 0)
	{
		MeshComp->SetForcedLOD(CrowdForcedLOD);
	}

	if (FMath::IsFinite(CrowdMaxDrawDistance) && CrowdMaxDrawDistance > 0.f)
	{
		MeshComp->LDMaxDrawDistance = CrowdMaxDrawDistance;
		MeshComp->bAllowCullDistanceVolume = true;
	}

	if (bDisableDynamicShadows)
	{
		MeshComp->SetCastShadow(false);
	}
}

void AEnemyParentNative::InitAbilityActorInfo()
{
	if (!AbilitySystemAeyerji)
	{
		UE_LOG(LogTemp, Warning, TEXT("APlayerParentNative::InitAbilityActorInfo AbilitySystemAeyerji is null"));
		return;
	}
	if (bASCInitialised)
	{
		UE_LOG(LogTemp, Warning, TEXT("APlayerParentNative::InitAbilityActorInfo() bASCInitialised already true"));
		return;
	}
    AbilitySystemAeyerji->InitAbilityActorInfo(this, this);
    // Collapse duplicate main attribute sets caused by ASC DefaultStartingData plus actor-owned subobjects.
    EnsurePrimaryAttributeSetRegistered();

    // Ensure Reward AttributeSet exists so we can read XPRewardBase on death.
    if (!AbilitySystemAeyerji->GetSet<UAeyerjiRewardAttributeSet>())
    {
        const FName RewardSetName = MakeUniqueObjectName(this, UAeyerjiRewardAttributeSet::StaticClass(), TEXT("AeyerjiRewardAttributeSet"));
        UAeyerjiRewardAttributeSet* RewardSet = NewObject<UAeyerjiRewardAttributeSet>(this, UAeyerjiRewardAttributeSet::StaticClass(), RewardSetName);
        AbilitySystemAeyerji->AddAttributeSetSubobject(RewardSet);
    }

    // Hook death delegate (server only)
    BindDeathEvent();
    BindCrowdControlEvents();
	
	SetGenericTeamId(FGenericTeamId(TeamId));   // 0 = players

	if (HasAuthority())
	{
		AJ_LOG(this, TEXT("HandleASCReady - Adding startup abilities (server)"));
		AddStartupAbilities();
	}

	OnAbilitySystemReady.Broadcast();
	
	// OPTIONAL: Set tag relationship tables, etc.
}

void AEnemyParentNative::GiveStartupAbilitiesAndEffects()
{
	if (bStartupGiven || !AbilitySystemAeyerji || !HasAuthority())
	{
		return;        // Only once, server side
	}

	GrantAbilityList(StartupAbilities, 1);
	ApplyEffectList(StartupEffects, 1.f);

	// Startup effects can author final vitals for some enemy variants, so push that state immediately.
	AbilitySystemAeyerji->ForceReplication();
	ForceNetUpdate();

	bStartupGiven = true;
}

void AEnemyParentNative::ApplyDefaultTeamTags()
{
	if (!HasAuthority() || !DefaultTeamTag.IsValid())
	{
		return;
	}

	if (!ActiveTeamTag.IsValid())
	{
		SetActiveTeamTag(DefaultTeamTag);
		return;
	}

	ApplyActiveTeamTagToASC(ActiveTeamTag);
}

void AEnemyParentNative::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();

	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(this, /*LookForComponent*/ true))
	{
		ASC->GetOwnedGameplayTags(TagContainer);
	}

	if (ActiveTeamTag.IsValid())
	{
		TagContainer.AddTag(ActiveTeamTag);
	}
}

void AEnemyParentNative::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEnemyParentNative, ActiveTeamTag);
	DOREPLIFETIME(AEnemyParentNative, EncounterPresentationState);
}

void AEnemyParentNative::OnRep_ActiveTeamTag()
{
	ApplyActiveTeamTagToASC(LastAppliedTeamTag);
	LastAppliedTeamTag = ActiveTeamTag;
}

void AEnemyParentNative::SetActiveTeamTag(const FGameplayTag& NewTag)
{
	if (!HasAuthority() || !NewTag.IsValid())
	{
		return;
	}

	const FGameplayTag OldTag = ActiveTeamTag;
	ActiveTeamTag = NewTag;
	ApplyActiveTeamTagToASC(OldTag);
	LastAppliedTeamTag = ActiveTeamTag;
}

void AEnemyParentNative::ApplyActiveTeamTagToASC(const FGameplayTag& OldTag)
{
	if (!AbilitySystemAeyerji)
	{
		return;
	}

	if (OldTag.IsValid() && OldTag != ActiveTeamTag)
	{
		if (AbilitySystemAeyerji->HasMatchingGameplayTag(OldTag))
		{
			AbilitySystemAeyerji->RemoveLooseGameplayTag(OldTag);
		}
	}

	if (ActiveTeamTag.IsValid() && !AbilitySystemAeyerji->HasMatchingGameplayTag(ActiveTeamTag))
	{
		AbilitySystemAeyerji->AddLooseGameplayTag(ActiveTeamTag);
	}
}

// Server-only: apply archetype data through the runtime component.
void AEnemyParentNative::ApplyArchetypeData()
{
	if (HasAuthority() && ArchetypeComponent)
	{
		if (!ArchetypeComponent->HasArchetypeData() && ArchetypeData)
		{
			ArchetypeComponent->SetArchetypeData(ArchetypeData, /*bApplyImmediately=*/false);
		}
		ArchetypeComponent->ApplyArchetype();
	}
}

void AEnemyParentNative::NotifyNearbyAlliesOfTarget(AActor* Target)
{
	const float SafeAlertRadius = FMath::IsFinite(AllyAlertRadius)
		? FMath::Clamp(AllyAlertRadius, 0.f, MaxAllyAlertRadius)
		: 0.f;
	if (!HasAuthority() || SafeAlertRadius <= 0.f || !IsValid(Target) || IsActorBeingDestroyed())
	{
		return;
	}

	const FGameplayTag DeadTag = EnemyParentDeadStateTag();
	if (!DeadTag.IsValid() || !IsAlive(DeadTag) || !IsAliveAndHostile(Target, DeadTag))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (LastAlertedTarget.Get() == Target
		&& LastAlertBroadcastTime >= 0.0
		&& (Now - LastAlertBroadcastTime) < AllyAlertRepeatCooldownSeconds)
	{
		return;
	}

	if (bRequireNavigableAllyAlertPath)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		{
			return;
		}
	}

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		GetActorLocation(),
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(SafeAlertRadius),
		QueryParams);

	const FGenericTeamId MyTeamId = GetGenericTeamId();
	TSet<AEnemyParentNative*> ProcessedEnemies;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AEnemyParentNative* NearbyEnemy = Cast<AEnemyParentNative>(Overlap.GetActor());
		if (!IsValid(NearbyEnemy) || NearbyEnemy == this || NearbyEnemy->IsActorBeingDestroyed())
		{
			continue;
		}

		if (ProcessedEnemies.Contains(NearbyEnemy))
		{
			continue;
		}
		ProcessedEnemies.Add(NearbyEnemy);

		if (NearbyEnemy->GetGenericTeamId() != MyTeamId || !NearbyEnemy->IsAlive(DeadTag))
		{
			continue;
		}

		AEnemyAIController* NearbyController = Cast<AEnemyAIController>(NearbyEnemy->GetController());
		if (!NearbyController || NearbyController->GetTargetActor() == Target)
		{
			continue;
		}

		if (bRequireNavigableAllyAlertPath && !HasNavigableAlertPathTo(NearbyEnemy))
		{
			continue;
		}

		NearbyEnemy->ReceiveAllyAlert(Target, this);
	}

	LastAlertedTarget = Target;
	LastAlertBroadcastTime = Now;
}

void AEnemyParentNative::ReceiveAllyAlert(AActor* Target, const AEnemyParentNative* Notifier)
{
	if (!HasAuthority() || !IsValid(Target) || IsActorBeingDestroyed())
	{
		return;
	}

	const FGameplayTag DeadTag = EnemyParentDeadStateTag();
	if (!DeadTag.IsValid() || !IsAlive(DeadTag))
	{
		return;
	}

	if (Notifier && Notifier != this && Notifier->GetGenericTeamId() != GetGenericTeamId())
	{
		return;
	}

	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->TryAcquireTarget(Target, /*bBroadcastAllyAlert=*/false);
	}
}

void AEnemyParentNative::HandleEnemyDamageTaken(AActor* VictimActor, AActor* InstigatorActor, const float DamageTaken, FGameplayTag DamageType)
{
	static_cast<void>(DamageType);

	if (!HasAuthority()
		|| VictimActor != this
		|| !FMath::IsFinite(DamageTaken)
		|| DamageTaken <= 0.f
		|| !IsValid(InstigatorActor)
		|| InstigatorActor == this
		|| IsActorBeingDestroyed())
	{
		return;
	}
	bTookDamageSincePooledActivation = true;

	AActor* ThreatActor = InstigatorActor;
	if (AController* InstigatorController = Cast<AController>(ThreatActor))
	{
		ThreatActor = InstigatorController->GetPawn();
	}

	if (!IsValid(ThreatActor) || ThreatActor == this)
	{
		return;
	}

	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->NotifyDamagedBy(ThreatActor);
	}
}

bool AEnemyParentNative::IsAliveAndHostile(const AActor* Candidate, FGameplayTag InvalidTag) const
{
	const AAIController* AI = Cast<AAIController>(GetController());
	if (!AI || !Candidate)
	{
		return false;
	}

	if (InvalidTag.IsValid())
	{
		if (const UAbilitySystemComponent* CandidateASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate, /*LookForComponent=*/true))
		{
			if (CandidateASC->HasMatchingGameplayTag(InvalidTag))
			{
				return false;
			}
		}
	}

	const ETeamAttitude::Type Att = AI->GetTeamAttitudeTowards(*Candidate);
	return Att == ETeamAttitude::Hostile;
}

bool AEnemyParentNative::IsAlive(FGameplayTag DeathTag) const
{
	if (!DeathTag.IsValid()) {
		UE_LOG(LogTemp, Error, TEXT("DeathTag is invalid - investigate as this will break functionality"));
		return false;
	}

	if (AbilitySystemAeyerji)
	{
		if (AbilitySystemAeyerji->HasMatchingGameplayTag(DeathTag))
		{
			return false;
		}
		return true;
	}
	UE_LOG(LogTemp, Error, TEXT("AbilitySystemAeyerji is invalid - investigate as this will break functionality in IsAlive()"));
	return false;
}

void AEnemyParentNative::SetArchetypeAndApply(UAeyerjiEnemyArchetypeData* NewArchetypeData, bool bApplyImmediately)
{
	if (!HasAuthority())
	{
		return;
	}

	ArchetypeData = NewArchetypeData;

	if (!ArchetypeComponent)
	{
		return;
	}

	ArchetypeComponent->SetArchetypeData(NewArchetypeData, bApplyImmediately);
}

// Adjusts a scaling value using archetype multipliers when the attribute matches a supported category.
bool AEnemyParentNative::ApplyArchetypeStatMultipliers(const FName& AttributeName, float& InOutValue) const
{
	if (!FMath::IsFinite(InOutValue))
	{
		InOutValue = 0.f;
		return false;
	}

	const FAeyerjiEnemyStatMultipliers* Mults = ArchetypeComponent ? ArchetypeComponent->GetStatMultipliers() : nullptr;
	if (!Mults && ArchetypeData)
	{
		Mults = &ArchetypeData->StatMultipliers;
	}
	if (!Mults)
	{
		return false;
	}
	FString NameString = AttributeName.ToString();
	int32 DotIndex = INDEX_NONE;
	if (NameString.FindChar('.', DotIndex))
	{
		NameString = NameString.Mid(DotIndex + 1);
	}

	const FName StrippedName(*NameString);
	if (StrippedName == TEXT("HP") || StrippedName == TEXT("HPMax"))
	{
		InOutValue *= FMath::IsFinite(Mults->HealthMultiplier) ? FMath::Max(0.f, Mults->HealthMultiplier) : 0.f;
		return true;
	}

	if (StrippedName == TEXT("AttackDamage"))
	{
		InOutValue *= FMath::IsFinite(Mults->DamageMultiplier) ? FMath::Max(0.f, Mults->DamageMultiplier) : 0.f;
		return true;
	}

	if (StrippedName == TEXT("RunSpeed") || StrippedName == TEXT("WalkSpeed"))
	{
		InOutValue *= FMath::IsFinite(Mults->MoveSpeedMultiplier) ? FMath::Max(0.f, Mults->MoveSpeedMultiplier) : 0.f;
		return true;
	}

	if (StrippedName == TEXT("AttackSpeed"))
	{
		InOutValue *= FMath::IsFinite(Mults->AttackRateMultiplier) ? FMath::Max(0.f, Mults->AttackRateMultiplier) : 0.f;
		return true;
	}

	if (StrippedName == TEXT("AttackCooldown"))
	{
		const float SafeRate = FMath::IsFinite(Mults->AttackRateMultiplier)
			? FMath::Max(0.01f, Mults->AttackRateMultiplier)
			: 1.f;
		InOutValue /= SafeRate;
		return true;
	}

	return false;
}

// Grants abilities if they are not already present on the ASC.
void AEnemyParentNative::GrantAbilityList(const TArray<TSubclassOf<UGameplayAbility>>& Abilities, int32 AbilityLevel)
{
	if (!HasAuthority() || !AbilitySystemAeyerji || Abilities.IsEmpty())
	{
		return;
	}

	const int32 ClampedLevel = FMath::Max(1, AbilityLevel);
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
	{
		if (!*AbilityClass)
		{
			continue;
		}

		if (AbilitySystemAeyerji->FindAbilitySpecFromClass(AbilityClass))
		{
			continue;
		}

		AbilitySystemAeyerji->GiveAbility(FGameplayAbilitySpec(AbilityClass, ClampedLevel, INDEX_NONE, this));
	}
}

// Applies gameplay effects to self at a consistent level.
void AEnemyParentNative::ApplyEffectList(const TArray<TSubclassOf<UGameplayEffect>>& Effects, float EffectLevel)
{
	if (!HasAuthority() || !AbilitySystemAeyerji || Effects.IsEmpty())
	{
		return;
	}

	const float ClampedLevel = FMath::IsFinite(EffectLevel) ? FMath::Max(0.01f, EffectLevel) : 1.f;
	for (const TSubclassOf<UGameplayEffect>& GEClass : Effects)
	{
		if (!GEClass)
		{
			continue;
		}

		const FGameplayEffectContextHandle Ctx = AbilitySystemAeyerji->MakeEffectContext();
		FGameplayEffectSpecHandle Spec = AbilitySystemAeyerji->MakeOutgoingSpec(GEClass, ClampedLevel, Ctx);

		if (Spec.IsValid())
		{
			AbilitySystemAeyerji->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
}

// Attaches trait components by class if they are not already present.
void AEnemyParentNative::AddTraitComponents(const TArray<TSubclassOf<UAeyerjiEnemyTraitComponent>>& TraitComponents)
{
	if (TraitComponents.IsEmpty())
	{
		return;
	}

	for (const TSubclassOf<UAeyerjiEnemyTraitComponent>& TraitClass : TraitComponents)
	{
		if (!*TraitClass)
		{
			continue;
		}

		if (GetComponentByClass(TraitClass))
		{
			continue;
		}

		UAeyerjiEnemyTraitComponent* NewTrait = NewObject<UAeyerjiEnemyTraitComponent>(this, TraitClass);
		if (!NewTrait)
		{
			continue;
		}

		AddInstanceComponent(NewTrait);
		NewTrait->OnComponentCreated();
		NewTrait->RegisterComponent();
	}
}

bool AEnemyParentNative::HasNavigableAlertPathTo(const AEnemyParentNative* OtherEnemy) const
{
	if (!IsValid(OtherEnemy))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		return false;
	}

	AActor* PathContext = const_cast<AEnemyParentNative*>(OtherEnemy);
	UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(
		World,
		GetActorLocation(),
		OtherEnemy->GetActorLocation(),
		PathContext);

	return Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() > 0;
}

void AEnemyParentNative::SetEnemyHighlighted(bool bInHighlighted)
{
	if (!OutlineHighlight)
	{
		return;
	}

	if (bEnemyHighlighted == bInHighlighted)
	{
		return;
	}

	bEnemyHighlighted = bInHighlighted;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::RefreshEnemyHighlightTargets()
{
	if (!OutlineHighlight)
	{
		return;
	}

	const bool bWasHighlighted = bEnemyHighlighted;

	OutlineHighlight->SetHighlighted(false);
	OutlineHighlight->ExplicitTargets.Reset();

	ConfigureEnemyOutlineComponent();

	const int32 ChannelIndex = FMath::Clamp(HighlightChannel, 0, 255);

	// When using palette-defined channels (0-7) keep the default mapping that ships with the component.
	// For any higher channel we ensure a direct identity mapping so the custom LUT row is used.
	if (ChannelIndex >= 8)
	{
		OutlineHighlight->RarityIndexToStencil.FindOrAdd(ChannelIndex) = ChannelIndex;
	}
	else
	{
		// Remove overrides that might have been injected previously so defaults (0->1, 1->2, ...) remain intact.
		if (int32* Existing = OutlineHighlight->RarityIndexToStencil.Find(ChannelIndex))
		{
			const int32 DefaultValue = ChannelIndex + 1;
			if (*Existing == ChannelIndex)
			{
				OutlineHighlight->RarityIndexToStencil.Remove(ChannelIndex);
			}
			else if (*Existing != DefaultValue)
			{
				// Leave user customization in place if they intentionally mapped to a different stencil.
			}
		}
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		OutlineHighlight->ExplicitTargets.Add(MeshComp);
	}

	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (IsValid(Primitive))
		{
			OutlineHighlight->ExplicitTargets.Add(Primitive);
		}
	}

	OutlineHighlight->InitializeFromRarityIndex(ChannelIndex);
	bEnemyHighlighted = bWasHighlighted;
	UpdateEnemyHighlightState();
}

bool AEnemyParentNative::IsHoverTargetComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return true;
	}

	if (Component == GetMesh())
	{
		return true;
	}

	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (Primitive && Component == Primitive)
		{
			return true;
		}
	}

	return Component->GetOwner() == this;
}

void AEnemyParentNative::UpdateEnemyHighlightState()
{
	if (!OutlineHighlight)
	{
		return;
	}

	const bool bShouldHighlight = bEnemyHighlighted || HoverHighlightRefCount > 0 || bHighlightOnSpawn;
	OutlineHighlight->SetHighlighted(bShouldHighlight);
}

void AEnemyParentNative::HandleMeshBeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, VeryVerbose, TEXT("Enemy %s hover begin on %s"), *GetName(), *GetNameSafe(TouchedComponent));
	++HoverHighlightRefCount;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::HandleMeshEndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, VeryVerbose, TEXT("Enemy %s hover end on %s"), *GetName(), *GetNameSafe(TouchedComponent));
	HoverHighlightRefCount = FMath::Max(0, HoverHighlightRefCount - 1);
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::ConfigureEnemyOutlineComponent()
{
	if (!OutlineHighlight)
	{
		return;
	}
}

void AEnemyParentNative::SetScalingSnapshot(
	const int32 InLevel,
	const float InDifficultyScale,
	const FGameplayTag& InSourceTag,
	const float InRewardQualityMultiplier)
{
	CachedScaledLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(InLevel);
	CachedDifficultyScale = FMath::IsFinite(InDifficultyScale) ? FMath::Clamp(InDifficultyScale, 0.f, 1.f) : 0.f;
	CachedRewardQualityMultiplier = FMath::IsFinite(InRewardQualityMultiplier)
		? FMath::Clamp(InRewardQualityMultiplier, 0.f, 100.f)
		: 1.f;
	CachedScalingSourceTag = InSourceTag;
}
