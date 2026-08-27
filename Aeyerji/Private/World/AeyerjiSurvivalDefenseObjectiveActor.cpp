// Copyright (c) 2025 Aeyerji.

#include "World/AeyerjiSurvivalDefenseObjectiveActor.h"

#include "AbilitySystemComponent.h"
#include "AeyerjiGameplayTags.h"
#include "../../AeyerjiPlayerController.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float MaxObjectiveAttributeValue = 1000000000.f;
	constexpr float MaxObjectiveInteractionRadius = 1000000.f;
}

AAeyerjiSurvivalDefenseObjectiveActor::AAeyerjiSurvivalDefenseObjectiveActor()
{
	bReplicates = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	TargetCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("TargetCollision"));
	SetRootComponent(TargetCollision);
	TargetCollision->SetBoxExtent(FVector(100.f, 100.f, 220.f));
	TargetCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetCollision->SetCollisionObjectType(ECC_Pawn);
	TargetCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	TargetCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TargetCollision->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Overlap);
	TargetCollision->SetGenerateOverlapEvents(true);

	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(TargetCollision);
	StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RepairInteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("RepairInteractionSphere"));
	RepairInteractionSphere->SetupAttachment(TargetCollision);
	RepairInteractionSphere->SetSphereRadius(RepairInteractionRadius);
	RepairInteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RepairInteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	RepairInteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	RepairInteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RepairInteractionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	RepairInteractionSphere->SetGenerateOverlapEvents(true);
	RepairInteractionSphere->SetCanEverAffectNavigation(false);

	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UAeyerjiAttributeSet>(TEXT("AttributeSet"));
}

void AAeyerjiSurvivalDefenseObjectiveActor::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
	}

	if (RepairInteractionSphere)
	{
		const float SafeInteractionRadius = FMath::IsFinite(RepairInteractionRadius)
			? FMath::Clamp(RepairInteractionRadius, 1.f, MaxObjectiveInteractionRadius)
			: 350.f;
		RepairInteractionSphere->SetSphereRadius(SafeInteractionRadius);
		RepairInteractionSphere->SetCollisionEnabled(bEnableRepairInteraction ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}

	if (HasAuthority())
	{
		InitializeObjectiveAttributes();
		if (AttributeSet)
		{
			AttributeSet->OnOutOfHealth.RemoveDynamic(this, &AAeyerjiSurvivalDefenseObjectiveActor::HandleObjectiveOutOfHealth);
			AttributeSet->OnOutOfHealth.AddDynamic(this, &AAeyerjiSurvivalDefenseObjectiveActor::HandleObjectiveOutOfHealth);
		}
	}
}

void AAeyerjiSurvivalDefenseObjectiveActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AttributeSet)
	{
		AttributeSet->OnOutOfHealth.RemoveDynamic(this, &AAeyerjiSurvivalDefenseObjectiveActor::HandleObjectiveOutOfHealth);
	}
	Super::EndPlay(EndPlayReason);
}

bool AAeyerjiSurvivalDefenseObjectiveActor::CanInteract_Implementation(AAeyerjiPlayerController* Controller)
{
	return bEnableRepairInteraction
		&& IsValid(Controller)
		&& Controller->GetWorld() == GetWorld()
		&& !bObjectiveDestroyed
		&& FMath::IsFinite(GetObjectiveHealth())
		&& GetObjectiveHealth() > 0.f;
}

FVector AAeyerjiSurvivalDefenseObjectiveActor::GetInteractionLocation_Implementation()
{
	return RepairInteractionSphere ? RepairInteractionSphere->GetComponentLocation() : GetActorLocation();
}

float AAeyerjiSurvivalDefenseObjectiveActor::GetInteractionRadius_Implementation()
{
	return bEnableRepairInteraction && FMath::IsFinite(RepairInteractionRadius)
		? FMath::Clamp(RepairInteractionRadius, 1.f, MaxObjectiveInteractionRadius)
		: 0.f;
}

void AAeyerjiSurvivalDefenseObjectiveActor::Interact_Implementation(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority()
		|| !bEnableRepairInteraction
		|| !IsValid(Controller)
		|| Controller->GetWorld() != GetWorld())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
	{
		It->OpenSurvivalDefenseObjectiveRepairMenu(Controller, this);
		return;
	}

	Controller->Client_ShowMissionMessageKey(FName(TEXT("RepairUnavailable")), 2.f);
}

