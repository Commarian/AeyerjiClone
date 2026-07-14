#include "Abilities/GA_AeyerjiTargetedEffectBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiGameplayTags.h"
#include "CharacterStatsLibrary.h"
#include "GAS/GE_DamagePhysical.h"
#include "Animation/AnimMontage.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

UGA_AeyerjiTargetedEffectBase::UGA_AeyerjiTargetedEffectBase()
{
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = AeyerjiTags::Event_External_Target;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
}

void UGA_AeyerjiTargetedEffectBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                    const FGameplayAbilityActorInfo* ActorInfo,
                                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                                    const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || IsOwnerDead(ActorInfo))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: activation rejected before validation (missing owner/avatar or owner dead)."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FAeyerjiAbilityResolvedConfig Config;
	if (!GetAbilityResolvedConfig(ActorInfo->AbilitySystemComponent.Get(), ResolveAbilityRank(Handle, ActorInfo), Config))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: activation rejected, no tuning row for %s."),
			*ResolveAbilityTag().ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (TriggerEventData && !TriggerEventData->InstigatorTags.IsEmpty())
	{
		const FGameplayTag ThisAbilityTag = ResolveAbilityTag();
		if (ThisAbilityTag.IsValid() && !TriggerEventData->InstigatorTags.HasTagExact(ThisAbilityTag))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	TArray<AActor*> Targets;
	FVector TargetLocation = ActorInfo->AvatarActor->GetActorLocation();
	ResolveTargets(*ActorInfo, TriggerEventData, Config, Targets, TargetLocation);

	if (!IsWithinRange(*ActorInfo, Config, TargetLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: activation rejected for %s, target location is out of range."),
			*Config.AbilityTag.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	const float ImpactDelay = CalculateImpactDelay(Config);
	if (ImpactDelay > KINDA_SMALL_NUMBER)
	{
		ApplyCastingLock(*ActorInfo);
	}

	PlayAbilityCastMontageNative(*ActorInfo, Config);

	if (ImpactDelay <= KINDA_SMALL_NUMBER)
	{
		ExecuteTargetedImpact(Handle, ActorInfo, ActivationInfo, Config, Targets, TargetLocation);
		return;
	}

	FTimerDelegate ImpactDelegate;
	ImpactDelegate.BindWeakLambda(this, [this, Handle, ActorInfo, ActivationInfo, ConfigCopy = Config, Targets, TargetLocation]()
	{
		ExecuteTargetedImpact(Handle, ActorInfo, ActivationInfo, ConfigCopy, Targets, TargetLocation);
	});

	if (UWorld* World = ActorInfo->AvatarActor->GetWorld())
	{
		World->GetTimerManager().SetTimer(DelayedImpactTimerHandle, ImpactDelegate, ImpactDelay, false);
	}
	else
	{
		ExecuteTargetedImpact(Handle, ActorInfo, ActivationInfo, Config, Targets, TargetLocation);
	}
}

void UGA_AeyerjiTargetedEffectBase::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                               const FGameplayAbilityActorInfo* ActorInfo,
                                               const FGameplayAbilityActivationInfo ActivationInfo,
                                               bool bReplicateEndAbility,
                                               bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (UWorld* World = ActorInfo->AvatarActor->GetWorld())
		{
			World->GetTimerManager().ClearTimer(DelayedImpactTimerHandle);
		}
	}

	RemoveCastingLock(ActorInfo);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

float UGA_AeyerjiTargetedEffectBase::CalculateImpactDelay(const FAeyerjiAbilityResolvedConfig& Config) const
{
	if (Config.Visuals.ImpactDelaySeconds >= 0.f)
	{
		return Config.Visuals.ImpactDelaySeconds;
	}

	if (Config.Visuals.Montage.IsNull())
	{
		return 0.f;
	}

	UAnimMontage* Montage = Config.Visuals.Montage.Get();
	if (!Montage)
	{
		Montage = Config.Visuals.Montage.LoadSynchronous();
	}

	if (!Montage)
	{
		return 0.f;
	}

	const float PlayRate = FMath::Max(0.01f, Config.Visuals.MontagePlayRate);
	return FMath::Max(0.f, Montage->GetPlayLength() * 0.5f / PlayRate);
}

void UGA_AeyerjiTargetedEffectBase::ApplyCastingLock(const FGameplayAbilityActorInfo& ActorInfo) const
{
	UAbilitySystemComponent* ASC = ActorInfo.AbilitySystemComponent.Get();
	if (ASC && AeyerjiTags::State_Ability_Casting.GetTag().IsValid())
	{
		FGameplayTagContainer PrimaryAttackTags;
		PrimaryAttackTags.AddTag(AeyerjiTags::Ability_Primary);
		ASC->CancelAbilities(&PrimaryAttackTags);
		ASC->AddLooseGameplayTag(AeyerjiTags::State_Ability_Casting, 1, EGameplayTagReplicationState::TagOnly);
	}

	APawn* Pawn = Cast<APawn>(ActorInfo.AvatarActor.Get());
	if (!Pawn)
	{
		return;
	}

	if (AController* Controller = Pawn->GetController())
	{
		Controller->StopMovement();
	}

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
}

void UGA_AeyerjiTargetedEffectBase::RemoveCastingLock(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (!ActorInfo || !AeyerjiTags::State_Ability_Casting.GetTag().IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		if (ASC->GetTagCount(AeyerjiTags::State_Ability_Casting) > 0)
		{
			ASC->RemoveLooseGameplayTag(AeyerjiTags::State_Ability_Casting, 1, EGameplayTagReplicationState::TagOnly);
		}
	}
}

void UGA_AeyerjiTargetedEffectBase::PlayAbilityCastMontageNative(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config) const
{
	AAeyerjiCharacter* Character = Cast<AAeyerjiCharacter>(ActorInfo.AvatarActor.Get());
	if (!Character || !Character->HasAuthority())
	{
		return;
	}

	if (Config.Visuals.Montage.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: %s has no cast montage configured."),
			*Config.AbilityTag.ToString());
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("TargetedAbility: playing cast montage for %s from %s."),
		*Config.AbilityTag.ToString(),
		*Config.Visuals.Montage.ToSoftObjectPath().ToString());
	Character->Multicast_PlayAbilityMontageByPath(Config.Visuals.Montage.ToSoftObjectPath(), Config.Visuals.MontagePlayRate);
}

