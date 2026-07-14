#include "Abilities/Potions/GA_HealPotion.h"

#include "AbilitySystemComponent.h"
#include "AeyerjiGameplayTags.h"
#include "Abilities/Potions/DA_Potions.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"
#include "GAS/GE_AeyerjiHealInstant.h"

UGA_HealPotion::UGA_HealPotion()
{
	AbilityTag = AeyerjiTags::Ability_Potion_Heal;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AeyerjiTags::Ability_Potion_Heal);
	SetAssetTags(AssetTags);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UGA_HealPotion::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;

	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (IsOwnerDead(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const float MaxHP = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	const float RawHealFraction = PotionData
		? FMath::Max(0.f, PotionData->Tunables.HealPercentageOfMaxHP)
		: FMath::Max(0.f, HealPercentageOfMaxHP);
	const float HealFraction = RawHealFraction > 1.f ? RawHealFraction * 0.01f : RawHealFraction;
	const float HealAmount = FMath::Max(0.f, MaxHP * HealFraction);

	if (HealAmount > KINDA_SMALL_NUMBER)
	{
		FGameplayEffectSpecHandle HealSpec = MakeOutgoingGameplayEffectSpec(
			UGE_AeyerjiHealInstant::StaticClass(),
			GetAbilityLevel(Handle, ActorInfo));

		if (HealSpec.IsValid() && HealSpec.Data.IsValid())
		{
			HealSpec.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_Heal_Instant, HealAmount);
			ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, HealSpec);
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_HealPotion::ApplyCooldown(
	const FGameplayAbilitySpecHandle Handle,
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

	const float CooldownSeconds = PotionData
		? FMath::Max(0.f, PotionData->EvaluateCost(ActorInfo->AbilitySystemComponent.Get()).Cooldown)
		: FMath::Max(0.f, FallbackCooldownSeconds);
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

const FGameplayTagContainer* UGA_HealPotion::GetCooldownTags() const
{
	PotionCooldownTags.Reset();

	if (const FGameplayTagContainer* ParentTags = Super::GetCooldownTags())
	{
		PotionCooldownTags.AppendTags(*ParentTags);
	}

	PotionCooldownTags.AddTag(AeyerjiTags::Cooldown_Potion);
	return &PotionCooldownTags;
}
