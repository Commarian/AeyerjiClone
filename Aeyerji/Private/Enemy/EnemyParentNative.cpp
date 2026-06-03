// EnemyParentNative.cpp
#include "Enemy/EnemyParentNative.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemGlobals.h"
#include "Logging/AeyerjiLog.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiRewardAttributeSet.h"
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
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Progression/AeyerjiRewardConfigComponent.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	const FGameplayTag& DeadStateTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
		return Tag;
	}

	constexpr double AllyAlertRepeatCooldownSeconds = 0.5;
}

AEnemyParentNative::AEnemyParentNative()
{
	PrimaryActorTick.bCanEverTick = false;          // Creeps usually tick via AI only
	/* Network */
	bReplicates = true;

	DefaultTeamTag = FGameplayTag::RequestGameplayTag(TEXT("Team.Enemy"));

	OutlineHighlight = CreateDefaultSubobject<UOutlineHighlightComponent>(TEXT("OutlineHighlight"));
	if (OutlineHighlight)
	{
		OutlineHighlight->bAffectAllPrimitivesIfNoExplicitTargets = false;
	}

	ClickTargetCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ClickTargetCapsule"));
	if (ClickTargetCapsule)
	{
		ClickTargetCapsule->bEditableWhenInherited = true;
		ClickTargetCapsule->SetupAttachment(GetCapsuleComponent());
		ClickTargetCapsule->SetUsingAbsoluteScale(true);
		ClickTargetCapsule->SetRelativeScale3D(FVector::OneVector);
		ClickTargetCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ClickTargetCapsule->SetCollisionObjectType(ECC_WorldDynamic);
		ClickTargetCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		ClickTargetCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block);
		ClickTargetCapsule->SetGenerateOverlapEvents(false);
		ClickTargetCapsule->SetCanEverAffectNavigation(false);
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
		AbilitySystemAeyerji->SetReplicationMode(EGameplayEffectReplicationMode::Full);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // interact traces
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

	if (HighlightChannel == 20 && HighlightStencilValue_DEPRECATED != 0)
	{
		HighlightChannel = FMath::Clamp(HighlightStencilValue_DEPRECATED, 0, 255);
		HighlightStencilValue_DEPRECATED = 0;
	}
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
	RefreshClickTargetCapsule();
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
	RefreshClickTargetCapsule();

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
	InitAbilityActorInfo();
	GiveStartupAbilitiesAndEffects();
	ApplyDefaultTeamTags();
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

void AEnemyParentNative::OnDeath_Implementation()
{
	if (HasAuthority())
	{
		TrySpawnEnemyDeathRewards(nullptr);
	}

	// Broadcast immediately so encounter logic can react before delayed cleanup runs.
	OnEnemyDied.Broadcast(this);
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

	if (!RewardInstigator)
	{
		RewardInstigator = UGameplayStatics::GetPlayerPawn(this, 0);
	}

	FLootContext RuntimeContext = DeathLootContext;
	if (!RuntimeContext.PlayerActor.IsValid())
	{
		RuntimeContext.PlayerActor = RewardInstigator;
	}
	RuntimeContext.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(CachedScaledLevel > 0 ? CachedScaledLevel : RuntimeContext.EnemyLevel);
	if (RuntimeContext.PlayerLevel <= 0)
	{
		RuntimeContext.PlayerLevel = RuntimeContext.EnemyLevel;
	}
	else
	{
		RuntimeContext.PlayerLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(RuntimeContext.PlayerLevel);
	}
	RuntimeContext.DifficultyScale = CachedDifficultyScale > 0.f ? CachedDifficultyScale : RuntimeContext.DifficultyScale;
	if (!RuntimeContext.SourceTag.IsValid())
	{
		RuntimeContext.SourceTag = CachedScalingSourceTag.IsValid()
			? CachedScalingSourceTag
			: AeyerjiTags::Loot_Source_NormalEnemy;
	}

	const FVector SpawnLocation = GetActorLocation();
	const FRotator SpawnRotation = GetActorRotation();
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
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	if (CrowdMinLOD > 0)
	{
		MeshComp->OverrideMinLOD(CrowdMinLOD);
	}

	if (CrowdForcedLOD > 0)
	{
		MeshComp->SetForcedLOD(CrowdForcedLOD);
	}

	if (CrowdMaxDrawDistance > 0.f)
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
}

void AEnemyParentNative::OnRep_ActiveTeamTag()
{
	ApplyActiveTeamTagToASC(LastAppliedTeamTag);
	LastAppliedTeamTag = ActiveTeamTag;
}

void AEnemyParentNative::SetActiveTeamTag(const FGameplayTag& NewTag)
{
	if (!NewTag.IsValid())
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
	if (ArchetypeComponent)
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
	if (!HasAuthority() || AllyAlertRadius <= 0.f || !IsValid(Target) || IsActorBeingDestroyed())
	{
		return;
	}

	const FGameplayTag DeadTag = DeadStateTag();
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
		FCollisionShape::MakeSphere(AllyAlertRadius),
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

	const FGameplayTag DeadTag = DeadStateTag();
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
		InOutValue *= Mults->HealthMultiplier;
		return true;
	}

	if (StrippedName == TEXT("AttackDamage"))
	{
		InOutValue *= Mults->DamageMultiplier;
		return true;
	}

	if (StrippedName == TEXT("RunSpeed") || StrippedName == TEXT("WalkSpeed"))
	{
		InOutValue *= Mults->MoveSpeedMultiplier;
		return true;
	}

	if (StrippedName == TEXT("AttackSpeed"))
	{
		InOutValue *= Mults->AttackRateMultiplier;
		return true;
	}

	if (StrippedName == TEXT("AttackCooldown"))
	{
		const float SafeRate = FMath::Max(0.01f, Mults->AttackRateMultiplier);
		InOutValue /= SafeRate;
		return true;
	}

	return false;
}