void UGA_AeyerjiTargetedEffectBase::ExecuteTargetedImpact(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FAeyerjiAbilityResolvedConfig Config,
	const TArray<AActor*> Targets,
	FVector TargetLocation)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !IsActive())
	{
		return;
	}

	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, true))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: impact rejected for %s, commit failed."),
			*Config.AbilityTag.ToString());
		return;
	}

	ApplyResolvedConfigEffects(Handle, *ActorInfo, ActivationInfo, Config, Targets);
	UE_LOG(LogTemp, Display, TEXT("TargetedAbility: activated %s Targets=%d."),
		*Config.AbilityTag.ToString(),
		Targets.Num());
	OnTargetedAbilityAppliedNative(*ActorInfo, Config, Targets, TargetLocation);
	BP_OnTargetedAbilityApplied(Targets, TargetLocation);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_AeyerjiTargetedEffectBase::OnTargetedAbilityAppliedNative(
	const FGameplayAbilityActorInfo& ActorInfo,
	const FAeyerjiAbilityResolvedConfig& Config,
	const TArray<AActor*>& Targets,
	FVector TargetLocation) const
{
	AAeyerjiCharacter* Character = Cast<AAeyerjiCharacter>(ActorInfo.AvatarActor.Get());
	if (!Character || !Character->HasAuthority())
	{
		return;
	}

	if (Config.Visuals.NiagaraSystem.IsNull())
	{
		return;
	}

	UNiagaraSystem* NiagaraSystem = Config.Visuals.NiagaraSystem.Get();
	if (!NiagaraSystem)
	{
		NiagaraSystem = Config.Visuals.NiagaraSystem.LoadSynchronous();
	}

	if (!NiagaraSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: failed to load Niagara for %s from %s."),
			*Config.AbilityTag.ToString(),
			*Config.Visuals.NiagaraSystem.ToSoftObjectPath().ToString());
		return;
	}

	const FVector NiagaraLocation = TargetLocation
		+ (Config.Visuals.bAttachNiagaraToOwnerMesh ? FVector::ZeroVector : Config.Visuals.NiagaraOffset);
	const FVector AttachedOffset = Config.Visuals.bAttachNiagaraToOwnerMesh ? Config.Visuals.NiagaraOffset : FVector::ZeroVector;

	Character->Multicast_PlayAbilityCosmetics(
		nullptr,
		Config.Visuals.MontagePlayRate,
		NiagaraSystem,
		NiagaraLocation,
		Character->GetActorRotation(),
		Config.Visuals.NiagaraScale,
		Config.Visuals.bAttachNiagaraToOwnerMesh,
		Config.Visuals.NiagaraAttachSocket,
		AttachedOffset);
}