void AAeyerjiSurvivalDefenseObjectiveActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiSurvivalDefenseObjectiveActor, bObjectiveDestroyed);
	DOREPLIFETIME(AAeyerjiSurvivalDefenseObjectiveActor, UpgradeReflectFraction);
	DOREPLIFETIME(AAeyerjiSurvivalDefenseObjectiveActor, UpgradeRegenPerSecond);
}

void AAeyerjiSurvivalDefenseObjectiveActor::OnRep_UpgradeReflectFraction()
{
	// Clients can react here if needed (e.g. VFX update). Currently passive.
}

void AAeyerjiSurvivalDefenseObjectiveActor::OnRep_UpgradeRegenPerSecond()
{
	// Clients can react here if needed.
}

UAbilitySystemComponent* AAeyerjiSurvivalDefenseObjectiveActor::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

FGenericTeamId AAeyerjiSurvivalDefenseObjectiveActor::GetGenericTeamId() const
{
	return FGenericTeamId(TeamId);
}

void AAeyerjiSurvivalDefenseObjectiveActor::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	TeamId = NewTeamID.GetId();
}

float AAeyerjiSurvivalDefenseObjectiveActor::GetObjectiveHealth() const
{
	const float Health = AbilitySystem ? AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) : 0.f;
	return FMath::IsFinite(Health) ? FMath::Max(0.f, Health) : 0.f;
}

float AAeyerjiSurvivalDefenseObjectiveActor::GetObjectiveMaxHealth() const
{
	const float MaxObjectiveHealth = AbilitySystem ? AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()) : 0.f;
	return FMath::IsFinite(MaxObjectiveHealth) ? FMath::Max(0.f, MaxObjectiveHealth) : 0.f;
}

