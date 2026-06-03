#include "Abilities/RoarCone/GA_AGRoarCone.h"

#include "NativeGameplayTags.h"

namespace AGRoarTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.RoarCone"));
		return Tag;
	}
}

UGA_AGRoarCone::UGA_AGRoarCone()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGRoarTags::AbilityTag());
	SetAssetTags(AssetTags);
	AbilityTag = AGRoarTags::AbilityTag();
}
