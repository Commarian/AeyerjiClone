// W_CharacterStatsPreview.cpp

#include "GUI/W_CharacterStatsPreview.h"

#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"
#include "GUI/AeyerjiStringLibrary.h"

namespace
{
    FCharacterStatPreviewRow MakeRow(const FText& Label, const FGameplayAttribute& Attribute, int32 FractionalDigits = 0, bool bAsPercent = false, bool bHideIfZero = false)
    {
        FCharacterStatPreviewRow Row;
        Row.Label = Label;
        Row.Attribute = Attribute;
        Row.FractionalDigits = FractionalDigits;
        Row.bFormatAsPercent = bAsPercent;
        Row.bHideIfZero = bHideIfZero;
        return Row;
    }

    FCharacterStatPreviewRow MakeDamageRangeRow()
    {
        FCharacterStatPreviewRow Row = MakeRow(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatLabelDamage")), UAeyerjiAttributeSet::GetAttackDamageAttribute());
        Row.SecondaryAttribute = UAeyerjiAttributeSet::GetAttackDamageVarianceAttribute();
        Row.bFormatAsDamageRange = true;
        return Row;
    }
}

UW_CharacterStatsPreview::UW_CharacterStatsPreview(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    if (Rows.Num() == 0)
    {
        // All stat labels resolved via GlobalStringTable.csv for localization.
        // Add/reimport keys in Data/Strings/GlobalStringTable.csv + reimport asset in editor.
        using namespace AeyerjiStringLibrary;
        Rows = {
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelLevel")), UAeyerjiAttributeSet::GetLevelAttribute()),
            MakeDamageRangeRow(),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelAttackSpeed")), UAeyerjiAttributeSet::GetAttackSpeedAttribute(), 2),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelAttackRange")), UAeyerjiAttributeSet::GetAttackRangeAttribute(), 1),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelArmor")), UAeyerjiAttributeSet::GetArmorAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelPhysicalDamage")), UAeyerjiAttributeSet::GetPhysicalDamageBonusAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelArmorPen")), UAeyerjiAttributeSet::GetArmorPenetrationAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelCritChance")), UAeyerjiAttributeSet::GetCritChanceAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelCritDamage")), UAeyerjiAttributeSet::GetCriticalDamageMultiplierAttribute(), 0, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelLifeSteal")), UAeyerjiAttributeSet::GetLifeStealAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelStaggerPower")), UAeyerjiAttributeSet::GetStaggerPowerAttribute(), 2),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelStaggerResist")), UAeyerjiAttributeSet::GetStaggerResistanceAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelPoise")), UAeyerjiAttributeSet::GetPoiseAttribute(), 0),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelCooldownRed")), UAeyerjiAttributeSet::GetCooldownReductionAttribute(), 1, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelStrength")), UAeyerjiAttributeSet::GetStrengthAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelAgility")), UAeyerjiAttributeSet::GetAgilityAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelIntellect")), UAeyerjiAttributeSet::GetIntellectAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelPoisonAmt")), UAeyerjiAttributeSet::GetPoisonAmountAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelPoisonDur")), UAeyerjiAttributeSet::GetPoisonDurationAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelTraumaAmt")), UAeyerjiAttributeSet::GetTraumaAmountAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelTraumaDur")), UAeyerjiAttributeSet::GetTraumaDurationAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelCorruptionAmt")), UAeyerjiAttributeSet::GetCorruptionAmountAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelCorruptionDur")), UAeyerjiAttributeSet::GetCorruptionDurationAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelSpellPower")), UAeyerjiAttributeSet::GetSpellPowerAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelMagicAmp")), UAeyerjiAttributeSet::GetMagicAmpAttribute(), 1),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelHPRegen")), UAeyerjiAttributeSet::GetHPRegenAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelManaRegen")), UAeyerjiAttributeSet::GetManaRegenAttribute(), 1, false, true),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelRunSpeed")), UAeyerjiAttributeSet::GetRunSpeedAttribute()),
            MakeRow(GetGlobalStringTableText(TEXT("StatLabelWalkSpeed")), UAeyerjiAttributeSet::GetWalkSpeedAttribute())
        };
    }
}

void UW_CharacterStatsPreview::NativeConstruct()
{
    Super::NativeConstruct();

    EnsureContainers();

    // Allow previewing in the designer even before binding to an ASC.
    if (ActiveRows.Num() == 0 && StatListBox)
    {
        BuildRows();
        RefreshAll();
    }
}

void UW_CharacterStatsPreview::NativeDestruct()
{
    UnbindDelegates();
    Super::NativeDestruct();
}

