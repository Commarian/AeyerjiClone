// EliteBurningTrailPatch.cpp

#include "Abilities/EliteBurningTrail/EliteBurningTrailPatch.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "AIController.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Components/SphereComponent.h"
#include "GameplayEffect.h"
#include "NiagaraComponent.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogEliteBurningTrail, Log, All);

AEliteBurningTrailPatch::AEliteBurningTrailPatch()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	DamageArea = CreateDefaultSubobject<USphereComponent>(TEXT("DamageArea"));
	DamageArea->SetupAttachment(RootComponent);
	DamageArea->SetMobility(EComponentMobility::Movable);
	DamageArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DamageArea->SetCollisionObjectType(ECC_WorldDynamic);
	DamageArea->SetCollisionResponseToAllChannels(ECR_Ignore);
	DamageArea->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DamageArea->SetGenerateOverlapEvents(true);
	DamageArea->OnComponentBeginOverlap.AddDynamic(this, &AEliteBurningTrailPatch::HandleDamageAreaBeginOverlap);

	VisualFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("VisualFX"));
	VisualFX->SetupAttachment(RootComponent);
	VisualFX->SetMobility(EComponentMobility::Movable);
	VisualFX->SetUsingAbsoluteLocation(false);
	VisualFX->SetUsingAbsoluteRotation(false);
	VisualFX->SetUsingAbsoluteScale(false);
	VisualFX->SetAutoActivate(false);
	VisualFX->SetAutoDestroy(false);
	VisualFX->SetIsReplicated(true);
}

void AEliteBurningTrailPatch::BeginPlay()
{
	Super::BeginPlay();

	SetReplicateMovement(true);

	RefreshCollisionRadius();

	if (HasAuthority())
	{
		SnapPatchToGround();
	}

	TryActivateVFX();

	if (LifetimeSeconds > 0.f)
	{
		SetLifeSpan(LifetimeSeconds);
	}
}

bool AEliteBurningTrailPatch::ApplyPatchDamage_Implementation(AActor* Target, float InDamagePerSecond, UGameplayAbility* InSourceAbility)
{
	if (!Target || InDamagePerSecond <= 0.f)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!TargetASC)
	{
		return false;
	}

	UAbilitySystemComponent* SourceASC = InstigatorASC.Get();
	UAbilitySystemComponent* SpecASC = SourceASC ? SourceASC : TargetASC;

	TSubclassOf<UGameplayEffect> DamageClass = DotEffectClass;
	if (!DamageClass)
	{
		return false;
	}

	FGameplayEffectContextHandle ContextHandle = SpecASC->MakeEffectContext();
	ContextHandle.AddSourceObject(this);
	if (InSourceAbility)
	{
		ContextHandle.AddSourceObject(InSourceAbility);
	}

	FGameplayEffectSpecHandle SpecHandle = SpecASC->MakeOutgoingSpec(DamageClass, 1.f, ContextHandle);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return false;
	}

	if (DamageSetByCallerTag.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(DamageSetByCallerTag, InDamagePerSecond);
	}

	if (DamageTypeTag.IsValid())
	{
		SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
	}

	TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	return true;
}

void AEliteBurningTrailPatch::InitializePatch(const FAGEliteBurningTrailTuning& InTuning,
                                              UAbilitySystemComponent* InInstigatorASC,
                                              const FGameplayTag& InDamageSetByCallerTag,
                                              const FGameplayTag& InDamageTypeTag,
                                              TSubclassOf<UGameplayEffect> InDotEffectClass,
                                              UGameplayAbility* InSourceAbility)
{
	LifetimeSeconds = InTuning.PatchLifetime;
	DamagePerSecond = InTuning.DamagePerSecond;
	PatchRadius = InTuning.PatchRadius;
	DamageSetByCallerTag = InDamageSetByCallerTag;
	DamageTypeTag = InDamageTypeTag;
	DotEffectClass = InDotEffectClass;
	InstigatorASC = InInstigatorASC;
	SourceAbility = InSourceAbility;

	RefreshCollisionRadius();

	if (HasAuthority())
	{
		SnapPatchToGround();
	}

	TryActivateVFX();

	if (LifetimeSeconds > 0.f)
	{
		SetLifeSpan(LifetimeSeconds);
	}
}

