// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiRunTypes.h"
#include "Blueprint/UserWidget.h"
#include "W_EndRunScreen.generated.h"

class UButton;
class USlider;
class UTextBlock;
class UWidget;

/**
 * Native end-of-run screen contract. Designers can subclass this in UMG and bind the optional widgets by name.
 */
UCLASS()
class AEYERJI_API UW_EndRunScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Populates the end-of-run summary and refreshes the retry difficulty controls. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void ApplyRunResults(const FAeyerjiRunResults& InResults);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleRetryDifficultyChanged(float NewValue);

	UFUNCTION()
	void HandleRetryClicked();

	UFUNCTION()
	void HandleReturnToMenuClicked();

	void RefreshDisplayedValues();
	void RefreshDifficultyControls();

protected:
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* ResultTitleText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* ResultDetailText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* UnitsKilledValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* UnitsKilledLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* UnitsKilledRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* UnitsGoalValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* UnitsGoalLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* UnitsGoalRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* TimeElapsedValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* TimeElapsedLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* TimeElapsedRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* TimeRemainingValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* TimeRemainingLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* TimeRemainingRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* SpeedBonusValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* SpeedBonusLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* SpeedBonusRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* BestTimeValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* BestTimeLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* BestTimeRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* DifficultyValueText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* DifficultyLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* DifficultyRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* FailureReasonText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* FailureReasonLabelText = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UWidget* FailureReasonRow = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	USlider* RetryDifficultySlider = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UButton* RetryButton = nullptr;

	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
	UButton* ReturnToMenuButton = nullptr;

private:
	FAeyerjiRunResults CachedResults;
	bool bUpdatingDifficultyControls = false;
};
