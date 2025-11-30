#include "Abilities/SpinTornado/GA_AGSpinTornado.h"

#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"

namespace AGSpinTags
{
	const FGameplayTag AbilityTag  = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.SpinTornado"));
	const FGameplayTag ManaCostTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cost.Mana"));
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cooldown.Seconds"));
}

UGA_AGSpinTornado::UGA_AGSpinTornado()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGSpinTags::AbilityTag);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(AGSpinTags::CooldownTag);
}

void UGA_AGSpinTornado::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
	// Pay costs and start cooldowns up front. Base class will read SpinConfig to set SBC magnitudes.
	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, /*bEndAbilityOnFailure*/true))
	{
		return;
	}

	// Here we’ll eventually drive montage playback, spin ticks, and the final shockwave.
	// Keeping it empty avoids accidental damage until the timing/FX are locked in.

	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/true, /*bWasCancelled*/false);
}

