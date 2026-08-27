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

	if (TargetLocation.ContainsNaN() || !IsWithinRange(*ActorInfo, Config, TargetLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: activation rejected for %s, target location is out of range."),
			*Config.AbilityTag.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	if (Config.Shape == EAeyerjiAbilityTargetShape::SingleActor
		&& Config.TargetTeam != EAeyerjiAbilityTargetTeam::Self
		&& Targets.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: activation rejected for %s, no valid actor target."),
			*Config.AbilityTag.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	const float ImpactDelay = CalculateAbilityImpactDelay(Config);
	BeginAbilityCastPresentation(*ActorInfo, Config, ImpactDelay);

	if (ImpactDelay <= KINDA_SMALL_NUMBER)
	{
		ExecuteTargetedImpact(Handle, ActorInfo, ActivationInfo, Config, Targets, TargetLocation);
		return;
	}

	TArray<TWeakObjectPtr<AActor>> WeakTargets;
	WeakTargets.Reserve(Targets.Num());
	for (AActor* Target : Targets)
	{
		WeakTargets.Add(Target);
	}

	FTimerDelegate ImpactDelegate;
	ImpactDelegate.BindWeakLambda(this, [this, Handle, ActorInfo, ActivationInfo, ConfigCopy = Config, WeakTargets, TargetLocation]()
	{
		TArray<AActor*> ValidTargets;
		for (const TWeakObjectPtr<AActor>& WeakTarget : WeakTargets)
		{
			AActor* Target = WeakTarget.Get();
			if (Target && ActorInfo && IsTargetAllowed(*ActorInfo, ConfigCopy, Target)
				&& (ConfigCopy.Shape != EAeyerjiAbilityTargetShape::SingleActor
					|| IsWithinRange(*ActorInfo, ConfigCopy, Target->GetActorLocation())))
			{
				ValidTargets.Add(Target);
			}
		}
		ExecuteTargetedImpact(Handle, ActorInfo, ActivationInfo, ConfigCopy, ValidTargets, TargetLocation);
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

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_AeyerjiTargetedEffectBase::ExecuteTargetedImpact(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FAeyerjiAbilityResolvedConfig& Config,
	const TArray<AActor*>& Targets,
	FVector TargetLocation)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || !IsActive())
	{
		return;
	}
	if (IsOwnerDead(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (TargetLocation.ContainsNaN() || !IsWithinRange(*ActorInfo, Config, TargetLocation))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	TArray<AActor*> ValidTargets;
	if (Config.TargetTeam == EAeyerjiAbilityTargetTeam::Self)
	{
		ValidTargets.Add(ActorInfo->AvatarActor.Get());
	}
	else if (Config.Shape == EAeyerjiAbilityTargetShape::SingleActor)
	{
		for (AActor* Target : Targets)
		{
			if (IsTargetAllowed(*ActorInfo, Config, Target)
				&& IsWithinRange(*ActorInfo, Config, Target->GetActorLocation()))
			{
				ValidTargets.AddUnique(Target);
			}
		}
	}
	else
	{
		// Area and cone attacks resolve occupants at impact time so actors that leave
		// a telegraphed shape are not hit by a stale activation-time overlap result.
		GatherShapeTargets(*ActorInfo, Config, TargetLocation, ValidTargets);
	}

	if (Config.Shape == EAeyerjiAbilityTargetShape::SingleActor
		&& Config.TargetTeam != EAeyerjiAbilityTargetTeam::Self
		&& ValidTargets.IsEmpty())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (Config.MaxTargets > 0 && ValidTargets.Num() > Config.MaxTargets)
	{
		ValidTargets.SetNum(Config.MaxTargets);
	}

	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, true))
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetedAbility: impact rejected for %s, commit failed."),
			*Config.AbilityTag.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ApplyResolvedConfigEffects(Handle, *ActorInfo, Config, ValidTargets);
	UE_LOG(LogTemp, Display, TEXT("TargetedAbility: activated %s Targets=%d."),
		*Config.AbilityTag.ToString(),
		ValidTargets.Num());
	OnTargetedAbilityAppliedNative(*ActorInfo, Config, ValidTargets, TargetLocation);
	BP_OnTargetedAbilityApplied(ValidTargets, TargetLocation);
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

		const UScriptStruct* TargetDataStruct = Data->GetScriptStruct();
		if (TargetDataStruct && TargetDataStruct->IsChildOf(FGameplayAbilityTargetData_LocationInfo::StaticStruct()))
		{
			const FGameplayAbilityTargetData_LocationInfo* LocationData = static_cast<const FGameplayAbilityTargetData_LocationInfo*>(Data);
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
					if (ActorPtr.IsValid()
						&& IsTargetAllowed(ActorInfo, Config, ActorPtr.Get())
						&& IsWithinRange(ActorInfo, Config, ActorPtr->GetActorLocation()))
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

	OutTargets.Sort([&OutTargetLocation](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), OutTargetLocation)
			< FVector::DistSquared(B.GetActorLocation(), OutTargetLocation);
	});

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
	const float ConfiguredRadius = Config.Shape == EAeyerjiAbilityTargetShape::OwnerCone
		? Config.MaxRange
		: Config.Radius;
	const float Radius = FMath::IsFinite(ConfiguredRadius) ? FMath::Max(0.f, ConfiguredRadius) : 0.f;
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
	const float HalfAngle = FMath::IsFinite(Config.ArcAngleDegrees)
		? FMath::Clamp(Config.ArcAngleDegrees * 0.5f, 0.f, 180.f)
		: 0.f;

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
	if (!IsValid(Target) || !IsValid(Avatar) || Target->GetWorld() != Avatar->GetWorld())
	{
		return false;
	}
	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, true))
	{
		if (TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		{
			return false;
		}
	}
	if (Target->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
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
	if (!ActorInfo.AvatarActor.IsValid() || TargetLocation.ContainsNaN())
	{
		return false;
	}
	if (!FMath::IsFinite(Config.MaxRange))
	{
		return false;
	}
	if (Config.MaxRange <= KINDA_SMALL_NUMBER)
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
	return FMath::IsFinite(Value) ? Value : 0.f;
}

void UGA_AeyerjiTargetedEffectBase::ApplyResolvedConfigEffects(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, const TArray<AActor*>& Targets) const
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
		if (!IsValid(Target) || !EffectClass)
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
		const float SafeLevel = FMath::IsFinite(Level) ? FMath::Max(0.01f, Level) : 1.f;
		FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(EffectClass, SafeLevel, Context);
		if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: failed to create spec for %s on %s."),
				*GetNameSafe(EffectClass),
				*GetNameSafe(Target));
			return FActiveGameplayEffectHandle();
		}

		if (SetByCallerTag.IsValid() && FMath::IsFinite(Magnitude))
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
	TSubclassOf<UGameplayEffect> DamageClass = UGE_DamagePhysical::StaticClass();
	if (!Config.Damage.GameplayEffectClass.IsNull())
	{
		DamageClass = Config.Damage.GameplayEffectClass.LoadSynchronous();
	}
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
		TSubclassOf<UGameplayEffect> EffectClass = Effect.GameplayEffectClass.LoadSynchronous();
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

			if (!AppliedHandle.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: failed to apply additional effect Class=%s Target=%s."),
					*GetNameSafe(EffectClass),
					*GetNameSafe(Target));
			}
		}
	}
}