bool UGA_AeyerjiTargetedEffectBase::ResolveTargetLocation(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, FVector& OutLocation) const
{
	OutLocation = ActorInfo.AvatarActor.IsValid() ? ActorInfo.AvatarActor->GetActorLocation() : FVector::ZeroVector;

	if (!TriggerEventData || TriggerEventData->TargetData.Num() <= 0)
	{
		return ActorInfo.AvatarActor.IsValid();
	}

	if (const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(0))
	{
		if (const FHitResult* Hit = Data->GetHitResult())
		{
			OutLocation = Hit->ImpactPoint;
			return true;
		}

		const TArray<TWeakObjectPtr<AActor>> Actors = Data->GetActors();
		if (!Actors.IsEmpty() && Actors[0].IsValid())
		{
			OutLocation = Actors[0]->GetActorLocation();
			return true;
		}

		const FGameplayAbilityTargetData_LocationInfo* LocationData = static_cast<const FGameplayAbilityTargetData_LocationInfo*>(Data);
		if (LocationData)
		{
			OutLocation = LocationData->GetEndPoint();
			return true;
		}
	}

	return false;
}

void UGA_AeyerjiTargetedEffectBase::ResolveTargets(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, const FAeyerjiAbilityResolvedConfig& Config, TArray<AActor*>& OutTargets, FVector& OutTargetLocation) const
{
	OutTargets.Reset();
	ResolveTargetLocation(ActorInfo, TriggerEventData, OutTargetLocation);

	if (Config.TargetTeam == EAeyerjiAbilityTargetTeam::Self)
	{
		if (ActorInfo.AvatarActor.IsValid())
		{
			OutTargets.Add(ActorInfo.AvatarActor.Get());
		}
		return;
	}

	if (Config.Shape == EAeyerjiAbilityTargetShape::SingleActor && TriggerEventData)
	{
		for (int32 DataIdx = 0; DataIdx < TriggerEventData->TargetData.Num(); ++DataIdx)
		{
			if (const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(DataIdx))
			{
				for (const TWeakObjectPtr<AActor>& ActorPtr : Data->GetActors())
				{
					if (ActorPtr.IsValid() && IsTargetAllowed(ActorInfo, Config, ActorPtr.Get()))
					{
						OutTargets.AddUnique(ActorPtr.Get());
					}
				}
			}
		}
	}
	else
	{
		GatherShapeTargets(ActorInfo, Config, OutTargetLocation, OutTargets);
	}

	if (Config.MaxTargets > 0 && OutTargets.Num() > Config.MaxTargets)
	{
		OutTargets.SetNum(Config.MaxTargets);
	}
}