void AAeyerjiSurvivalDefenseObjectiveActor::HandleObjectiveOutOfHealth(AActor* VictimActor, AActor* InstigatorActor, const float DamageTaken)
{
	if (!HasAuthority() || bObjectiveDestroyed || VictimActor != this)
	{
		return;
	}

	bObjectiveDestroyed = true;
	if (!Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
	{
		Tags.Add(AeyerjiTags::State_Dead.GetTag().GetTagName());
	}

	ApplyDestroyedPresentation();
	const float SafeDamageTaken = FMath::IsFinite(DamageTaken) ? FMath::Max(0.f, DamageTaken) : 0.f;
	OnObjectiveOutOfHealth.Broadcast(this, InstigatorActor, SafeDamageTaken);
	BP_OnObjectiveDestroyed(InstigatorActor, SafeDamageTaken);
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::OnRep_ObjectiveDestroyed()
{
	if (bObjectiveDestroyed)
	{
		ApplyDestroyedPresentation();
	}
	else
	{
		ApplyActivePresentation();
	}
}

void AAeyerjiSurvivalDefenseObjectiveActor::InitializeObjectiveAttributes()
{
	if (!AbilitySystem)
	{
		return;
	}

	const float ClampedMaxHealth = FMath::IsFinite(MaxHealth)
		? FMath::Clamp(MaxHealth, 1.f, MaxObjectiveAttributeValue)
		: 1000.f;
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), ClampedMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), ClampedMaxHealth);
	const float SafeArmor = FMath::IsFinite(Armor)
		? FMath::Clamp(Armor, 0.f, MaxObjectiveAttributeValue)
		: 0.f;
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetArmorAttribute(), SafeArmor);
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyDestroyedPresentation()
{
	if (!bObjectiveDestroyed)
	{
		return;
	}

	if (TargetCollision && bDisableCollisionWhenDestroyed)
	{
		TargetCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetCollision->SetGenerateOverlapEvents(false);
	}

	if (RepairInteractionSphere)
	{
		RepairInteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RepairInteractionSphere->SetGenerateOverlapEvents(false);
	}

	if (StaticMesh && bHideWhenDestroyed)
	{
		StaticMesh->SetHiddenInGame(true);
	}
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyActivePresentation()
{
	if (bObjectiveDestroyed)
	{
		return;
	}

	if (TargetCollision)
	{
		TargetCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		TargetCollision->SetGenerateOverlapEvents(true);
	}
	if (RepairInteractionSphere)
	{
		RepairInteractionSphere->SetCollisionEnabled(
			bEnableRepairInteraction ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		RepairInteractionSphere->SetGenerateOverlapEvents(bEnableRepairInteraction);
	}
	if (StaticMesh && bHideWhenDestroyed)
	{
		StaticMesh->SetHiddenInGame(false);
	}
	BP_OnObjectiveReset();
}

void AAeyerjiSurvivalDefenseObjectiveActor::AddTreeReflectFraction(float Amount)
{
	if (!HasAuthority() || !FMath::IsFinite(Amount) || Amount <= 0.f)
	{
		return;
	}

	UpgradeReflectFraction = FMath::Clamp(UpgradeReflectFraction + Amount, 0.f, 100.f);
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::AddTreeRegenPerSecond(float Amount)
{
	if (!HasAuthority() || !FMath::IsFinite(Amount) || Amount <= 0.f)
	{
		return;
	}

	UpgradeRegenPerSecond = FMath::Clamp(
		UpgradeRegenPerSecond + Amount,
		0.f,
		MaxObjectiveAttributeValue);
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyTreeMaxHealthUpgrade(float DeltaHP)
{
	// GAS hygiene note: direct base attribute mutation for permanent per-run upgrade.
	// For full modifier support a GameplayEffect could be used, but this keeps it simple and immediate.
	if (!HasAuthority() || !FMath::IsFinite(DeltaHP) || DeltaHP <= 0.f || !AbilitySystem)
	{
		return;
	}

	const float CurrentMax = GetObjectiveMaxHealth();
	const float CurrentHP = GetObjectiveHealth();
	const double NewMaxValue = FMath::Clamp(
		static_cast<double>(CurrentMax) + FMath::Clamp(DeltaHP, 0.f, MaxObjectiveAttributeValue),
		1.0,
		static_cast<double>(MaxObjectiveAttributeValue));
	const float NewMax = static_cast<float>(NewMaxValue);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), NewMax);
	// Grant the delta to current HP as well (clamped)
	AbilitySystem->SetNumericAttributeBase(
		UAeyerjiAttributeSet::GetHPAttribute(),
		static_cast<float>(FMath::Clamp(
			static_cast<double>(CurrentHP) + FMath::Clamp(DeltaHP, 0.f, MaxObjectiveAttributeValue),
			0.0,
			NewMaxValue)));
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyRegenTick()
{
	// Simple per-second additive regen. Called by LevelDirector's timer for now.
	// Could be moved to a periodic GE on the objective in future for full GAS integration.
	if (!HasAuthority()
		|| !FMath::IsFinite(UpgradeRegenPerSecond)
		|| UpgradeRegenPerSecond <= 0.f
		|| !AbilitySystem)
	{
		return;
	}

	const float CurrentHP = GetObjectiveHealth();
	const float MaxHP = GetObjectiveMaxHealth();
	if (MaxHP <= UE_SMALL_NUMBER)
	{
		return;
	}
	if (CurrentHP >= MaxHP - KINDA_SMALL_NUMBER)
	{
		return;
	}

	AbilitySystem->SetNumericAttributeBase(
		UAeyerjiAttributeSet::GetHPAttribute(),
		static_cast<float>(FMath::Clamp(
			static_cast<double>(CurrentHP) + UpgradeRegenPerSecond,
			0.0,
			static_cast<double>(MaxHP))));
}

void AAeyerjiSurvivalDefenseObjectiveActor::ResetSurvivalUpgrades()
{
	if (!HasAuthority())
	{
		return;
	}

	UpgradeReflectFraction = 0.f;
	UpgradeRegenPerSecond = 0.f;
	if (AbilitySystem)
	{
		const float AuthoredMaxHealth = FMath::IsFinite(MaxHealth)
			? FMath::Clamp(MaxHealth, 1.f, MaxObjectiveAttributeValue)
			: 1000.f;
		const float CurrentHP = GetObjectiveHealth();
		AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), AuthoredMaxHealth);
		AbilitySystem->SetNumericAttributeBase(
			UAeyerjiAttributeSet::GetHPAttribute(),
			FMath::Clamp(CurrentHP, 0.f, AuthoredMaxHealth));
	}
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::ResetObjectiveForNewRun()
{
	if (!HasAuthority())
	{
		return;
	}

	ResetSurvivalUpgrades();
	bObjectiveDestroyed = false;
	Tags.Remove(AeyerjiTags::State_Dead.GetTag().GetTagName());
	InitializeObjectiveAttributes();
	ApplyActivePresentation();
	ForceNetUpdate();
}
