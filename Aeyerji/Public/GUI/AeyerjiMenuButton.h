// Copyright (c) 2025 Aeyerji.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"

#include "AeyerjiMenuButton.generated.h"

class UMaterialInterface;

/**
 * Menu button that self-applies the approved living-menu style.
 *
 * Drop-in replacement for UButton on any menu or submenu: set an existing
 * Designer button's class to this (or place a new one) and it restyles itself
 * in the Designer preview and at runtime. Designer children (MenuLabel text)
 * are preserved; only the button brushes change. Presentation only, no networking.
 */
UCLASS()
class AEYERJI_API UAeyerjiMenuButton : public UButton
{
	GENERATED_BODY()

public:
	/** When false, the button keeps whatever style the Designer authored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Style")
	bool bAutoApplyLivingStyle = true;

	/** When true the hovered state uses the animated violet/cyan edge-light material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Style", meta = (EditCondition = "bAutoApplyLivingStyle"))
	bool bUseAnimatedHover = true;

	/** Optional per-button hover material. Null loads the shared M_UI_MenuHoverAtmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Style", meta = (EditCondition = "bUseAnimatedHover"))
	TSoftObjectPtr<UMaterialInterface> HoverMaterialOverride;

	/** Re-applies the living style on demand (also called automatically on construct). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	void RefreshLivingStyle();

protected:
	// UWidget (unlike UUserWidget) has no NativeConstruct; SynchronizeProperties
	// runs in both the Designer preview and at runtime.
	virtual void SynchronizeProperties() override;

private:
	void ApplyLivingStyle() const;

	/** Re-entrancy guard: SetStyle must not recurse back through SynchronizeProperties. */
	mutable bool bApplyingLivingStyle = false;
};