void UW_CharacterStatsPreview::BindToAbilitySystem(UAbilitySystemComponent* InASC)
{
    if (BoundASC.Get() == InASC && ActiveRows.Num() > 0)
    {
        RefreshAll();
        return;
    }

    BoundASC = InASC;
    BuildRows();
    RefreshAll();
}

void UW_CharacterStatsPreview::RefreshAll()
{
    for (int32 Index = 0; Index < ActiveRows.Num(); ++Index)
    {
        RefreshRowValue(Index);
    }
}

void UW_CharacterStatsPreview::EnsureContainers()
{
    if (!StatListBox && WidgetTree)
    {
        StatListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GeneratedStatList"));
        if (!WidgetTree->RootWidget)
        {
            WidgetTree->RootWidget = StatListBox;
        }
    }

    if (OuterBorder && StatListBox && OuterBorder->GetContent() != StatListBox)
    {
        OuterBorder->SetContent(StatListBox);
    }
}

void UW_CharacterStatsPreview::BuildRows()
{
    EnsureContainers();
    ClearRows();

    if (!StatListBox)
    {
        return;
    }

    ActiveRows.Reserve(Rows.Num());
    AttributeToIndex.Reserve(Rows.Num());

    for (const FCharacterStatPreviewRow& RowDef : Rows)
    {
        UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

        UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        LabelText->SetText(RowDef.Label);
        LabelText->SetMinDesiredWidth(LabelMinDesiredWidth);
        if (LabelFont.FontObject)
        {
            LabelText->SetFont(LabelFont);
        }
        if (LabelColor.IsColorSpecified())
        {
            LabelText->SetColorAndOpacity(LabelColor);
        }

        UTextBlock* ValueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        if (ValueFont.FontObject)
        {
            ValueText->SetFont(ValueFont);
        }
        if (ValueColor.IsColorSpecified())
        {
            ValueText->SetColorAndOpacity(ValueColor);
        }

        UHorizontalBoxSlot* LabelSlot = RowBox->AddChildToHorizontalBox(LabelText);
        LabelSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
        LabelSlot->SetSize( FSlateChildSize(ESlateSizeRule::Automatic) );

        UHorizontalBoxSlot* ValueSlot = RowBox->AddChildToHorizontalBox(ValueText);
        ValueSlot->SetSize( FSlateChildSize(ESlateSizeRule::Fill) );

        UVerticalBoxSlot* VBoxSlot = StatListBox->AddChildToVerticalBox(RowBox);
        VBoxSlot->SetPadding(RowPadding);

        FRowRuntime Runtime;
        Runtime.Definition = RowDef;
        Runtime.ValueText = ValueText;
        Runtime.RowWidget = RowBox;

        if (UAbilitySystemComponent* ASC = BoundASC.Get())
        {
            Runtime.ChangeHandle = ASC->GetGameplayAttributeValueChangeDelegate(RowDef.Attribute)
                .AddUObject(this, &UW_CharacterStatsPreview::HandleAttributeChanged);
            if (RowDef.SecondaryAttribute.IsValid())
            {
                Runtime.SecondaryChangeHandle = ASC->GetGameplayAttributeValueChangeDelegate(RowDef.SecondaryAttribute)
                    .AddUObject(this, &UW_CharacterStatsPreview::HandleAttributeChanged);
            }
        }

        const int32 RuntimeIndex = ActiveRows.Add(MoveTemp(Runtime));
        if (RowDef.Attribute.IsValid())
        {
            AttributeToIndex.Add(RowDef.Attribute, RuntimeIndex);
        }
        if (RowDef.SecondaryAttribute.IsValid())
        {
            AttributeToIndex.Add(RowDef.SecondaryAttribute, RuntimeIndex);
        }
    }
}

void UW_CharacterStatsPreview::ClearRows()
{
    UnbindDelegates();
    AttributeToIndex.Reset();
    ActiveRows.Reset();

    if (StatListBox)
    {
        StatListBox->ClearChildren();
    }
}

void UW_CharacterStatsPreview::UnbindDelegates()
{
    if (UAbilitySystemComponent* ASC = BoundASC.Get())
    {
        for (FRowRuntime& Row : ActiveRows)
        {
            if (Row.ChangeHandle.IsValid())
            {
                ASC->GetGameplayAttributeValueChangeDelegate(Row.Definition.Attribute).Remove(Row.ChangeHandle);
                Row.ChangeHandle.Reset();
            }
            if (Row.SecondaryChangeHandle.IsValid())
            {
                ASC->GetGameplayAttributeValueChangeDelegate(Row.Definition.SecondaryAttribute).Remove(Row.SecondaryChangeHandle);
                Row.SecondaryChangeHandle.Reset();
            }
        }
    }
}

