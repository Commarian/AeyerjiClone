// AeyerjiFloatingStatusBarComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "AeyerjiFloatingStatusBarComponent.generated.h"

class UW_AeyerjiStatusBar;
class UWidgetComponent;
class UAbilitySystemComponent;
class UAeyerjiStatusBarOverlayComponent;
class UUserWidget;

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

    /** World offset above actor (used by World & Overlay modes). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar")
    FVector WorldOffset = FVector(0.f, 0.f, 110.f);

    /** Extra pixel offset after projection (Overlay only). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay")
    FVector2D OverlayPixelOffset = FVector2D(0.f, -4.f);

    /** Extra ZOrder for overlay widgets. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBar|Overlay")
    int32 OverlayZOrder = 0;

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

    // Accessors used by overlay manager
    TSubclassOf<UW_AeyerjiStatusBar> GetStatusBarWidgetClass() const { return StatusBarWidgetClass; }
    const FVector& GetWorldOffset() const { return WorldOffset; }
    const FVector2D& GetOverlayPixelOffset() const { return OverlayPixelOffset; }
    int32 GetOverlayZOrder() const { return OverlayZOrder; }
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

    void CreateWorldWidget();
    void CreateHUDWidget();
    void RegisterWithOverlay();   // robust (retries until PC is ready)
    void CleanupOverlay();
    void BindWidget(UW_AeyerjiStatusBar* WB);
    UAbilitySystemComponent* FindASC() const;

    void LogMissingWidget() const;
};
