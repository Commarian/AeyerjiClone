// GCN_Blink_Out.h
#pragma once
#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "NiagaraFunctionLibrary.h"
#include "GCN_Blink_Out.generated.h"

UCLASS()
class AEYERJI_API UGCN_Blink_Out : public UGameplayCueNotify_Static
{
	GENERATED_BODY()
public:
	/** Niagara system to spawn when the cue executes */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|FX")
	UNiagaraSystem* NiagaraTemplate;

	/** Sound to play with the dissolve flash */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|FX")
	USoundBase* DissolveSFX;

	virtual bool OnExecute_Implementation(
		AActor* Target,
		const FGameplayCueParameters& Parameters) const override;
};
