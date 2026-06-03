#include "Abilities/FrontArcSlash/GA_AGFrontArcSlash.h"

#include "NativeGameplayTags.h"

namespace AGFrontArcTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.FrontArcSlash"));
		return Tag;
	}
}

UGA_AGFrontArcSlash::UGA_AGFrontArcSlash()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGFrontArcTags::AbilityTag());
	SetAssetTags(AssetTags);
	AbilityTag = AGFrontArcTags::AbilityTag();
}
