// GameplayAbility shell for Astral Guardian's roar cone (applies Bleed stacks, no direct damage).

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "DA_AGRoarCone.h"
#include "GA_AGRoarCone.generated.h"

UCLASS()
class AEYERJI_API UGA_AGRoarCone : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_AGRoarCone();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone|Config")
	TObjectPtr<UDA_AGRoarCone> RoarConfig = nullptr;
};

