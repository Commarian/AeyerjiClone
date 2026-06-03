#include "Abilities/GA_AeyerjiTargetedEffectBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AeyerjiGameplayTags.h"
#include "CharacterStatsLibrary.h"
#include "GAS/GE_DamagePhysical.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const FAeyerjiAbilityTableRow* Row = GetAbilityTuningRow(ActorInfo->AbilitySystemComponent.Get());
	if (!Row)
	{
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
	ResolveTargets(*ActorInfo, TriggerEventData, *Row, Targets, TargetLocation);

	if (!IsWithinRange(*ActorInfo, *Row, TargetLocation))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, true))
	{
		return;
	}

	ApplyRowEffects(Handle, *ActorInfo, ActivationInfo, *Row, Targets);
	BP_OnTargetedAbilityApplied(Targets, TargetLocation);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
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

void UGA_AeyerjiTargetedEffectBase::ResolveTargets(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, const FAeyerjiAbilityTableRow& Row, TArray<AActor*>& OutTargets, FVector& OutTargetLocation) const
{
	OutTargets.Reset();
	ResolveTargetLocation(ActorInfo, TriggerEventData, OutTargetLocation);

	if (Row.TargetTeam == EAeyerjiAbilityTargetTeam::Self)
	{
		if (ActorInfo.AvatarActor.IsValid())
		{
			OutTargets.Add(ActorInfo.AvatarActor.Get());
		}
		return;
	}

	if (Row.Shape == EAeyerjiAbilityTargetShape::SingleActor && TriggerEventData)
	{
		for (int32 DataIdx = 0; DataIdx < TriggerEventData->TargetData.Num(); ++DataIdx)
		{
			if (const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(DataIdx))
			{
				for (const TWeakObjectPtr<AActor>& ActorPtr : Data->GetActors())
				{
					if (ActorPtr.IsValid() && IsTargetAllowed(ActorInfo, Row, ActorPtr.Get()))
					{
						OutTargets.AddUnique(ActorPtr.Get());
					}
				}
			}
		}
	}
	else
	{
		GatherShapeTargets(ActorInfo, Row, OutTargetLocation, OutTargets);
	}

	if (Row.MaxTargets > 0 && OutTargets.Num() > Row.MaxTargets)
	{
		OutTargets.SetNum(Row.MaxTargets);
	}
}

void UGA_AeyerjiTargetedEffectBase::GatherShapeTargets(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, const FVector& TargetLocation, TArray<AActor*>& OutTargets) const
{
	AActor* Avatar = ActorInfo.AvatarActor.Get();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FVector Origin = Row.Shape == EAeyerjiAbilityTargetShape::GroundRadius ? TargetLocation : Avatar->GetActorLocation();
	const float Radius = FMath::Max(Row.Radius, Row.Shape == EAeyerjiAbilityTargetShape::OwnerCone ? Row.MaxRange : 0.f);
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
	const float HalfAngle = FMath::Clamp(Row.ArcAngleDegrees * 0.5f, 0.f, 180.f);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!IsTargetAllowed(ActorInfo, Row, Candidate))
		{
			continue;
		}

		if (Row.Shape == EAeyerjiAbilityTargetShape::OwnerCone)
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

bool UGA_AeyerjiTargetedEffectBase::IsTargetAllowed(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, AActor* Target) const
{
	AActor* Avatar = ActorInfo.AvatarActor.Get();
	if (!Target || !Avatar)
	{
		return false;
	}

	if (Target == Avatar)
	{
		return Row.TargetTeam == EAeyerjiAbilityTargetTeam::Self || Row.TargetTeam == EAeyerjiAbilityTargetTeam::Friendly || Row.TargetTeam == EAeyerjiAbilityTargetTeam::Any;
	}

	switch (Row.TargetTeam)
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

bool UGA_AeyerjiTargetedEffectBase::IsWithinRange(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, const FVector& TargetLocation) const
{
	if (!ActorInfo.AvatarActor.IsValid() || Row.MaxRange <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	return FVector::DistSquared(ActorInfo.AvatarActor->GetActorLocation(), TargetLocation) <= FMath::Square(Row.MaxRange + KINDA_SMALL_NUMBER);
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

void UGA_AeyerjiTargetedEffectBase::ApplyRowEffects(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo& ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FAeyerjiAbilityTableRow& Row, const TArray<AActor*>& Targets) const
{
	UAbilitySystemComponent* SourceASC = ActorInfo.AbilitySystemComponent.Get();
	if (!SourceASC)
	{
		return;
	}

	auto ApplySpecToTarget = [&](AActor* Target, TSubclassOf<UGameplayEffect> EffectClass, float Level, const FGameplayTag& SetByCallerTag, float Magnitude, const FGameplayTag& DamageTypeTag) -> FActiveGameplayEffectHandle
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

		return TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	};

	const float DamageMagnitude = EvaluateMagnitude(ActorInfo, Row.Damage);
	TSubclassOf<UGameplayEffect> DamageClass = Row.Damage.GameplayEffectClass.IsNull()
		? UGE_DamagePhysical::StaticClass()
		: Row.Damage.GameplayEffectClass.LoadSynchronous();
	const FGameplayTag DamageSetByCaller = Row.Damage.SetByCallerTag.IsValid() ? Row.Damage.SetByCallerTag : AeyerjiTags::SBC_Damage_Instant;
	if (DamageMagnitude > KINDA_SMALL_NUMBER && DamageClass)
	{
		for (AActor* Target : Targets)
		{
			ApplySpecToTarget(Target, DamageClass, GetAbilityLevel(Handle, &ActorInfo), DamageSetByCaller, DamageMagnitude, Row.Damage.DamageTypeTag);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: Ability=%s Targets=%d AdditionalEffects=%d"),
		*Row.AbilityTag.ToString(),
		Targets.Num(),
		Row.AdditionalEffects.Num());

	for (const FAeyerjiAbilityAppliedEffect& Effect : Row.AdditionalEffects)
	{
		TSubclassOf<UGameplayEffect> EffectClass = Effect.GameplayEffectClass.LoadSynchronous();
		if (!EffectClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: Additional effect failed to load. SetByCaller=%s Magnitude=%.2f"),
				*Effect.SetByCallerTag.ToString(),
				Effect.Magnitude);
			continue;
		}

		for (AActor* Target : Targets)
		{
			FActiveGameplayEffectHandle AppliedHandle =
				ApplySpecToTarget(Target, EffectClass, Effect.EffectLevel, Effect.SetByCallerTag, Effect.Magnitude, FGameplayTag());

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

			UE_LOG(LogTemp, Warning, TEXT("TargetedEffect: Applied additional effect Class=%s Target=%s HandleValid=%d SetByCaller=%s Magnitude=%.2f StunTagCount=%d"),
				*GetNameSafe(EffectClass),
				*GetNameSafe(Target),
				AppliedHandle.IsValid() ? 1 : 0,
				*Effect.SetByCallerTag.ToString(),
				Effect.Magnitude,
				StunTagCount);
		}
	}
}
