#include "Components/AeyerjiNavSafetyComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"

UAeyerjiNavSafetyComponent::UAeyerjiNavSafetyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.TickInterval = CheckInterval;
}

void UAeyerjiNavSafetyComponent::BeginPlay()
{
	Super::BeginPlay();

	PrimaryComponentTick.TickInterval = FMath::Max(0.01f, CheckInterval);
	EnsureOwnerOnSafeNav(/*bImmediateRecover=*/false);
}

void UAeyerjiNavSafetyComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bEnableRuntimeRecovery || !IsOwnerRecoverable())
	{
		OffNavStartTime = -1.0;
		return;
	}

	APawn* Pawn = GetOwnerPawn();
	if (!Pawn)
	{
		return;
	}

	FAeyerjiNavSafetyResult CurrentResult;
	if (UAeyerjiNavSafetyLibrary::IsPawnOnUsableNav(Pawn, ResolveParams, CurrentResult))
	{
		UpdateLastSafeLocation(CurrentResult);
		OffNavStartTime = -1.0;
		return;
	}

	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (OffNavStartTime < 0.0)
	{
		OffNavStartTime = Now;
	}

	const bool bCriticalZ = IsOwnerBelowCriticalZ(Pawn);
	const bool bGraceExpired = OffNavGraceSeconds <= 0.f || (Now - OffNavStartTime) >= OffNavGraceSeconds;
	if (bCriticalZ || bGraceExpired)
	{
		RecoverOwnerToSafeNav();
	}
}

bool UAeyerjiNavSafetyComponent::EnsureOwnerOnSafeNav(const bool bImmediateRecover)
{
	if (!IsOwnerRecoverable())
	{
		return false;
	}

	APawn* Pawn = GetOwnerPawn();
	if (!Pawn)
	{
		return false;
	}

	FAeyerjiNavSafetyResult CurrentResult;
	if (UAeyerjiNavSafetyLibrary::IsPawnOnUsableNav(Pawn, ResolveParams, CurrentResult))
	{
		UpdateLastSafeLocation(CurrentResult);
		OffNavStartTime = -1.0;
		return true;
	}

	if (!bImmediateRecover)
	{
		return false;
	}

	return RecoverOwnerToSafeNav();
}

bool UAeyerjiNavSafetyComponent::RecoverOwnerToSafeNav()
{
	if (!IsOwnerRecoverable())
	{
		return false;
	}

	APawn* Pawn = GetOwnerPawn();
	if (!Pawn)
	{
		return false;
	}

	const bool bUseLastSafe = LastSafeNavTime >= 0.0;
	FVector RecoveredLocation = Pawn->GetActorLocation();
	const bool bRecovered = UAeyerjiNavSafetyLibrary::RecoverPawnToSafeNav(
		Pawn,
		Pawn->GetActorLocation(),
		LastSafeNavLocation,
		bUseLastSafe,
		ResolveParams,
		RecoveredLocation);

	if (bRecovered)
	{
		LastSafeNavLocation = RecoveredLocation;
		LastSafeNavRotation = Pawn->GetActorRotation();
		if (UWorld* World = GetWorld())
		{
			LastSafeNavTime = World->GetTimeSeconds();
		}
		OffNavStartTime = -1.0;
	}

	return bRecovered;
}

APawn* UAeyerjiNavSafetyComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}

bool UAeyerjiNavSafetyComponent::IsOwnerRecoverable() const
{
	const APawn* Pawn = GetOwnerPawn();
	if (!Pawn || !Pawn->HasAuthority())
	{
		return false;
	}

	if (Pawn->ActorHasTag(AeyerjiTags::State_Dead.GetTag().GetTagName()))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true);
	return !ASC || !ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead);
}

bool UAeyerjiNavSafetyComponent::IsOwnerBelowCriticalZ(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}

	float CriticalZ = AbsoluteCriticalZ;
	if (const UWorld* World = Pawn->GetWorld())
	{
		if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
		{
			CriticalZ = FMath::Max(CriticalZ, WorldSettings->KillZ + KillZRecoveryMargin);
		}
	}

	return Pawn->GetActorLocation().Z <= CriticalZ;
}

void UAeyerjiNavSafetyComponent::UpdateLastSafeLocation(const FAeyerjiNavSafetyResult& Result)
{
	if (!Result.bSuccess)
	{
		return;
	}

	const APawn* Pawn = GetOwnerPawn();
	LastSafeNavLocation = Result.GroundedLocation;
	LastSafeNavRotation = Pawn ? Pawn->GetActorRotation() : FRotator::ZeroRotator;
	if (UWorld* World = GetWorld())
	{
		LastSafeNavTime = World->GetTimeSeconds();
	}
}
