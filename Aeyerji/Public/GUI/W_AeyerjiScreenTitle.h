// Copyright (c) 2025 Aeyerji.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "W_AeyerjiScreenTitle.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UOverlay;
class URichTextBlock;

/**
 * Drop-in screen title with the approved living atmosphere behind readable text.
 *
 * Reuse path for submenus: create a Widget Blueprint reparented to this class
 * (no Designer work needed, the tree is built natively), set TitleStringTableKey,
 * then place that WBP inside any menu page. The effect layer never blocks input.
 * Presentation only; all motion is material Time plus the owning page's entrance.
 */
UCLASS(Blueprintable)
class AEYERJI_API UW_AeyerjiScreenTitle : public UUserWidget
{
	GENERATED_BODY()

public:
	UW_AeyerjiScreenTitle(const FObjectInitializer& ObjectInitializer);

	/** String-table key whose SourceString already carries its RichText tag (e.g. <LandingTitle>...</>). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Title")
	FName TitleStringTableKey = TEXT("Frontend_MainMenu_Title");

	/** Effect brightness multiplier forwarded to the atmosphere material instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Title", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float Intensity = 1.f;

	/** Effect clock speed forwarded to the atmosphere material instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Title", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float MotionSpeed = 1.f;

	/** Render scale of the effect layer so filaments/motes extend past the glyphs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Title")
	FVector2D AtmosphereScale = FVector2D(1.12f, 1.60f);

	/** Optional per-title atmosphere material. Null loads the shared M_UI_LandingTitleAtmosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Title")
	TSoftObjectPtr<UMaterialInterface> AtmosphereMaterialOverride;

	/** Switches the title to another string-table key (clears any direct-text override). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Title")
	void SetTitleKey(FName InKey);

	/** Shows literal text instead of a string-table key. Prefer keys for localizable titles. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Title")
	void SetTitleText(const FText& InText);

	/** Retunes effect brightness/speed at runtime without rebuilding the widget. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Title")
	void SetEffectParams(float InIntensity, float InMotionSpeed);

protected:
	virtual void NativeOnInitialized() override;
	virtual void SynchronizeProperties() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UOverlay> RootOverlay = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UImage> AtmosphereImage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<URichTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LastAtmosphereBase = nullptr;

	bool bHasDirectText = false;
	FText DirectText;

	/** Builds the overlay/effect/title tree so a reparented WBP needs no Designer graph. */
	void EnsureWidgetTree();
	void ApplyTitle();
	void ApplyEffect();
};