// Grants abilities if they are not already present on the ASC.
void AEnemyParentNative::GrantAbilityList(const TArray<TSubclassOf<UGameplayAbility>>& Abilities, int32 AbilityLevel)
{
	if (!AbilitySystemAeyerji || Abilities.IsEmpty())
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
	if (!AbilitySystemAeyerji || Effects.IsEmpty())
	{
		return;
	}

	const float ClampedLevel = FMath::Max(0.01f, EffectLevel);
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

void AEnemyParentNative::RefreshClickTargetCapsule()
{
	if (!ClickTargetCapsule)
	{
		return;
	}

	UCapsuleComponent* RootCapsule = GetCapsuleComponent();
	if (!RootCapsule)
	{
		return;
	}

	RootCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
	ClickTargetCapsule->SetUsingAbsoluteScale(true);
	ClickTargetCapsule->SetRelativeScale3D(FVector::OneVector);
	ClickTargetCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block);

	if (bAutoSizeClickTargetCapsule)
	{
		const float RadiusScale = FMath::Max(1.f, ClickTargetRadiusScale);
		const float HeightScale = FMath::Max(1.f, ClickTargetHalfHeightScale);

		ClickTargetCapsule->SetRelativeLocation(FVector::ZeroVector);
		ClickTargetCapsule->SetCapsuleSize(
			RootCapsule->GetScaledCapsuleRadius() * RadiusScale,
			RootCapsule->GetScaledCapsuleHalfHeight() * HeightScale);
		return;
	}

	// Manual mode leaves the inherited component's authored transform and capsule size untouched.
}

void AEnemyParentNative::ConfigureEnemyOutlineComponent()
{
	if (!OutlineHighlight)
	{
		return;
	}
}

void AEnemyParentNative::SetScalingSnapshot(int32 InLevel, float InDifficultyScale, const FGameplayTag& InSourceTag)
{
	CachedScaledLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(InLevel);
	CachedDifficultyScale = FMath::Clamp(InDifficultyScale, 0.f, 1.f);
	CachedScalingSourceTag = InSourceTag;
}
