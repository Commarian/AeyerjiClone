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
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Full);

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
		RepairInteractionSphere->SetSphereRadius(RepairInteractionRadius);
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

bool AAeyerjiSurvivalDefenseObjectiveActor::CanInteract_Implementation(AAeyerjiPlayerController* Controller)
{
	return bEnableRepairInteraction
		&& IsValid(Controller)
		&& !bObjectiveDestroyed
		&& GetObjectiveHealth() > 0.f;
}

FVector AAeyerjiSurvivalDefenseObjectiveActor::GetInteractionLocation_Implementation()
{
	return RepairInteractionSphere ? RepairInteractionSphere->GetComponentLocation() : GetActorLocation();
}

float AAeyerjiSurvivalDefenseObjectiveActor::GetInteractionRadius_Implementation()
{
	return bEnableRepairInteraction ? RepairInteractionRadius : 0.f;
}

void AAeyerjiSurvivalDefenseObjectiveActor::Interact_Implementation(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || !bEnableRepairInteraction || !IsValid(Controller))
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
	return AbilitySystem ? AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) : 0.f;
}

float AAeyerjiSurvivalDefenseObjectiveActor::GetObjectiveMaxHealth() const
{
	return AbilitySystem ? AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()) : 0.f;
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
	OnObjectiveOutOfHealth.Broadcast(this, InstigatorActor, DamageTaken);
	BP_OnObjectiveDestroyed(InstigatorActor, DamageTaken);
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::OnRep_ObjectiveDestroyed()
{
	ApplyDestroyedPresentation();
}

void AAeyerjiSurvivalDefenseObjectiveActor::InitializeObjectiveAttributes()
{
	if (!AbilitySystem)
	{
		return;
	}

	const float ClampedMaxHealth = FMath::Max(1.f, MaxHealth);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), ClampedMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), ClampedMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetArmorAttribute(), FMath::Max(0.f, Armor));
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

void AAeyerjiSurvivalDefenseObjectiveActor::AddTreeReflectFraction(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	UpgradeReflectFraction += Amount;
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::AddTreeRegenPerSecond(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f)
	{
		return;
	}

	UpgradeRegenPerSecond += Amount;
	ForceNetUpdate();
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyTreeMaxHealthUpgrade(float DeltaHP)
{
	// GAS hygiene note: direct base attribute mutation for permanent per-run upgrade.
	// For full modifier support a GameplayEffect could be used, but this keeps it simple and immediate.
	if (!HasAuthority() || DeltaHP <= 0.f || !AbilitySystem)
	{
		return;
	}

	const float CurrentMax = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	const float CurrentHP = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	const float NewMax = FMath::Max(1.f, CurrentMax + DeltaHP);
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), NewMax);
	// Grant the delta to current HP as well (clamped)
	AbilitySystem->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), FMath::Clamp(CurrentHP + DeltaHP, 0.f, NewMax));
}

void AAeyerjiSurvivalDefenseObjectiveActor::ApplyRegenTick()
{
	// Simple per-second additive regen. Called by LevelDirector's timer for now.
	// Could be moved to a periodic GE on the objective in future for full GAS integration.
	if (!HasAuthority() || UpgradeRegenPerSecond <= 0.f || !AbilitySystem)
	{
		return;
	}

	const float CurrentHP = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	const float MaxHP = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	if (CurrentHP >= MaxHP - KINDA_SMALL_NUMBER)
	{
		return;
	}

	AbilitySystem->SetNumericAttributeBase(
		UAeyerjiAttributeSet::GetHPAttribute(),
		FMath::Clamp(CurrentHP + UpgradeRegenPerSecond, 0.f, MaxHP));
}

void AAeyerjiSurvivalDefenseObjectiveActor::ResetSurvivalUpgrades()
{
	if (!HasAuthority())
	{
		return;
	}

	UpgradeReflectFraction = 0.f;
	UpgradeRegenPerSecond = 0.f;
	ForceNetUpdate();
}
