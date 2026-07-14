#include "GAS/GE_Stagger.h"

#include "AeyerjiGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UGE_Stagger::UGE_Stagger(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat StaggerDuration;
	StaggerDuration.DataTag = AeyerjiTags::SBC_Stagger_Duration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(StaggerDuration);

	FInheritedTagContainer GrantedTags;
	GrantedTags.AddTag(AeyerjiTags::State_CrowdControl_Staggered);
	UTargetTagsGameplayEffectComponent* TargetTagsComponent =
		ObjectInitializer.CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(this, TEXT("StaggerGrantedTagsComponent"));
	if (TargetTagsComponent)
	{
		GEComponents.Add(TargetTagsComponent);
		TargetTagsComponent->SetAndApplyTargetTagChanges(GrantedTags);
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	InheritableOwnedTagsContainer.AddTag(AeyerjiTags::State_CrowdControl_Staggered);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
