#include "GAS/GE_Stun.h"

#include "AeyerjiGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UGE_Stun::UGE_Stun(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat StunDuration;
	StunDuration.DataTag = AeyerjiTags::SBC_Stun_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(StunDuration);

	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(AeyerjiTags::State_CrowdControl_Stunned);
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(
			this,
			TEXT("StunGrantedTagsComponent"));
	if (TargetTagsComponent)
	{
		GEComponents.Add(TargetTagsComponent);
		TargetTagsComponent->SetAndApplyTargetTagChanges(GrantedTags);
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	InheritableOwnedTagsContainer.AddTag(AeyerjiTags::State_CrowdControl_Stunned);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
