// Copyright (c) 2025 Aeyerji.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AeyerjiUIStyleLibrary.generated.h"

class UButton;
class UBorder;
class UDataTable;
class UFont;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProgressBar;
class URichTextBlock;
class USlider;
class UTextBlock;

/**
 * Single source of truth for the approved living-menu look.
 *
 * Landing (W_MainMenu) styling was originally applied by one-off editor scripts.
 * New widgets and submenus should call these helpers instead of copying brush
 * values, so the violet/cyan direction stays consistent and tunable in one place.
 * All functions are client-side presentation only; no gameplay or networked state.
 */
UCLASS()
class AEYERJI_API UAeyerjiUIStyleLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Shared motion rhythm for new UI. Existing approved landing variants retain their timing.
	static constexpr float MessageEntranceSeconds = 0.28f;
	static constexpr float MessageExitSeconds = 0.20f;
	static constexpr float PanelEntranceSeconds = 0.42f;
	static constexpr float LevelUpPulseSeconds = 1.20f;
	static float EasePresentation(float NormalizedTime);
	static void ApplyLevelUpAccent(UTextBlock* LevelLabel, UProgressBar* ExperienceBar, float Strength);

	/** Animated edge-light material used by hovered menu buttons. Null if the asset is missing. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UMaterialInterface* GetMenuHoverMaterial();

	/** Procedural filament/mote material placed behind screen titles. Null if the asset is missing. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UMaterialInterface* GetTitleAtmosphereMaterial();

	/** Shared RichText style set every styled RichTextBlock must reference. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UDataTable* GetMenuTextStyleSet();

	/** Exo 2 composite font used for stats, values, labels, and buttons. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UFont* GetExo2Font();

	/** Bruno Ace display face used by major screen headings. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UFont* GetBrunoAceFont();

	/** Inter face used for readable supporting and transient copy. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static UFont* GetInterFont();

	/**
	 * Applies the approved menu-button treatment: dark violet fills that keep pale
	 * MenuLabel text readable, violet outlines, the animated hover material, and a
	 * centered content slot so labels sit in the middle of full-height menu rows.
	 * The label widget itself is never replaced or retexted.
	 *
	 * @param Button Button to restyle. Null is a safe no-op.
	 * @param bUseAnimatedHover When true the hovered brush uses the living edge-light material.
	 * @param HoverMaterialOverride Optional replacement hover material. Null loads the shared one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void ApplyMenuButtonStyle(UButton* Button, bool bUseAnimatedHover = true, UMaterialInterface* HoverMaterialOverride = nullptr);

	/**
	 * Turns an Image into a non-interactive title-atmosphere layer: brush uses a
	 * material instance of the shared atmosphere effect, desired size is zeroed so
	 * text drives layout, and hit testing is disabled so it never blocks input.
	 *
	 * @param Atmosphere Target image placed behind the title text.
	 * @param OuterForMID Outer for the created material instance (usually the owning widget).
	 * @return The created material instance, or null when the image or material is missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static UMaterialInstanceDynamic* ApplyTitleAtmosphere(UImage* Atmosphere, UObject* OuterForMID, float Intensity = 1.f, float MotionSpeed = 1.f, UMaterialInterface* AtmosphereMaterialOverride = nullptr);

	/** Exo 2 Medium 20 profile value treatment (lavender, or warm gold for currency). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleProfileLabel(UTextBlock* Label, bool bIsGold = false);

	/** Smaller Exo 2 Medium 16 treatment for profile/operation status lines. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleStatusLabel(UTextBlock* Label);

	/** Violet XP fill over a dark track, matching the landing profile band. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleXPBar(UProgressBar* Bar, bool bGhost = false);

	/** Compact display treatment for inventory lane headings. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleInventoryLaneHeading(URichTextBlock* Heading);

	/** Strong level badge typography for the persistent HUD. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleHUDLevelText(UTextBlock* Label);

	/** Readable compact values placed over persistent HUD bars. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleHUDValueText(UTextBlock* Label);

	/** Borderless red gameplay feedback with a black glyph outline for scene contrast. */
	static void StyleGameplayWarning(UTextBlock* Label);

	/** Scene-overlay difficulty text needs contrast independent of the world behind it. */
	static void StyleMissionDifficulty(UTextBlock* Label);

	/** Bruno Ace treatment for compact mission and defense-objective headings. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMissionHeading(UTextBlock* Label);

	/** Exo 2 treatment for mission counters and defense-objective values. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMissionValue(UTextBlock* Label, bool bWarning = false);

	/** Compact gold total or pickup-delta treatment used by persistent mission HUDs. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMissionGold(UTextBlock* Label, bool bDelta = false);

	/** Rounded living-theme progress treatment for mission and defense-objective bars. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMissionProgressBar(UProgressBar* Bar, bool bDefenseObjective = false);

	/** Shared low-cost bar treatment for enemy/world status widgets. Preserves the runtime fill color. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleCompactStatusBar(UProgressBar* Bar);

	/** Compact level/value label used by floating status widgets. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleFloatingStatusText(UTextBlock* Label);

	/** Readability treatment for native floating combat text. Preserves its result-specific color. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleCombatText(UTextBlock* Label);

	/** Dark living-menu surface and readable copy for toast and error messages. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleTransientMessage(UBorder* Surface, UTextBlock* Message, bool bErrorPresentation = false);

	/** Bruno Ace violet treatment for a major screen heading. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleScreenHeading(UTextBlock* Label);

	/** Inter supporting-copy treatment. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleBodyText(UTextBlock* Label);

	/** Exo 2 treatment for metric names. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMetricLabel(UTextBlock* Label);

	/** Exo 2 treatment for metric values, optionally using the warning accent. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMetricValue(UTextBlock* Label, bool bWarning = false);

	/** Exo 2 treatment for text nested inside a themed menu button. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleMenuButtonLabel(UTextBlock* Label);

	/** Violet/cyan living-menu treatment for sliders. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Style")
	static void StyleSlider(USlider* Slider);

	/** Localized "Level {0}" text using Frontend_LevelFormat. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static FText FormatFrontendLevel(int32 CharacterLevel);

	/** Localized "{0} Gold" text using Frontend_GoldFormat. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static FText FormatFrontendGold(int64 Gold);

	/** Localized "XP {0} / {1}" text using Frontend_XPFormat with safely clamped integers. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static FText FormatFrontendXP(float CurrentXP, float RequiredXP);

	/** Clamped 0-1 XP fraction. Non-finite or non-positive requirements yield 0. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static float ComputeFrontendXPPercent(float CurrentXP, float RequiredXP);

	/** Safely clamped non-negative XP integer for display. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Style")
	static int64 FrontendXPInteger(float Value);
};
