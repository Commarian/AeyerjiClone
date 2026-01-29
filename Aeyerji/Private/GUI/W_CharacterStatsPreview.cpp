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

#define LOCTEXT_NAMESPACE "AeyerjiStatsPreview"

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
}

UW_CharacterStatsPreview::UW_CharacterStatsPreview(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    if (Rows.Num() == 0)
    {
        Rows = {
            MakeRow(LOCTEXT("Level", "Level"), UAeyerjiAttributeSet::GetLevelAttribute()),
            MakeRow(LOCTEXT("AttackDamage", "Damage"), UAeyerjiAttributeSet::GetAttackDamageAttribute()),
            MakeRow(LOCTEXT("AttackSpeed", "Attack Speed"), UAeyerjiAttributeSet::GetAttackSpeedAttribute(), 2),
            MakeRow(LOCTEXT("AttackRange", "Attack Range"), UAeyerjiAttributeSet::GetAttackRangeAttribute(), 1),
            MakeRow(LOCTEXT("Armor", "Armor"), UAeyerjiAttributeSet::GetArmorAttribute()),
            MakeRow(LOCTEXT("CritChance", "Crit Chance"), UAeyerjiAttributeSet::GetCritChanceAttribute(), 1, true),
            MakeRow(LOCTEXT("CooldownReduction", "Cooldown Reduction"), UAeyerjiAttributeSet::GetCooldownReductionAttribute(), 1, true),
            MakeRow(LOCTEXT("Strength", "Strength"), UAeyerjiAttributeSet::GetStrengthAttribute()),
            MakeRow(LOCTEXT("Agility", "Agility"), UAeyerjiAttributeSet::GetAgilityAttribute()),
            MakeRow(LOCTEXT("Intellect", "Intellect"), UAeyerjiAttributeSet::GetIntellectAttribute()),
            MakeRow(LOCTEXT("Ailment", "Ailment"), UAeyerjiAttributeSet::GetAilmentAttribute()),
            MakeRow(LOCTEXT("SpellPower", "Spell Power"), UAeyerjiAttributeSet::GetSpellPowerAttribute()),
            MakeRow(LOCTEXT("MagicAmp", "Magic Amp"), UAeyerjiAttributeSet::GetMagicAmpAttribute(), 1, true),
            MakeRow(LOCTEXT("HPRegen", "HP Regen"), UAeyerjiAttributeSet::GetHPRegenAttribute(), 1, false, true),
            MakeRow(LOCTEXT("ManaRegen", "Mana Regen"), UAeyerjiAttributeSet::GetManaRegenAttribute(), 1, false, true),
            MakeRow(LOCTEXT("RunSpeed", "Run Speed"), UAeyerjiAttributeSet::GetRunSpeedAttribute()),
            MakeRow(LOCTEXT("WalkSpeed", "Walk Speed"), UAeyerjiAttributeSet::GetWalkSpeedAttribute())
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
        }

        const int32 RuntimeIndex = ActiveRows.Add(MoveTemp(Runtime));
        if (RowDef.Attribute.IsValid())
        {
            AttributeToIndex.Add(RowDef.Attribute, RuntimeIndex);
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
        ValueText->SetText(LOCTEXT("NoASCValue", "--"));
        ApplyVisibilityForRow(Index, 0.f);
        return;
    }

    ValueText->SetText(FormatValue(RawValue, Runtime.Definition));
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
        ? FText::Format(LOCTEXT("PercentFormat", "{0}%"), Number)
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

#undef LOCTEXT_NAMESPACE
