#include "Enemy/AeyerjiBombardment.h"

#include "Abilities/AbilityTeamUtils.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "Testing/AeyerjiCombatBalanceTestHarness.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AAeyerjiBombardment::AAeyerjiBombardment()
{
	bReplicates = true;
	// At most two warnings exist; all players must receive their telegraphs independently of the caster's relevance.
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	BoundaryRing = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoundaryRing"));
	SetRootComponent(BoundaryRing);
	CountdownRing = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("CountdownRing"));
	CountdownRing->SetupAttachment(BoundaryRing);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (UInstancedStaticMeshComponent* Ring : { BoundaryRing.Get(), CountdownRing.Get() })
	{
		Ring->SetStaticMesh(Cube.Object);
		Ring->SetMobility(EComponentMobility::Movable);
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetGenerateOverlapEvents(false);
		Ring->SetCanEverAffectNavigation(false);
		Ring->SetCastShadow(false);
	}
}

bool AAeyerjiBombardment::CanStartBombardment(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	int32 Active = 0;
	for (TActorIterator<AAeyerjiBombardment> It(World); It; ++It)
	{
		if (IsValid(*It) && It->IsPending() && ++Active >= 2)
		{
			return false;
		}
	}
	return true;
}

bool AAeyerjiBombardment::InitializeBombardment(AActor* Source, AActor* AimTarget,
	const FVector& Center, float Radius, float WindupSeconds, const FGameplayEffectSpecHandle& DamageSpec)
{
	if (!HasAuthority() || State.Phase != EAeyerjiBombardmentPhase::Inactive
		|| !IsValid(Source) || Source->GetWorld() != GetWorld() || !DamageSpec.IsValid()
		|| Center.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 50.f || Radius > 600.f
		|| !FMath::IsFinite(WindupSeconds) || WindupSeconds < 0.75f || WindupSeconds > 2.f
		|| !CanStartBombardment(GetWorld()))
	{
		return false;
	}
	SourceActor = Source;
	AimActor = AimTarget;
	if (!IsSourceReady())
	{
		return false;
	}
	SetOwner(Source);
	Source->OnDestroyed.AddDynamic(this, &ThisClass::HandleSourceDestroyed);
	PendingDamageSpec = DamageSpec;
	State.Center = Center;
	State.Radius = Radius;
	State.WindupSeconds = WindupSeconds;
	State.ImpactServerTime = GetWorld()->GetTimeSeconds() + WindupSeconds;
	State.Phase = EAeyerjiBombardmentPhase::Warning;
	SetActorLocation(Center);
	AAeyerjiCombatBalanceTestHarness::RecordBombardmentEvent(Source, AimTarget, GetUniqueID(),
		EAeyerjiBombardmentTelemetryEvent::Started, Center, Radius);
	GetWorldTimerManager().SetTimer(ImpactTimer, this, &ThisClass::ResolveImpact, WindupSeconds, false);
	ForceNetUpdate();
	RefreshPresentation();
	return true;
}

bool AAeyerjiBombardment::IsInsideBlast(const FVector& Center, const FVector& Point, float Radius, float HalfHeight)
{
	return !Center.ContainsNaN() && !Point.ContainsNaN() && FMath::IsFinite(Radius)
		&& FMath::IsFinite(HalfHeight) && Radius > 0.f && HalfHeight > 0.f
		&& FVector::DistSquared2D(Center, Point) <= FMath::Square(static_cast<double>(Radius))
		&& FMath::Abs(Point.Z - Center.Z) <= HalfHeight;
}

bool AAeyerjiBombardment::IsDamageTarget(const AActor* Source, AActor* Candidate)
{
	if (!IsValid(Source) || !IsValid(Candidate) || Source == Candidate
		|| Source->GetWorld() != Candidate->GetWorld() || !Candidate->CanBeDamaged()
		|| Candidate->IsActorBeingDestroyed() || Candidate->IsHidden()
		|| AbilityTeamUtils::ResolveTeamId(Source) == FGenericTeamId::NoTeam
		|| AbilityTeamUtils::ResolveTeamId(Candidate) == FGenericTeamId::NoTeam
		|| AbilityTeamUtils::AreOnSameTeam(Source, Candidate))
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
	return ASC && ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPAttribute())
		&& !ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead)
		&& ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) > 0.f;
}

bool AAeyerjiBombardment::IsSourceReady() const
{
	const AActor* Source = SourceActor.Get();
	if (!IsValid(Source) || Source->IsActorBeingDestroyed() || Source->IsHidden())
	{
		return false;
	}
	if (const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(Source))
	{
		if (!Enemy->IsEncounterCombatActive())
		{
			return false;
		}
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source);
	return ASC && ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPAttribute())
		&& ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) > 0.f
		&& !ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead)
		&& !ASC->HasMatchingGameplayTag(AeyerjiTags::State_CrowdControl_Stunned)
		&& !ASC->HasMatchingGameplayTag(AeyerjiTags::State_CrowdControl_Staggered);
}

