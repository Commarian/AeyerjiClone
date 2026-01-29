// GA_AGGravitonPull.cpp

#include "Abilities/GravitonPull/GA_AGGravitonPull.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DrawDebugHelpers.h"
#include "Logging/AeyerjiLog.h"

#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

namespace GravitonTags
{
	const FGameplayTag AbilityTag  = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"));
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Graviton"));
	const FGameplayTag EventTag    = FGameplayTag::RequestGameplayTag(TEXT("Event.Ability.AG.GravitonPull"));
}

UGA_AGGravitonPull::UGA_AGGravitonPull()
{
	// Set the ability's asset tag so TryActivateAbilitiesByTag can find it.
	{
		FGameplayTagContainer AssetTags;
		AssetTags.AddTag(GravitonTags::AbilityTag);
		SetAssetTags(AssetTags);
	}
	{
		FAbilityTriggerData Trigger;
		Trigger.TriggerTag    = GravitonTags::EventTag;
		Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		AbilityTriggers.Add(Trigger);
	}

	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	DefaultDamageTypeTag = AeyerjiTags::DamageType_Physical;

	if (!DamageSetByCallerTag.IsValid())
	{
		DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.Instant"), /*ErrorIfNotFound=*/false);
	}
}

void UGA_AGGravitonPull::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo,
                                         const FGameplayAbilityActivationInfo ActivationInfo,
                                         const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	if (!GravitonConfig)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	// Commit cost & cooldown first (Diablo-style: miss still consumes resource).
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		AJ_LOG(this, TEXT("GravitonPull: CommitAbility failed (cost/cd)"));
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}

	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal   = FVector::UpVector;
	AActor* Target = nullptr;

	// Prefer any target data passed in from the controller.
	if (TriggerEventData && TriggerEventData->TargetData.Num() > 0)
	{
		if (const FGameplayAbilityTargetData* TD = TriggerEventData->TargetData.Get(0))
		{
			const TArray<TWeakObjectPtr<AActor>> Actors = TD->GetActors();
			if (Actors.Num() > 0 && Actors[0].IsValid())
			{
				Target = Actors[0].Get();
			}

			if (const FHitResult* HR = TD->GetHitResult())
			{
				HitLocation = HR->ImpactPoint;
				HitNormal   = HR->ImpactNormal;
			}
			else if (const FGameplayAbilityTargetData_LocationInfo* LocData = static_cast<const FGameplayAbilityTargetData_LocationInfo*>(TD))
			{
				HitLocation = LocData->GetEndPoint();
				HitNormal   = FVector::UpVector;
			}
		}
	}

	// Fall back to a line trace if nothing was provided.
	if (!Target)
	{
		Target = FindTargetForPull(*ActorInfo, HitLocation, HitNormal);
	}

	if (!Target)
	{
		AJ_LOG(this, TEXT("GravitonPull: no valid target found (MaxRange=%.1f)"),
		       GravitonConfig ? GravitonConfig->Tunables.MaxRange : 0.f);
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		return;
	}

	// Range-check provided targets to keep behavior consistent with the preview circle.
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		const float Dist = FVector::Dist(ActorInfo->AvatarActor->GetActorLocation(), Target->GetActorLocation());
		const float MaxRange = GravitonConfig ? GravitonConfig->Tunables.MaxRange : 0.f;
		if (MaxRange > 0.f && Dist > MaxRange + KINDA_SMALL_NUMBER)
		{
			AJ_LOG(this, TEXT("GravitonPull: target %s rejected (dist=%.1f > MaxRange=%.1f)"),
			       *GetNameSafe(Target), Dist, MaxRange);
			EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
			return;
		}
	}

	if (HitLocation.IsNearlyZero() && Target)
	{
		HitLocation = Target->GetActorLocation();
		HitNormal   = FVector::UpVector;
	}

	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (Target == ActorInfo->AvatarActor.Get())
		{
			AJ_LOG(this, TEXT("GravitonPull: ignoring self target"));
			EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
			return;
		}
	}

	// Only operate on pawns/characters.
	if (!Target->IsA(APawn::StaticClass()))
	{
		AJ_LOG(this, TEXT("GravitonPull: target %s rejected (not a Pawn)"), *GetNameSafe(Target));
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		return;
	}

	AJ_LOG(this, TEXT("GravitonPull: target %s at %s normal=%s"),
	       *GetNameSafe(Target),
	       *HitLocation.ToCompactString(),
	       *HitNormal.ToCompactString());

	// Visuals first (so damage/pull don't run without VFX if something goes wrong visually).
	PlayPullVisuals(Target, HitLocation, HitNormal, *ActorInfo);

	// Apply gameplay effects.
	ApplyEffectsToTarget(Target, *ActorInfo);

	// Teleport target toward the Astral Guardian.
	PullTarget(Target, HitLocation, *ActorInfo);

	// Ability is immediate; no montage / wait states here.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_AGGravitonPull::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                    const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    bool bReplicateEndAbility,
                                    bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AActor* UGA_AGGravitonPull::FindTargetForPull_Implementation(const FGameplayAbilityActorInfo& ActorInfo,
                                                             FVector& OutHitLocation,
                                                             FVector& OutHitNormal) const
{
	OutHitLocation = FVector::ZeroVector;
	OutHitNormal   = FVector::UpVector;

	if (!ActorInfo.AvatarActor.IsValid())
	{
		return nullptr;
	}

	AActor* Avatar = ActorInfo.AvatarActor.Get();
	if (!Avatar)
	{
		return nullptr;
	}

	const APawn* Pawn = Cast<APawn>(Avatar);
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;

	FVector Start;
	FRotator ViewRot;

	// Use player/controller view if possible, otherwise use actor eyes.
	if (Controller)
	{
		Controller->GetPlayerViewPoint(Start, ViewRot);
	}
	else
	{
		Avatar->GetActorEyesViewPoint(Start, ViewRot);
	}

	const FVector Direction = ViewRot.Vector();
	const float MaxRange = (GravitonConfig ? GravitonConfig->Tunables.MaxRange : 1200.f);
	const FVector End = Start + Direction * MaxRange;

	UWorld* World = Avatar->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GravitonPullTrace), false, Avatar);
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const ECollisionChannel TraceChannel = ECC_Visibility; // Adjust if you have a dedicated ability channel.

	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params);
