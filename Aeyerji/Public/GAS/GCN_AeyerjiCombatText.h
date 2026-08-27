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

	/** Presentation category used to format and filter this cue's combat text. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text")
	EAeyerjiCombatTextResultType CombatTextType = EAeyerjiCombatTextResultType::Damage;

	/** Enables local floating text in addition to the ordinary burst presentation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text")
	bool bSpawnCombatText = true;

	/** Per-cue scale multiplier applied after the result-type scale. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Combat Text", meta=(ClampMin="0.01"))
	float TextScaleMultiplier = 1.f;

	/** Also invokes the target actor's optional combat-cue presentation profile. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Target Presentation")
	bool bPlayTargetCombatCueProfile = true;

	virtual bool OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) const override;
};
