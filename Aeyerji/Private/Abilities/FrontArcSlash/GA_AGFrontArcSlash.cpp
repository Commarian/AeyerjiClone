#include "Abilities/FrontArcSlash/GA_AGFrontArcSlash.h"

#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"

// Local tags for clarity; swap to your project-wide tags when you wire GEs/GCs.
namespace AGFrontArcTags
{
	const FGameplayTag AbilityTag  = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.FrontArcSlash"));
	const FGameplayTag ManaCostTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cost.Mana"));
	const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cooldown.Seconds"));
}

UGA_AGFrontArcSlash::UGA_AGFrontArcSlash()
{
	// Tag the asset so GAS knows what this ability is for filtering, UI, etc.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGFrontArcTags::AbilityTag);
	SetAssetTags(AssetTags);

	// Cooldown tag helps GAS apply shared cooldown effects if you add them later.
	ActivationOwnedTags.AddTag(AGFrontArcTags::CooldownTag);
}

void UGA_AGFrontArcSlash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                          const FGameplayAbilityActorInfo* ActorInfo,
                                          const FGameplayAbilityActivationInfo ActivationInfo,
                                          const FGameplayEventData* TriggerEventData)
{
	// Pay mana + start cooldown (SetByCaller pulls values from FrontArcConfig via UGA_AeyerjiBase).
	if (!TryCommitAbilityInternal(Handle, ActorInfo, ActivationInfo, /*bEndAbilityOnFailure*/true))
	{
		// Nothing fancy: if we can't pay the cost, bail early.
		return;
	}

	// Actual hit logic (traces / apply damage + ailment) will live here.
	// I’m keeping it empty so we can agree on animation/trace shapes before coding the specifics.

	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/true, /*bWasCancelled*/false);
}