void AAeyerjiBombardment::ResolveImpact()
{
	if (!HasAuthority() || !IsPending() || bResolvingImpact)
	{
		return;
	}
	if (!IsSourceReady() || !PendingDamageSpec.IsValid())
	{
		CancelBombardment();
		return;
	}
	// Synchronous GAS callbacks can cancel the source ability. The impact is atomic once started.
	TGuardValue<bool> ResolveGuard(bResolvingImpact, true);
	const float Height = FMath::IsFinite(BlastHalfHeight) ? FMath::Clamp(BlastHalfHeight, 50.f, 500.f) : 180.f;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BombardmentImpact), false, this);
	Query.AddIgnoredActor(SourceActor.Get());
	GetWorld()->OverlapMultiByObjectType(Overlaps, State.Center, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(FMath::Sqrt(FMath::Square(State.Radius) + FMath::Square(Height))), Query);
	TSet<AActor*> Seen;
	int32 Candidates = 0;
	int32 Applications = 0;
	UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SourceActor.Get());
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (Seen.Contains(Target) || !IsDamageTarget(SourceActor.Get(), Target)
			|| !IsInsideBlast(State.Center, Target->GetActorLocation(), State.Radius, Height))
		{
			continue;
		}
		Seen.Add(Target);
		FCollisionQueryParams Occlusion(SCENE_QUERY_STAT(BombardmentOcclusion), true, this);
		Occlusion.AddIgnoredActor(SourceActor.Get());
		Occlusion.AddIgnoredActor(Target);
		if (GetWorld()->LineTraceTestByChannel(State.Center + FVector(0, 0, 30),
			Target->GetActorLocation(), ECC_Visibility, Occlusion))
		{
			continue;
		}
		++Candidates;
		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		if (SourceASC && TargetASC)
		{
			FGameplayEffectSpec PerTargetSpec(*PendingDamageSpec.Data.Get());
			PerTargetSpec.SetContext(PendingDamageSpec.Data->GetContext().Duplicate());
			SourceASC->ApplyGameplayEffectSpecToTarget(PerTargetSpec, TargetASC);
			++Applications;
		}
	}
	FinishBombardment(false, Candidates, Applications);
}

void AAeyerjiBombardment::CancelBombardment()
{
	if (HasAuthority() && IsPending() && !bResolvingImpact)
	{
		FinishBombardment(true, INDEX_NONE, INDEX_NONE);
	}
}

void AAeyerjiBombardment::FinishBombardment(bool bCancelled, int32 Candidates, int32 Applications)
{
	GetWorldTimerManager().ClearTimer(ImpactTimer);
	if (AActor* Source = SourceActor.Get())
	{
		Source->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleSourceDestroyed);
	}
	State.Phase = bCancelled ? EAeyerjiBombardmentPhase::Cancelled : EAeyerjiBombardmentPhase::Impacted;
	AAeyerjiCombatBalanceTestHarness::RecordBombardmentEvent(SourceActor.Get(), AimActor.Get(), GetUniqueID(),
		bCancelled ? EAeyerjiBombardmentTelemetryEvent::Cancelled : EAeyerjiBombardmentTelemetryEvent::Impacted,
		State.Center, State.Radius, Candidates, Applications);
	PendingDamageSpec = FGameplayEffectSpecHandle();
	ForceNetUpdate();
	RefreshPresentation();
	OnResolved.Broadcast(bCancelled);
	OnResolved.Clear();
	// Retain the terminal snapshot briefly so clients can observe impact/cancellation before actor destruction.
	SetLifeSpan(0.5f);
}

void AAeyerjiBombardment::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void AAeyerjiBombardment::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelBombardment();
	GetWorldTimerManager().ClearTimer(ImpactTimer);
	Super::EndPlay(Reason);
}

void AAeyerjiBombardment::OnRep_State()
{
	RefreshPresentation();
}

void AAeyerjiBombardment::HandleSourceDestroyed(AActor* DestroyedSource)
{
	// Record cancellation while the source still has its identity, before destruction invalidates weak references.
	CancelBombardment();
}

void AAeyerjiBombardment::RefreshPresentation()
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	SetActorLocation(State.Center);
	if (State.Phase == LastPresentedPhase)
	{
		return;
	}
	LastPresentedPhase = State.Phase;
	BoundaryRing->ClearInstances();
	CountdownRing->ClearInstances();
	if (bShowNativeWarning && (IsPending() || State.Phase == EAeyerjiBombardmentPhase::Impacted))
	{
		constexpr int32 Segments = 40;
		for (UInstancedStaticMeshComponent* Ring : { BoundaryRing.Get(), CountdownRing.Get() })
		{
			if (WarningMaterial)
			{
				Ring->SetMaterial(0, WarningMaterial);
			}
			for (int32 Index = 0; Index < Segments; ++Index)
			{
				const float Angle = 2.f * PI * Index / Segments;
				const FVector Position(FMath::Cos(Angle) * State.Radius, FMath::Sin(Angle) * State.Radius, 6.f);
				Ring->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90.f, 0), Position,
					FVector(2.f * PI * State.Radius / Segments / 100.f, 0.045f, 0.025f)));
			}
		}
	}
	BoundaryRing->SetVisibility(bShowNativeWarning && State.Phase != EAeyerjiBombardmentPhase::Cancelled, false);
	CountdownRing->SetVisibility(bShowNativeWarning && IsPending(), false);
	BP_OnBombardmentStateChanged(State);
}

void AAeyerjiBombardment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && IsPending() && !IsSourceReady())
	{
		CancelBombardment();
	}
	if (GetNetMode() != NM_DedicatedServer && IsPending())
	{
		const AGameStateBase* GameState = GetWorld()->GetGameState();
		const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		const float Fraction = FMath::Clamp(static_cast<float>((State.ImpactServerTime - Now) / State.WindupSeconds), 0.f, 1.f);
		CountdownRing->SetRelativeScale3D(FVector(FMath::Max(0.01f, Fraction), FMath::Max(0.01f, Fraction), 1.f));
		BP_UpdateWarning(Fraction);
	}
}

void AAeyerjiBombardment::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAeyerjiBombardment, State);
}
