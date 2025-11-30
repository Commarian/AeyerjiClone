#include "Abilities/RoarCone/GA_AGRoarCone.h"

#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"

namespace AGRoarTags
{
	const FGameplayTag AbilityTag  = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.RoarCone"));
	const FGameplayTag ManaCostTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cost.Mana"));
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cooldown.Seconds"));
}

UGA_AGRoarCone::UGA_AGRoarCone()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGRoarTags::AbilityTag);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(AGRoarTags::CooldownTag);
}

void UGA_AGRoarCone::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo,
                                     const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
	// Commit handles mana/cooldown and lets GA_AeyerjiBase inject SetByCaller values from RoarConfig.
	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, /*bEndAbilityOnFailure*/true))
	{
		return;
	}

	// Placeholder: this is where the cone trace and Bleed application will live.
	// Leaving it empty keeps things safe until we agree on visuals / targeting rules.

	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/true, /*bWasCancelled*/false);
}

