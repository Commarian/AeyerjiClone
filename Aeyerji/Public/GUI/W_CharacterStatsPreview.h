// W_CharacterStatsPreview.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "W_CharacterStatsPreview.generated.h"

class UAbilitySystemComponent;
class UBorder;
class UTextBlock;
class UVerticalBox;

/**
 * Simple native widget that lists key GAS attributes in a vertical stack for quick previews / tooltips.
 * Intended to be subclassed in BP and styled there (add a Border + VerticalBox and bind them).
 */
USTRUCT(BlueprintType)
struct FCharacterStatPreviewRow
{
    GENERATED_BODY()

    /** Text label shown to the left (e.g. "Damage"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    FText Label = FText::GetEmpty();

    /** Attribute to watch from the bound AbilitySystemComponent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    FGameplayAttribute Attribute;

    /** Number of fractional digits to show (0 = ints). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview", meta=(ClampMin="0", UIMin="0"))
    int32 FractionalDigits = 0;

    /** If true, multiply by 100 and append a % sign. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    bool bFormatAsPercent = false;

    /** Optional: collapse the row when the value is exactly zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    bool bHideIfZero = false;

    /** Optional text format (supports {Value} and {Label} placeholders). Leave empty to show just the number. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    FText ValueFormatText;
};

UCLASS()
class AEYERJI_API UW_CharacterStatsPreview : public UUserWidget
{
    GENERATED_BODY()

public:
    UW_CharacterStatsPreview(const FObjectInitializer& ObjectInitializer);

    /** Bind to an ASC to start listening for attribute changes and populate the list. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatsPreview")
    void BindToAbilitySystem(UAbilitySystemComponent* InASC);

    /** Manually force a refresh if you change row definitions at runtime. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatsPreview")
    void RefreshAll();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    /** Optional designer widgets; the code will create fallbacks if they are missing. */
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UBorder* OuterBorder = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UVerticalBox* StatListBox = nullptr;

    /** Layout tuning for generated rows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    FMargin RowPadding = FMargin(2.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    float LabelMinDesiredWidth = 120.f;

    /** Optional fonts for generated labels/values (leave unset to use designer defaults). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview|Style")
    FSlateFontInfo LabelFont;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview|Style")
    FSlateFontInfo ValueFont;

    /** Optional tint overrides for generated labels/values (leave unset to inherit defaults). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview|Style")
    FSlateColor LabelColor = FSlateColor(FLinearColor::White);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview|Style")
    FSlateColor ValueColor = FSlateColor(FLinearColor::White);

    /** Rows to render; defaults are seeded from UAeyerjiAttributeSet. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatsPreview")
    TArray<FCharacterStatPreviewRow> Rows;

private:
    void EnsureContainers();
    void BuildRows();
    void ClearRows();
    void UnbindDelegates();
    void HandleAttributeChanged(const FOnAttributeChangeData& Data);
    void RefreshRowValue(int32 Index);
    FText FormatValue(float RawValue, const FCharacterStatPreviewRow& RowDef) const;
    void ApplyVisibilityForRow(int32 Index, float Value);

    struct FRowRuntime
    {
        FCharacterStatPreviewRow Definition;
        TWeakObjectPtr<UTextBlock> ValueText;
        TWeakObjectPtr<UWidget> RowWidget;
        FDelegateHandle ChangeHandle;
    };

    TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
    TArray<FRowRuntime> ActiveRows;
    TMap<FGameplayAttribute, int32> AttributeToIndex;
};