#if WITH_EDITOR
	// Debug lines if you need them while tuning.
	// DrawDebugLine(World, Start, End, FColor::Purple, false, 1.0f, 0, 1.f);
	// if (bHit) DrawDebugSphere(World, Hit.ImpactPoint, 16.f, 12, FColor::Red, false, 1.0f);
#endif

	if (!bHit || !Hit.GetActor())
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (HitActor == Avatar)
	{
		return nullptr;
	}

	// Very simple filter: we only pull Pawns/Characters.
	if (!HitActor->IsA(APawn::StaticClass()))
	{
		AJ_LOG(this, TEXT("GravitonPull: hit %s but rejected (not a Pawn)"),
		       *GetNameSafe(HitActor));
		return nullptr;
	}

	OutHitLocation = Hit.ImpactPoint;
	OutHitNormal   = Hit.ImpactNormal;
	return HitActor;
}

void UGA_AGGravitonPull::PullTarget_Implementation(AActor* Target,
                                                   const FVector& HitLocation,
                                                   const FGameplayAbilityActorInfo& ActorInfo) const
{
	if (!Target || !ActorInfo.AvatarActor.IsValid() || !GravitonConfig)
	{
		return;
	}

	AActor* Avatar = ActorInfo.AvatarActor.Get();
	if (!Avatar)
	{
		return;
	}

	const FVector AvatarLocation = Avatar->GetActorLocation();
	const FVector TargetLocation = Target->GetActorLocation();

	// Compute direction from target toward the Astral Guardian.
	FVector Direction = AvatarLocation - TargetLocation;
	if (!Direction.Normalize())
	{
		return;
	}

	const float DesiredPullDistance = GravitonConfig->Tunables.PullDistance;

	// Optional heavy scaling based on tag (e.g., actors tagged "Heavy" are pulled less).
	float PullScale = 1.0f;
	if (Target->ActorHasTag(FName(TEXT("Heavy"))))
	{
		PullScale = GravitonConfig->Tunables.HeavyTargetPullScale;
	}

	const float EffectivePull = FMath::Max(0.0f, DesiredPullDistance * PullScale);

	// Do not overshoot the Avatar; leave a small buffer (e.g., 150 units).
	const float DistanceToAvatar = FVector::Dist(TargetLocation, AvatarLocation);
	const float MaxAllowedPull = FMath::Max(0.0f, DistanceToAvatar - 150.f);
	const float FinalPullDistance = FMath::Min(EffectivePull, MaxAllowedPull);

	if (FinalPullDistance <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector NewLocation = TargetLocation + Direction * FinalPullDistance;

	// Teleport the target; sweep to avoid sticking in walls.
	Target->SetActorLocation(NewLocation, true);
	AJ_LOG(this, TEXT("GravitonPull: pulled %s from %s -> %s (dist=%.1f)"),
	       *GetNameSafe(Target),
	       *TargetLocation.ToCompactString(),
	       *NewLocation.ToCompactString(),
	       FinalPullDistance);
}

void UGA_AGGravitonPull::ApplyEffectsToTarget_Implementation(AActor* Target,
                                                             const FGameplayAbilityActorInfo& ActorInfo) const
{
	if (!Target || !ActorInfo.AvatarActor.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = ActorInfo.AbilitySystemComponent.Get();
	if (!SourceASC)
	{
		return;
	}

	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!TargetASC)
	{
		return;
	}

	UWorld* World = Target->GetWorld();
	if (!World)
	{
		return;
	}

	const float HitDamage    = GravitonConfig ? GravitonConfig->Tunables.HitDamage    : 0.f;
	const float SlowDuration = GravitonConfig ? GravitonConfig->Tunables.SlowDuration : 0.f;
	const float SlowPercent  = GravitonConfig ? GravitonConfig->Tunables.SlowPercent  : 0.f;
	const bool  bApplyAilment = GravitonConfig ? GravitonConfig->Tunables.bApplyAilment : false;
	const int32 AilmentStacks = GravitonConfig ? GravitonConfig->Tunables.AilmentStacks : 0;

	// Damage
	if (HitDamage > 0.f)
	{
		TSubclassOf<UGameplayEffect> ResolvedDamageClass = DamageEffectClass;
		if (!ResolvedDamageClass)
		{
			ResolvedDamageClass = UGE_DamagePhysical::StaticClass();
		}
		if (!ResolvedDamageClass)
		{
			return;
		}

		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		ContextHandle.AddSourceObject(this);

		FGameplayEffectSpecHandle SpecHandle =
			SourceASC->MakeOutgoingSpec(ResolvedDamageClass, GetAbilityLevel(), ContextHandle);

		if (SpecHandle.IsValid() && SpecHandle.Data.IsValid())
		{
			ApplyDamageTypeTagToSpec(SpecHandle, DefaultDamageTypeTag);

			const FGameplayTag DamageTag = DamageSetByCallerTag.IsValid()
				? DamageSetByCallerTag
				: FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.Instant"), /*ErrorIfNotFound=*/false);
			if (DamageTag.IsValid())
			{
				SpecHandle.Data->SetSetByCallerMagnitude(DamageTag, HitDamage);
			}

			TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	// Slow
	if (SlowEffectClass && SlowPercent > 0.f && SlowDuration > 0.f)
	{
		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		ContextHandle.AddSourceObject(this);

		FGameplayEffectSpecHandle SpecHandle =
			SourceASC->MakeOutgoingSpec(SlowEffectClass, GetAbilityLevel(), ContextHandle);

		if (SpecHandle.IsValid())
		{
			if (SlowSetByCallerTag.IsValid())
			{
				SpecHandle.Data->SetSetByCallerMagnitude(SlowSetByCallerTag, SlowPercent);
			}

			TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	// Ailment stacks
	if (bApplyAilment && AilmentEffectClass && AilmentStacks > 0)
	{
		FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
		ContextHandle.AddSourceObject(this);

		FGameplayEffectSpecHandle SpecHandle =
			SourceASC->MakeOutgoingSpec(AilmentEffectClass, GetAbilityLevel(), ContextHandle);

		if (SpecHandle.IsValid())
		{
			if (AilmentStacksSetByCallerTag.IsValid())
			{
				SpecHandle.Data->SetSetByCallerMagnitude(AilmentStacksSetByCallerTag, static_cast<float>(AilmentStacks));
			}

			TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}
}

void UGA_AGGravitonPull::PlayPullVisuals_Implementation(AActor* Target,
                                                        const FVector& HitLocation,
                                                        const FVector& HitNormal,
                                                        const FGameplayAbilityActorInfo& ActorInfo) const
{
	if (!ActorInfo.AvatarActor.IsValid())
	{
		return;
	}

	AActor* Avatar = ActorInfo.AvatarActor.Get();
	if (!Avatar)
	{
		return;
	}

	UWorld* World = Avatar->GetWorld();
	if (!World)
	{
		return;
	}

	// Resolve a sensible start location: try mesh socket, fallback to actor location.
	FVector StartLocation = Avatar->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(Avatar))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			// Replace "Hand_R" with the socket you want, or expose as config.
			static const FName SocketName(TEXT("Hand_R"));
			if (Mesh->DoesSocketExist(SocketName))
			{
				StartLocation = Mesh->GetSocketLocation(SocketName);
			}
		}
	}

	const FVector EndLocation = HitLocation;

	// Beam VFX: quick tether from AG to hit point.
	if (BeamVFX)
	{
		UNiagaraComponent* BeamComp =
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, BeamVFX, StartLocation, FRotator::ZeroRotator);

		if (BeamComp)
		{
			// If your NS expects user parameters "User.StartPosition" and "User.EndPosition".
			BeamComp->SetVariableVec3(FName(TEXT("User.StartPosition")), StartLocation);
			BeamComp->SetVariableVec3(FName(TEXT("User.EndPosition")),   EndLocation);

			// Lifetime and fade are handled inside the Niagara system (no tick logic here).
		}
	}

	// Impact VFX: burst at the target location.
	if (ImpactVFX)
	{
		const FRotator ImpactRot = HitNormal.Rotation();
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, ImpactVFX, EndLocation, ImpactRot);
	}
}
