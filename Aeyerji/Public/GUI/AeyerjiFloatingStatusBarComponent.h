// AeyerjiFloatingStatusBarComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Aeyerji/AeyerjiGameState.h"
#include "AeyerjiFloatingStatusBarComponent.generated.h"

class UW_AeyerjiStatusBar;
class UWidgetComponent;
class UAbilitySystemComponent;
class UAeyerjiStatusBarOverlayComponent;
class UUserWidget;
class AAeyerjiCharacter;
class AAeyerjiGameState;

UENUM(BlueprintType)
enum class EStatusBarMode : uint8
{
    HUD,        // AddToViewport (player)
    World,      // UWidgetComponent in world
    Overlay     // Project world -> screen (enemies)
};

UCLASS(Blueprintable, ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiFloatingStatusBarComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAeyerjiFloatingStatusBarComponent();

    /** Widget to use (for all modes). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar")
    TSubclassOf<UW_AeyerjiStatusBar> StatusBarWidgetClass;

    /** Which technique to use. Recommended: HUD for player, Overlay for enemies. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar")
    EStatusBarMode Mode ;

    /** When true, HUD mode waits until the run enters InRun before creating the widget. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|HUD")
    bool bInitializeHUDOnRunStart = true;

    /** World offset above actor (used by World & Overlay modes). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar")
    FVector WorldOffset = FVector(0.f, 0.f, 110.f);

    /** Extra pixel offset after projection (Overlay only). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay")
    FVector2D OverlayPixelOffset = FVector2D(0.f, -4.f);

    /** Extra ZOrder for overlay widgets. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay")
    int32 OverlayZOrder = 0;

    /** When enabled, Overlay mode derives the bar width from the owning actor's collision size so smaller enemies receive shorter bars. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay|Sizing")
    bool bScaleOverlayWidthWithOwnerSize = true;

    /** The owner diameter in cm that should render at the floating widget's authored width; a standard 42 cm capsule radius is 84 cm across. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay|Sizing", meta=(ClampMin="1.0", Units="cm", EditCondition="bScaleOverlayWidthWithOwnerSize"))
    float OverlayReferenceOwnerDiameter = 84.f;

    /** Smallest allowed width multiplier, preserving readability for physically tiny enemies. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay|Sizing", meta=(ClampMin="0.05", EditCondition="bScaleOverlayWidthWithOwnerSize"))
    float OverlayMinWidthScale = 0.60f;

    /** Largest allowed width multiplier, preventing unusually large enemies from creating impractically wide UI. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay|Sizing", meta=(ClampMin="0.05", EditCondition="bScaleOverlayWidthWithOwnerSize"))
    float OverlayMaxWidthScale = 1.75f;

    /** Optional artistic multiplier applied after physical-size calculation; leave at one for direct physical scaling. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay|Sizing", meta=(ClampMin="0.05", EditCondition="bScaleOverlayWidthWithOwnerSize"))
    float OverlayWidthScaleMultiplier = 1.f;

    /** Draws Max-HP-based divisions on floating health bars. HUD mode is intentionally left unsegmented. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks")
    bool bEnableHealthChunkDivisions = true;

    /** When enabled, a floating bar is segmented only while its ASC owns any tag in HealthChunkEligibilityTags. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(EditCondition="bEnableHealthChunkDivisions"))
    bool bRequireAnyHealthChunkEligibilityTag = true;

    /**
     * Any matching owned gameplay tag enables chunks. Defaults cover standard elites,
     * elite role archetypes, mini-bosses, and bosses; normal mob roles are intentionally absent.
     */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(EditCondition="bEnableHealthChunkDivisions && bRequireAnyHealthChunkEligibilityTag"))
    FGameplayTagContainer HealthChunkEligibilityTags;

    /** Ideal number of health chunks. Chunk HP is rounded to a clean value near this target. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="1", ClampMax="16", EditCondition="bEnableHealthChunkDivisions"))
    int32 TargetHealthChunkCount = 12;

    /** Preferred lower bound used when choosing between adjacent clean chunk sizes. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="1", ClampMax="16", EditCondition="bEnableHealthChunkDivisions"))
    int32 MinPreferredHealthChunkCount = 10;

    /** Preferred upper bound used when choosing between adjacent clean chunk sizes. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="1", ClampMax="16", EditCondition="bEnableHealthChunkDivisions"))
    int32 MaxPreferredHealthChunkCount = 14;

    /** Absolute separator-count safety limit, independent of enemy rank or Max HP. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="1", ClampMax="32", EditCondition="bEnableHealthChunkDivisions"))
    int32 MaxHealthChunkCount = 16;

    /** Width of each health-chunk separator in widget-space pixels. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="0.25", ClampMax="8.0", EditCondition="bEnableHealthChunkDivisions"))
    float HealthChunkSeparatorThickness = 1.f;

    /** Space left above and below each separator so the authored health-bar frame remains visible. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(ClampMin="0.0", ClampMax="8.0", EditCondition="bEnableHealthChunkDivisions"))
    float HealthChunkSeparatorVerticalInset = 1.f;

    /** Tint applied to every generated health-chunk separator. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Health Chunks", meta=(EditCondition="bEnableHealthChunkDivisions"))
    FLinearColor HealthChunkSeparatorColor = FLinearColor(0.01f, 0.01f, 0.01f, 0.9f);

    // ---- World widget cosmetics ----
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    bool bFaceCamera = true;

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    bool bYawOnly = true;

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    bool bTwoSided = true;

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    bool bDrawAtDesiredSize = true;

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World", meta=(EditCondition="!bDrawAtDesiredSize"))
    FIntPoint DrawSize = FIntPoint(160, 18);

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    FVector2D Pivot = FVector2D(0.5f, 0.f);

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    FVector WorldScale = FVector(0.02f, 0.02f, 0.02f);

    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|World")
    int32 SortPriority = 5;

    // ---- Attributes (overrideable in BP) ----
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute HealthAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute MaxHealthAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute ManaAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute MaxManaAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute XPAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute XPMaxAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute LevelAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute HPRegenAttr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar")
    FGameplayAttribute ManaRegenAttr;

    /** Returns the live widget instance this component owns (HUD/world modes, local only). */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    UUserWidget* GetStatusBarWidget() const;

    /**
     * Immediately hides or restores the local status-bar presentation without destroying
     * this component. Pooled enemies use this on death/reuse so corpse lifetime cannot
     * leave a zero-health bar visible or permanently remove the reusable status-bar source.
     */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    void SetStatusBarPresentationVisible(bool bVisible);

    /** Returns whether this source currently permits its local status-bar presentation. */
    bool IsStatusBarPresentationVisible() const { return bStatusBarPresentationVisible; }

    // Accessors used by overlay manager
    TSubclassOf<UW_AeyerjiStatusBar> GetStatusBarWidgetClass() const { return StatusBarWidgetClass; }
    const FVector& GetWorldOffset() const { return WorldOffset; }
    const FVector2D& GetOverlayPixelOffset() const { return OverlayPixelOffset; }
    int32 GetOverlayZOrder() const { return OverlayZOrder; }
    bool ShouldScaleOverlayWidthWithOwnerSize() const { return bScaleOverlayWidthWithOwnerSize; }
    float GetOverlayReferenceOwnerDiameter() const { return OverlayReferenceOwnerDiameter; }
    float GetOverlayMinWidthScale() const { return OverlayMinWidthScale; }
    float GetOverlayMaxWidthScale() const { return OverlayMaxWidthScale; }
    float GetOverlayWidthScaleMultiplier() const { return OverlayWidthScaleMultiplier; }
    bool ShouldShowHealthChunkDivisions() const { return bEnableHealthChunkDivisions && Mode != EStatusBarMode::HUD; }
    bool ShouldRequireAnyHealthChunkEligibilityTag() const { return bRequireAnyHealthChunkEligibilityTag; }
    const FGameplayTagContainer& GetHealthChunkEligibilityTags() const { return HealthChunkEligibilityTags; }
    int32 GetTargetHealthChunkCount() const { return TargetHealthChunkCount; }
    int32 GetMinPreferredHealthChunkCount() const { return MinPreferredHealthChunkCount; }
    int32 GetMaxPreferredHealthChunkCount() const { return MaxPreferredHealthChunkCount; }
    int32 GetMaxHealthChunkCount() const { return MaxHealthChunkCount; }
    float GetHealthChunkSeparatorThickness() const { return HealthChunkSeparatorThickness; }
    float GetHealthChunkSeparatorVerticalInset() const { return HealthChunkSeparatorVerticalInset; }
    const FLinearColor& GetHealthChunkSeparatorColor() const { return HealthChunkSeparatorColor; }
    const FGameplayAttribute& GetHealthAttr() const { return HealthAttr; }
    const FGameplayAttribute& GetMaxHealthAttr() const { return MaxHealthAttr; }
    const FGameplayAttribute& GetManaAttr() const { return ManaAttr; }
    const FGameplayAttribute& GetMaxManaAttr() const { return MaxManaAttr; }
    const FGameplayAttribute& GetXPAttr() const { return XPAttr; }
    const FGameplayAttribute& GetXPMaxAttr() const { return XPMaxAttr; }
    const FGameplayAttribute& GetLevelAttr() const { return LevelAttr; }
    const FGameplayAttribute& GetHPRegenAttr() const { return HPRegenAttr; }
    const FGameplayAttribute& GetManaRegenAttr() const { return ManaRegenAttr; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    // World
    UPROPERTY(Transient) UWidgetComponent* WidgetComp = nullptr;
    // HUD
    UPROPERTY(Transient) UW_AeyerjiStatusBar* HUDWidget = nullptr;
    // Overlay
    UPROPERTY(Transient) UAeyerjiStatusBarOverlayComponent* OverlayMgr = nullptr;

    FTimerHandle RetryTimer;
    FTimerHandle DeferredBindTimer;
    TWeakObjectPtr<AAeyerjiCharacter> BoundOwnerCharacter;
    int32 DeferredBindAttempts = 0;

    void CreateWorldWidget();
    void CreateHUDWidget();
    void RegisterWithOverlay();   // robust (retries until PC is ready)
    void CleanupOverlay();
    void BindWidget(UW_AeyerjiStatusBar* WB);
    UAbilitySystemComponent* FindASC() const;
    void RebindLiveWidget();
    void BindOwnerASCReadyDelegate();
    void UnbindOwnerASCReadyDelegate();

    UFUNCTION()
    void HandleOwnerAbilitySystemReady();

    UFUNCTION()
    void AttemptDeferredRebind();

    UFUNCTION()
    void HandleRunStateChanged(EAeyerjiRunState NewState, EAeyerjiRunState OldState);

    UFUNCTION()
    void RetryBindToRunStateChanges();

    void LogMissingWidget() const;
    bool ShouldDeferHUDInitializationToRunStart() const;
    void InitializeStatusBarPresentation();
    void BindToRunStateChanges();
    void UnbindFromRunStateChanges();

    bool bStatusBarInitialized = false;
    bool bStatusBarPresentationVisible = true;
    FTimerHandle RunStateBindRetryTimer;
    TWeakObjectPtr<AAeyerjiGameState> BoundGameState;
};
