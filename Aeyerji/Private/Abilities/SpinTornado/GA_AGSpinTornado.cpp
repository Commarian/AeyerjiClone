#include "Abilities/SpinTornado/GA_AGSpinTornado.h"

namespace AGSpinTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.SpinTornado"));
		return Tag;
	}
}

UGA_AGSpinTornado::UGA_AGSpinTornado()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AGSpinTags::AbilityTag());
	SetAssetTags(AssetTags);
	AbilityTag = AGSpinTags::AbilityTag();
}