void UGA_AeyerjiTargetedEffectBase::GatherShapeTargets(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, const FVector& TargetLocation, TArray<AActor*>& OutTargets) const
{
	AActor* Avatar = ActorInfo.AvatarActor.Get();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FVector Origin = Config.Shape == EAeyerjiAbilityTargetShape::GroundRadius ? TargetLocation : Avatar->GetActorLocation();
	const float Radius = FMath::Max(Config.Radius, Config.Shape == EAeyerjiAbilityTargetShape::OwnerCone ? Config.MaxRange : 0.f);
	if (Radius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AeyerjiTargetedAbilityOverlap), false, Avatar);
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Radius),
		Params);

	const FVector AvatarForward = Avatar->GetActorForwardVector();
	const float HalfAngle = FMath::Clamp(Config.ArcAngleDegrees * 0.5f, 0.f, 180.f);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!IsTargetAllowed(ActorInfo, Config, Candidate))
		{
			continue;
		}

		if (Config.Shape == EAeyerjiAbilityTargetShape::OwnerCone)
		{
			FVector ToCandidate = Candidate->GetActorLocation() - Avatar->GetActorLocation();
			ToCandidate.Z = 0.f;
			if (!ToCandidate.Normalize())
			{
				continue;
			}

			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(AvatarForward, ToCandidate), -1.f, 1.f)));
			if (Angle > HalfAngle)
			{
				continue;
			}
		}

		OutTargets.AddUnique(Candidate);
	}
}

bool UGA_AeyerjiTargetedEffectBase::IsTargetAllowed(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, AActor* Target) const
{
	AActor* Avatar = ActorInfo.AvatarActor.Get();
	if (!Target || !Avatar)
	{
		return false;
	}

	if (Target == Avatar)
	{
		return Config.TargetTeam == EAeyerjiAbilityTargetTeam::Self || Config.TargetTeam == EAeyerjiAbilityTargetTeam::Friendly || Config.TargetTeam == EAeyerjiAbilityTargetTeam::Any;
	}

	switch (Config.TargetTeam)
	{
	case EAeyerjiAbilityTargetTeam::Enemy:
		return !AbilityTeamUtils::AreOnSameTeam(Avatar, Target);
	case EAeyerjiAbilityTargetTeam::Friendly:
		return AbilityTeamUtils::AreOnSameTeam(Avatar, Target);
	case EAeyerjiAbilityTargetTeam::Any:
		return true;
	case EAeyerjiAbilityTargetTeam::Self:
	default:
		return false;
	}
}

