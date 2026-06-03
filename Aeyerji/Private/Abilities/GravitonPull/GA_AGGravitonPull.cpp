#include "Abilities/GravitonPull/GA_AGGravitonPull.h"

#include "AeyerjiGameplayTags.h"
#include "NativeGameplayTags.h"

namespace GravitonTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"));
		return Tag;
	}
}

UGA_AGGravitonPull::UGA_AGGravitonPull()
{
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GravitonTags::AbilityTag());
	SetAssetTags(AssetTags);

	AbilityTag = GravitonTags::AbilityTag();
	DefaultDamageTypeTag = AeyerjiTags::DamageType_Physical;
}