void UW_CharacterStatsPreview::HandleAttributeChanged(const FOnAttributeChangeData& Data)
{
    const int32* Index = AttributeToIndex.Find(Data.Attribute);
    if (Index)
    {
        RefreshRowValue(*Index);
    }
}

void UW_CharacterStatsPreview::RefreshRowValue(int32 Index)
{
    if (!ActiveRows.IsValidIndex(Index))
    {
        return;
    }

    FRowRuntime& Runtime = ActiveRows[Index];
    UTextBlock* ValueText = Runtime.ValueText.Get();
    if (!ValueText)
    {
        return;
    }

    float RawValue = 0.f;
    bool bHasValue = false;

    if (UAbilitySystemComponent* ASC = BoundASC.Get())
    {
        if (Runtime.Definition.Attribute.IsValid())
        {
            RawValue = ASC->GetNumericAttribute(Runtime.Definition.Attribute);
            bHasValue = true;
        }
    }

    if (!bHasValue)
    {
        ValueText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatNoValue")));
        ApplyVisibilityForRow(Index, 0.f);
        return;
    }

    ValueText->SetText(Runtime.Definition.bFormatAsDamageRange
        ? FormatDamageRange(RawValue, Runtime.Definition)
        : FormatValue(RawValue, Runtime.Definition));
    ApplyVisibilityForRow(Index, RawValue);
}

FText UW_CharacterStatsPreview::FormatValue(float RawValue, const FCharacterStatPreviewRow& RowDef) const
{
    const float DisplayValue = RowDef.bFormatAsPercent ? RawValue * 100.f : RawValue;

    FNumberFormattingOptions NumberOptions;
    NumberOptions.MinimumFractionalDigits = RowDef.FractionalDigits;
    NumberOptions.MaximumFractionalDigits = RowDef.FractionalDigits;

    const FText Number = FText::AsNumber(DisplayValue, &NumberOptions);
    const FText NumberWithPercent = RowDef.bFormatAsPercent
        ? FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatPercentFormat")), Number)
        : Number;

    // Allow BP tuning via format text; supports {Value} and {Label}.
    if (!RowDef.ValueFormatText.IsEmpty())
    {
        FFormatNamedArguments Args;
        Args.Add(TEXT("Value"), NumberWithPercent);
        Args.Add(TEXT("Label"), RowDef.Label);
        return FText::Format(RowDef.ValueFormatText, Args);
    }

    return NumberWithPercent;
}

FText UW_CharacterStatsPreview::FormatDamageRange(float AverageDamage, const FCharacterStatPreviewRow& RowDef) const
{
    float Variance = 0.f;
    if (const UAbilitySystemComponent* ASC = BoundASC.Get())
    {
        if (RowDef.SecondaryAttribute.IsValid())
        {
            Variance = ASC->GetNumericAttribute(RowDef.SecondaryAttribute);
        }
    }

    Variance = FMath::Clamp(Variance, 0.f, 0.95f);
    const float MinimumDamage = FMath::Max(0.f, AverageDamage * (1.f - Variance));
    const float MaximumDamage = FMath::Max(0.f, AverageDamage * (1.f + Variance));

    FNumberFormattingOptions WholeNumberOptions;
    WholeNumberOptions.MinimumFractionalDigits = RowDef.FractionalDigits;
    WholeNumberOptions.MaximumFractionalDigits = RowDef.FractionalDigits;

    return FText::Format(
        AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatDamageRangeFormat")),
        FText::AsNumber(MinimumDamage, &WholeNumberOptions),
        FText::AsNumber(MaximumDamage, &WholeNumberOptions),
        FText::AsNumber(AverageDamage, &WholeNumberOptions));
}

void UW_CharacterStatsPreview::ApplyVisibilityForRow(int32 Index, float Value)
{
    if (!ActiveRows.IsValidIndex(Index))
    {
        return;
    }

    const FRowRuntime& Runtime = ActiveRows[Index];
    UWidget* RowWidget = Runtime.RowWidget.Get();
    if (!RowWidget)
    {
        return;
    }

    if (Runtime.Definition.bHideIfZero && FMath::IsNearlyZero(Value))
    {
        RowWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
    else
    {
        RowWidget->SetVisibility(ESlateVisibility::Visible);
    }
}