bool UGA_AeyerjiTargetedEffectBase::IsWithinRange(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, const FVector& TargetLocation) const
{
	if (!ActorInfo.AvatarActor.IsValid() || Config.MaxRange <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	return FVector::DistSquared(ActorInfo.AvatarActor->GetActorLocation(), TargetLocation) <= FMath::Square(Config.MaxRange + KINDA_SMALL_NUMBER);
}

float UGA_AeyerjiTargetedEffectBase::EvaluateMagnitude(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityMagnitude& Magnitude) const
{
	float Value = Magnitude.FlatValue;
	if (Magnitude.SourceStat != EAeyerjiStat::None && Magnitude.SourceStatScalar != 0.f && ActorInfo.AvatarActor.IsValid())
	{
		float StatValue = 0.f;
		if (UCharacterStatsLibrary::GetAeyerjiStatFromActor(ActorInfo.AvatarActor.Get(), Magnitude.SourceStat, StatValue))
		{
			Value += StatValue * Magnitude.SourceStatScalar;
		}
	}
	return Value;
}

void UGA_AeyerjiTargetedEffectBase::ApplyResolvedConfigEffects(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo& ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FAeyerjiAbilityResolvedConfig& Config, const TArray<AActor*>& Targets) const
{
	UAbilitySystemComponent* SourceASC = ActorInfo.AbilitySystemComponent.Get();
	if (!SourceASC)
	{
		return;
	}

	auto ApplySpecToTarget = [&](AActor* Target,
	                             TSubclassOf<UGameplayEffect> EffectClass,
	                             float Level,
	                             const FGameplayTag& SetByCallerTag,
	                             float Magnitude,
	                             const FGameplayTag& DamageTypeTag,
	                             const FAeyerjiDamageRuleConfig& DamageRules) -> FActiveGameplayEffectHandle
	{
		if (!Target || !EffectClass)
		{
			return FActiveGameplayEffectHandle();
		}

		UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, true);
		if (!TargetASC)
		{
			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: %s has no ASC; skipped %s."),
				*GetNameSafe(Target),
				*GetNameSafe(EffectClass));
			return FActiveGameplayEffectHandle();
		}

		FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
		Context.AddSourceObject(this);
		FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(EffectClass, FMath::Max(0.01f, Level), Context);
		if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: failed to create spec for %s on %s."),
				*GetNameSafe(EffectClass),
				*GetNameSafe(Target));
			return FActiveGameplayEffectHandle();
		}

		if (SetByCallerTag.IsValid())
		{
			SpecHandle.Data->SetSetByCallerMagnitude(SetByCallerTag, Magnitude);
		}
		if (DamageTypeTag.IsValid())
		{
			SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
		}
		DamageRules.ApplyToSpec(*SpecHandle.Data.Get());

		return TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	};

	const float DamageMagnitude = EvaluateMagnitude(ActorInfo, Config.Damage);
	TSubclassOf<UGameplayEffect> DamageClass = Config.Damage.GameplayEffectClass.IsNull()
		? UGE_DamagePhysical::StaticClass()
		: Config.Damage.GameplayEffectClass.Get();
	const FGameplayTag DamageSetByCaller = Config.Damage.SetByCallerTag.IsValid() ? Config.Damage.SetByCallerTag : AeyerjiTags::SBC_Damage_Instant;
	if (DamageMagnitude > KINDA_SMALL_NUMBER && DamageClass)
	{
		for (AActor* Target : Targets)
		{
			ApplySpecToTarget(
				Target,
				DamageClass,
				GetAbilityLevel(Handle, &ActorInfo),
				DamageSetByCaller,
				DamageMagnitude,
				Config.Damage.DamageTypeTag,
				Config.Damage.MakeDamageRuleConfig());
		}
	}

	for (const FAeyerjiAbilityAppliedEffect& Effect : Config.AdditionalEffects)
	{
		TSubclassOf<UGameplayEffect> EffectClass = Effect.GameplayEffectClass.Get();
		if (!EffectClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: Additional effect class is not loaded. SetByCaller=%s Magnitude=%.2f"),
				*Effect.SetByCallerTag.ToString(),
				Effect.Magnitude);
			continue;
		}

		for (AActor* Target : Targets)
		{
			FActiveGameplayEffectHandle AppliedHandle =
				ApplySpecToTarget(
					Target,
					EffectClass,
					Effect.EffectLevel,
					Effect.SetByCallerTag,
					Effect.Magnitude,
					FGameplayTag(),
					FAeyerjiDamageRuleConfig());

			UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, true);
			int32 StunTagCount = TargetASC ? TargetASC->GetTagCount(AeyerjiTags::State_CrowdControl_Stunned) : 0;

			if (Target
				&& Target->HasAuthority()
				&& TargetASC
				&& Effect.SetByCallerTag == AeyerjiTags::SBC_Stun_Duration
				&& Effect.Magnitude > KINDA_SMALL_NUMBER
				&& StunTagCount <= 0)
			{
				TargetASC->AddLooseGameplayTag(AeyerjiTags::State_CrowdControl_Stunned, 1, EGameplayTagReplicationState::TagOnly);
				StunTagCount = TargetASC->GetTagCount(AeyerjiTags::State_CrowdControl_Stunned);

				TWeakObjectPtr<UAbilitySystemComponent> WeakTargetASC = TargetASC;
				if (UWorld* TargetWorld = Target->GetWorld())
				{
					FTimerHandle RemoveStunTagTimer;
					TargetWorld->GetTimerManager().SetTimer(
						RemoveStunTagTimer,
						[WeakTargetASC]()
						{
							if (UAbilitySystemComponent* ASC = WeakTargetASC.Get())
							{
								ASC->RemoveLooseGameplayTag(AeyerjiTags::State_CrowdControl_Stunned, 1, EGameplayTagReplicationState::TagOnly);
							}
						},
						Effect.Magnitude,
						false);
				}

				UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: Applied replicated stun fallback Target=%s Duration=%.2f StunTagCount=%d"),
					*GetNameSafe(Target),
					Effect.Magnitude,
					StunTagCount);
			}
			if (!AppliedHandle.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: failed to apply additional effect Class=%s Target=%s."),
					*GetNameSafe(EffectClass),
					*GetNameSafe(Target));
			}
		}
	}
}
