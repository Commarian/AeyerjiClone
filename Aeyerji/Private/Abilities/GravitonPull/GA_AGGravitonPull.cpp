#include "Abilities/GravitonPull/GA_AGGravitonPull.h"

#include "NativeGameplayTags.h"

namespace AeyerjiGravitonPullTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"));
		return Tag;
	}
}

UGA_AGGravitonPull::UGA_AGGravitonPull()
{
	AbilityTag = AeyerjiGravitonPullTags::AbilityTag();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AbilityTag);
	SetAssetTags(AssetTags);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
