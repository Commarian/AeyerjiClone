#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Burst.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "GCN_AeyerjiCombatText.generated.h"

/** Burst GameplayCue base that also spawns local floating combat text. */
UCLASS(Blueprintable, Category="GameplayCueNotify", meta=(DisplayName="GCN Aeyerji Combat Text"))
class AEYERJI_API UGCN_AeyerjiCombatText : public UGameplayCueNotify_Burst
{
	GENERATED_BODY()

public:
	UGCN_AeyerjiCombatText();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text")
	EAeyerjiCombatTextResultType CombatTextType = EAeyerjiCombatTextResultType::Damage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text")
	bool bSpawnCombatText = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text", meta=(ClampMin="0.01"))
	float TextScaleMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Target Presentation")
	bool bPlayTargetCombatCueProfile = true;

	virtual bool OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) const override;
};
