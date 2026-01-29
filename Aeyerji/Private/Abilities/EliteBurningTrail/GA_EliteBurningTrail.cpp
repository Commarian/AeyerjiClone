// GA_EliteBurningTrail.cpp

#include "Abilities/EliteBurningTrail/GA_EliteBurningTrail.h"

#include "Abilities/EliteBurningTrail/DA_EliteBurningTrail.h"
#include "Abilities/EliteBurningTrail/EliteBurningTrailPatch.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

namespace EliteBurningTrailTags
{
	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Elite.BurningTrail"), /*ErrorIfNotFound=*/false);
}

UGA_EliteBurningTrail::UGA_EliteBurningTrail()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
	bServerRespectsRemoteAbilityCancellation = true;
	if (EliteBurningTrailTags::AbilityTag.IsValid())
	{
		FGameplayTagContainer AssetTags = GetAssetTags();
		AssetTags.AddTag(EliteBurningTrailTags::AbilityTag);
		SetAssetTags(AssetTags);
	}

	if (!DamageSetByCallerTag.IsValid())
	{
		DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.PerSecond"), /*ErrorIfNotFound=*/false);
	}
}

void UGA_EliteBurningTrail::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo,
                                            const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->IsNetAuthority() || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	if (!BurningTrailConfig || !PatchClass || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	LastPatchLocation = ActorInfo->AvatarActor->GetActorLocation();

	if (UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(0.05f, BurningTrailConfig->Tunables.FootstepInterval);
		World->GetTimerManager().SetTimer(FootstepTimerHandle, this, &UGA_EliteBurningTrail::HandleFootstepTick, Interval, /*bLoop=*/true);
	}
}

void UGA_EliteBurningTrail::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			ASC->TryActivateAbility(Spec.Handle);
		}
	}
}

void UGA_EliteBurningTrail::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo,
                                       const FGameplayAbilityActivationInfo ActivationInfo,
                                       bool bReplicateEndAbility,
                                       bool bWasCancelled)
{
	ClearFootstepTimer();
	ActivePatches.Empty();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_EliteBurningTrail::HandleFootstepTick()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (!ActorInfo->AvatarActor.IsValid() || !BurningTrailConfig)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	if (IsOwnerDead(ActorInfo))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
		return;
	}

	APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	if (!Pawn)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	if (!CanSpawnPatch(Pawn))
	{
		return;
	}

	TrySpawnPatch(Pawn, ActorInfo);
}

bool UGA_EliteBurningTrail::CanSpawnPatch(const APawn* Pawn) const
{
	if (!Pawn || !BurningTrailConfig)
	{
		return false;
	}

	const FVector CurrentLoc = Pawn->GetActorLocation();
	const FVector2D Current2D(CurrentLoc.X, CurrentLoc.Y);
	const FVector2D Last2D(LastPatchLocation.X, LastPatchLocation.Y);

	const float Dist2D = FVector2D::Distance(Current2D, Last2D);
	if (Dist2D < BurningTrailConfig->Tunables.MinTravelDistanceForNewPatch)
	{
		return false;
	}

	const float Speed2D = Pawn->GetVelocity().Size2D();
	if (Speed2D < 10.f)
	{
		return false;
	}

	return true;
}

bool UGA_EliteBurningTrail::TrySpawnPatch(APawn* Pawn, const FGameplayAbilityActorInfo* ActorInfo)
{
	if (!Pawn || !ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid() || !BurningTrailConfig)
	{
		return false;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return false;
	}

	FVector SpawnLocation = Pawn->GetActorLocation();
	FVector GroundNormal = FVector::UpVector;
	{
		const FVector TraceStart = SpawnLocation + FVector::UpVector * 50.f;
		const FVector TraceEnd = TraceStart - FVector::UpVector * (200.f + BurningTrailConfig->Tunables.PatchRadius);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EliteBurningTrailGroundTrace), false, Pawn);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			SpawnLocation = Hit.ImpactPoint;
			GroundNormal = Hit.ImpactNormal;
		}
	}

	ActivePatches.RemoveAll([](const TWeakObjectPtr<AEliteBurningTrailPatch>& PatchPtr)
	{
		return !PatchPtr.IsValid();
	});

	const int32 MaxPatches = FMath::Max(0, BurningTrailConfig->Tunables.MaxActivePatches);
	if (MaxPatches > 0 && ActivePatches.Num() >= MaxPatches)
	{
		if (ActivePatches[0].IsValid())
		{
			ActivePatches[0]->Destroy();
		}
		ActivePatches.RemoveAt(0);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Pawn;
	SpawnParams.Instigator = Pawn;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FRotator PatchRotation = FRotationMatrix::MakeFromZ(GroundNormal).Rotator();
	AEliteBurningTrailPatch* Patch = World->SpawnActor<AEliteBurningTrailPatch>(PatchClass, SpawnLocation, PatchRotation, SpawnParams);
	if (!Patch)
	{
		return false;
	}

	Patch->InitializePatch(BurningTrailConfig->Tunables,
		ActorInfo->AbilitySystemComponent.Get(),
		DamageSetByCallerTag,
		FGameplayTag(),
		DotEffectClass,
		this);
	ActivePatches.Add(Patch);

	LastPatchLocation = SpawnLocation;
	return true;
}

void UGA_EliteBurningTrail::ClearFootstepTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FootstepTimerHandle);
	}
}
