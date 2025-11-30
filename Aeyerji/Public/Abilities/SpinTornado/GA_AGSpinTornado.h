// GameplayAbility shell for Astral Guardian's spin tornado finisher.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "DA_AGSpinTornado.h"
#include "GA_AGSpinTornado.generated.h"

UCLASS()
class AEYERJI_API UGA_AGSpinTornado : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_AGSpinTornado();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Config")
	TObjectPtr<UDA_AGSpinTornado> SpinConfig = nullptr;
};

