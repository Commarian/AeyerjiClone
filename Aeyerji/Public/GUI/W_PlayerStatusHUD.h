// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.h"
#include "GUI/W_AeyerjiStatusBar.h"
#include "W_PlayerStatusHUD.generated.h"

class UW_AeyerjiTransientMessage;

/**
 * Native player HUD contract used by the controller-owned objective presentation flow.
 * Blueprint subclasses should keep gameplay discovery out of the widget and render only from ApplyObjectiveState().
 */
UCLASS()
class AEYERJI_API UW_PlayerStatusHUD : public UW_AeyerjiStatusBar
{
	GENERATED_BODY()

public:
	/** Routes controller-owned transient feedback through this HUD's existing toast animation. */
	void PresentPopupMessage(const FText& Message, float Duration);

	/** Local gameplay failures use unobtrusive corner text, separate from reward toasts. */
	void PresentGameplayWarning(const FText& Message, float Duration);

	/** Caches and forwards the latest replicated objective snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Objective")
	void ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState);

	/** Caches and forwards the latest replicated survival-round snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Returns the last snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Objective")
	const FAeyerjiObjectiveState& GetCachedObjectiveState() const { return CachedObjectiveState; }

	/** Returns the last survival-round snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Survival")
	const FAeyerjiSurvivalRoundState& GetCachedSurvivalRoundState() const { return CachedSurvivalRoundState; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandleLevelAdvanced(int32 OldLevel, int32 NewLevel) override;

	/**
	 * Blueprint presentation hook for a dedicated level-up banner, glow, or animation.
	 * The C++ call path is UW_AeyerjiStatusBar::OnLevelChanged() -> HandleLevelAdvanced(),
	 * which always routes the same formatted copy through the shared toast first and then
	 * fires this event so Blueprint adds visuals without duplicating detection or formatting.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Progression", meta=(DisplayName="Handle Level Advanced"))
	void BP_HandleLevelAdvanced(int32 OldLevel, int32 NewLevel, const FText& LevelUpText);

	/** Blueprint presentation hook fired whenever ApplyObjectiveState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Objective", meta=(DisplayName="Handle Objective State Applied"))
	void BP_HandleObjectiveStateApplied(const FAeyerjiObjectiveState& InObjectiveState);

	/** Blueprint presentation hook fired whenever ApplySurvivalRoundState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Handle Survival Round State Applied"))
	void BP_HandleSurvivalRoundStateApplied(const FAeyerjiSurvivalRoundState& InSurvivalState);

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GameplayWarningText = nullptr;
	float GameplayWarningRemaining = 0.f;

	void TickLevelUpEffect(float DeltaSeconds);
	float LevelUpElapsed = -1.f;
	FWidgetTransform LevelRestTransform;
	FVector2D LevelRestShadowOffset = FVector2D::ZeroVector;
	FLinearColor LevelRestShadowColor = FLinearColor::Transparent;
	void TryBindToOwningAttributes();
	TWeakObjectPtr<UAbilitySystemComponent> BoundStatusASC;

	UPROPERTY(Transient)
	TObjectPtr<UW_AeyerjiTransientMessage> ToastMessageWidget = nullptr;

	/** Cached objective snapshot for immediate refreshes after widget reattachment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Objective", meta=(AllowPrivateAccess="true"))
	FAeyerjiObjectiveState CachedObjectiveState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Survival", meta=(AllowPrivateAccess="true"))
	FAeyerjiSurvivalRoundState CachedSurvivalRoundState;
};
