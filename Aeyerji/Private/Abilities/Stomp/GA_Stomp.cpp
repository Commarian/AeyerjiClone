#include "Abilities/Stomp/GA_Stomp.h"

#include "GameplayTagContainer.h"

namespace AeyerjiStompTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Stomp"));
		return Tag;
	}
}

UGA_Stomp::UGA_Stomp()
{
	AbilityTag = AeyerjiStompTags::AbilityTag();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AbilityTag);
	SetAssetTags(AssetTags);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
