#include "Abilities/Blink/GABlink.h"

#include "AbilitySystemComponent.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"

namespace BlinkTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Blink"));
		return Tag;
	}

	const FGameplayTag& CooldownTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Blink"));
		return Tag;
	}

	const FGameplayTag& GC_BlinkOut()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.Out"));
		return Tag;
	}

	const FGameplayTag& GC_BlinkIn()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Blink.In"));
		return Tag;
	}
}

namespace
{
	bool ResolveBlinkTargetLocation(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* TriggerEventData, FVector& OutTargetLocation)
	{
		if (TriggerEventData)
		{
			if (const AActor* ExplicitTarget = TriggerEventData->Target.Get())
			{
				OutTargetLocation = ExplicitTarget->GetActorLocation();
				return true;
			}

			for (int32 DataIndex = 0; DataIndex < TriggerEventData->TargetData.Num(); ++DataIndex)
			{
				const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(DataIndex);
				if (!Data)
				{
					continue;
				}

				if (const FHitResult* HitResult = Data->GetHitResult())
				{
					OutTargetLocation = HitResult->ImpactPoint;
					return true;
				}

				const TArray<TWeakObjectPtr<AActor>> TargetActors = Data->GetActors();
				if (!TargetActors.IsEmpty() && TargetActors[0].IsValid())
				{
					OutTargetLocation = TargetActors[0]->GetActorLocation();
					return true;
				}

				if (Data->GetScriptStruct() == FGameplayAbilityTargetData_LocationInfo::StaticStruct())
				{
					const FGameplayAbilityTargetData_LocationInfo* LocationData = static_cast<const FGameplayAbilityTargetData_LocationInfo*>(Data);
					OutTargetLocation = LocationData->GetEndPoint();
					return true;
				}
			}
		}

		if (ActorInfo && ActorInfo->AvatarActor.IsValid())
		{
			OutTargetLocation = ActorInfo->AvatarActor->GetActorLocation();
			return true;
		}

		OutTargetLocation = FVector::ZeroVector;
		return false;
	}
}

UGABlink::UGABlink()
{
	AbilityTag = BlinkTags::AbilityTag();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AbilityTag);
	SetAssetTags(AssetTags);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = AeyerjiTags::Event_External_Target;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);

	CooldownTags.AddTag(BlinkTags::CooldownTag());
	BlinkOutCue = BlinkTags::GC_BlinkOut();
	BlinkInCue = BlinkTags::GC_BlinkIn();

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

float UGABlink::GetMaxBlinkRange(const UAbilitySystemComponent* ASC) const
{
	float Range = MaxBlinkDistance;

	FAeyerjiAbilityResolvedConfig Config;
	if (GetAbilityResolvedConfig(ASC, ResolveAbilityRank(ASC), Config))
	{
		Range = FMath::Max(Range, FMath::Max(Config.MaxRange, Config.PreviewRange));
	}

	if (ASC)
	{
		if (const UAttributeSet_Ranges* RangeSet = ASC->GetSet<UAttributeSet_Ranges>())
		{
			Range = FMath::Max(Range, RangeSet->GetBlinkRange());
		}
	}

	return FMath::Max(0.f, Range);
}

void UGABlink::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return;
	}

	FAeyerjiAbilityResolvedConfig Config;
	if (GetCooldownGameplayEffect() || GetAbilityResolvedConfig(ActorInfo->AbilitySystemComponent.Get(), ResolveAbilityRank(Handle, ActorInfo), Config))
	{
		Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		return;
	}

	const float CooldownSeconds = FMath::Max(0.f, FallbackCooldownSeconds);
	if (CooldownSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(UGE_AeyerjiAbilityCooldown::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	ApplyAbilitySetByCallerToSpec(SpecHandle, 0.f, CooldownSeconds);
	ApplyResolvedCooldownTagsToSpec(SpecHandle);

	if (SpecHandle.IsValid())
	{
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
	}
}

const FGameplayTagContainer* UGABlink::GetCooldownTags() const
{
	return CooldownTags.IsEmpty() ? Super::GetCooldownTags() : &CooldownTags;
}

void UGABlink::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || IsOwnerDead(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AAeyerjiCharacter* Avatar = Cast<AAeyerjiCharacter>(ActorInfo->AvatarActor.Get());
	if (!Avatar)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float MaxRange = GetMaxBlinkRange(ActorInfo->AbilitySystemComponent.Get());
	const FVector StartLocation = Avatar->GetActorLocation();
	FVector TargetLocation = StartLocation + Avatar->GetActorForwardVector() * MaxRange;
	ResolveBlinkTargetLocation(ActorInfo, TriggerEventData, TargetLocation);

	FVector TravelVector = TargetLocation - StartLocation;
	TravelVector.Z = 0.f;
	if (TravelVector.IsNearlyZero())
	{
		TravelVector = Avatar->GetActorForwardVector() * MaxRange;
		TravelVector.Z = 0.f;
	}

	const FVector BlinkDirection = TravelVector.GetSafeNormal();
	const float BlinkDistance = FMath::Min(MaxRange, TravelVector.Size());
	const FVector DesiredLocation = StartLocation + BlinkDirection * BlinkDistance;
	const FRotator DesiredRotation = BlinkDirection.IsNearlyZero() ? Avatar->GetActorRotation() : BlinkDirection.Rotation();

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		ASC->ExecuteGameplayCue(BlinkOutCue);
	}

	FVector FinalLocation = StartLocation;
	if (!TeleportCharacterSafely(Avatar, DesiredLocation, DesiredRotation, 250.f, 4.f, FinalLocation))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		ASC->ExecuteGameplayCue(BlinkInCue);
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
