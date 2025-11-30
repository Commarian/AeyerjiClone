// GameplayAbility shell for Astral Guardian's front-arc light slash.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "DA_AGFrontArcSlash.h"
#include "GA_AGFrontArcSlash.generated.h"

/**
 * Light frontal arc swing. Think of it as the "generator" light filler.
 * Real hit logic will live in ActivateAbility; right now we only wire cost/cooldown + config.
 */
UCLASS()
class AEYERJI_API UGA_AGFrontArcSlash : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_AGFrontArcSlash();

protected:
	// Main entry: commit cost, then later we'll place the trace/damage logic.
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

public:
	/** Designer-tunable data asset. Set this on the CDO so cost/cooldown pull through via SetByCaller. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Config")
	TObjectPtr<UDA_AGFrontArcSlash> FrontArcConfig = nullptr;
};