void AEliteBurningTrailPatch::RefreshCollisionRadius()
{
	if (DamageArea)
	{
		const float Radius = FMath::Max(1.f, PatchRadius);
		DamageArea->SetSphereRadius(Radius);
	}
}

void AEliteBurningTrailPatch::SnapPatchToGround()
{
	if (USceneComponent* RootComp = GetRootComponent())
	{
		if (RootComp->Mobility != EComponentMobility::Movable)
		{
			RootComp->SetMobility(EComponentMobility::Movable);
		}
	}

	FHitResult Hit;
	if (!GetGroundSnapHitResult(Hit))
	{
		return;
	}

	if (!Hit.GetComponent())
	{
		return;
	}

	FVector TargetLocation = Hit.ImpactPoint;
	TargetLocation.Z = Hit.ImpactPoint.Z + GetGroundOffsetZ(Hit);

	if (HasAuthority())
	{
		SetActorLocation(TargetLocation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
		ForceNetUpdate();
	}
}

float AEliteBurningTrailPatch::GetGroundOffsetZ_Implementation(const FHitResult& GroundHit) const
{
	return GroundOffsetZ;
}

bool AEliteBurningTrailPatch::GetGroundSnapHitResult(FHitResult& OutGroundHit) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TraceStart = GetActorLocation() + FVector::UpVector * 50.f;
	const FVector TraceEnd = TraceStart - FVector::UpVector * FMath::Max(0.f, GroundTraceDistance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EliteBurningTrailPatchGroundTrace), /*bTraceComplex=*/false, this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.AddIgnoredActor(this);

	const FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::InitType::AllStaticObjects);
	bool bHit = World->LineTraceSingleByObjectType(OutGroundHit, TraceStart, TraceEnd, ObjectQueryParams, QueryParams);
	if (!bHit)
	{
		bHit = World->LineTraceSingleByChannel(OutGroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	}

	return bHit;
}

void AEliteBurningTrailPatch::TryActivateVFX()
{
	if (bVFXActivated || !VisualFX)
	{
		return;
	}

	if (HasAuthority())
	{
		Multicast_ActivateVFX(GetActorLocation());
	}
}

void AEliteBurningTrailPatch::Multicast_ActivateVFX_Implementation(FVector ReplicatedLocation)
{
	if (bVFXActivated || !VisualFX)
	{
		return;
	}

	bVFXActivated = true;

	if (!HasAuthority())
	{
		SetActorLocation(ReplicatedLocation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
	}

	VisualFX->DeactivateImmediate();
	VisualFX->Activate(/*bReset=*/true);
}

void AEliteBurningTrailPatch::HandleDamageAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent,
                                                           AActor* OtherActor,
                                                           UPrimitiveComponent* OtherComp,
                                                           int32 OtherBodyIndex,
                                                           bool bFromSweep,
                                                           const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this || OtherActor == GetOwner())
	{
		return;
	}

	if (!HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = InstigatorASC.Get();
	if (!SourceASC)
	{
		return;
	}

	AActor* SourceActor = SourceASC->GetAvatarActor();
	if (!SourceActor)
	{
		SourceActor = SourceASC->GetOwnerActor();
	}

	const FGenericTeamId SourceTeam = AbilityTeamUtils::ResolveTeamId(SourceActor);
	const FGenericTeamId TargetTeam = AbilityTeamUtils::ResolveTeamId(OtherActor);
	const bool bBothTeamsValid = SourceTeam != FGenericTeamId::NoTeam && TargetTeam != FGenericTeamId::NoTeam;
	if (bBothTeamsValid && SourceTeam == TargetTeam)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor);
	if (TargetASC && TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
	{
		return;
	}

	// Non-ASC pawns rely on BP to handle damage; stop AI movement to avoid pathfollowing crashes during destruction.
	if (!TargetASC)
	{
		if (APawn* TargetPawn = Cast<APawn>(OtherActor))
		{
			if (AAIController* TargetAI = Cast<AAIController>(TargetPawn->GetController()))
			{
				TargetAI->StopMovement();
			}
		}
	}

	ApplyPatchDamage(OtherActor, DamagePerSecond, SourceAbility.Get());
}
