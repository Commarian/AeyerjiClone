#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "W_AeyerjiCombatText.generated.h"

class UTextBlock;

/** Base widget for short-lived floating combat text. */
UCLASS(Blueprintable)
class AEYERJI_API UW_AeyerjiCombatText : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Applies text payload data before the widget starts animating. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Combat Text")
	void ApplyCombatText(
		const FText& InDisplayText,
		FLinearColor InTextColor,
		float InTextScale,
		EAeyerjiCombatTextResultType InResultType,
		float InMagnitude);

	/** Called by the manager each tick with 0..1 normalized age. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Combat Text")
	void NotifyCombatTextTick(float NormalizedAge);

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Combat Text")
	void BP_OnCombatTextAssigned();

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Combat Text")
	void BP_OnCombatTextTick(float NormalizedAge);

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat Text")
	FText DisplayText;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat Text")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat Text")
	float TextScale = 1.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat Text")
	EAeyerjiCombatTextResultType ResultType = EAeyerjiCombatTextResultType::Damage;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat Text")
	float Magnitude = 0.f;

protected:
	virtual void NativePreConstruct() override;

private:
	void EnsureNativeTextBlock();
	void RefreshNativeTextBlock();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NativeTextBlock = nullptr;
};
