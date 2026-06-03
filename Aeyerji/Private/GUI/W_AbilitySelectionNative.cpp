// W_AbilitySelectionNative.cpp

#include "GUI/W_AbilitySelectionNative.h"

#include "Abilities/AeyerjiAbilityTuning.h"
#include "Blueprint/WidgetTree.h"
#include "AbilitySystemComponent.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/GameInstance.h"
#include "GUI/W_AbilityIconNative.h"

void UW_AbilitySelectionNative::NativeConstruct()
{
	Super::NativeConstruct();

	if (bPopulateAbilitiesOnConstruct)
	{
		RebuildAbilityGrid();
	}

	RefreshManagedUniformGrids();

	for (const TWeakObjectPtr<UUniformGridPanel>& GridPanelPtr : ManagedUniformGrids)
	{
		if (UUniformGridPanel* GridPanel = GridPanelPtr.Get())
		{
			NormalizeUniformGridChildren(*GridPanel);
			UpdateUniformGridSizing(*GridPanel);
		}
	}
}

void UW_AbilitySelectionNative::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (ManagedUniformGrids.Num() == 0)
	{
		RefreshManagedUniformGrids();
	}

	for (const TWeakObjectPtr<UUniformGridPanel>& GridPanelPtr : ManagedUniformGrids)
	{
		if (UUniformGridPanel* GridPanel = GridPanelPtr.Get())
		{
			NormalizeUniformGridChildren(*GridPanel);
			UpdateUniformGridSizing(*GridPanel);
		}
	}
}

void UW_AbilitySelectionNative::SetAbilitySystemForTooltip(UAbilitySystemComponent* InAbilitySystem)
{
	AbilitySystemForTooltip = InAbilitySystem;
}

void UW_AbilitySelectionNative::RebuildAbilityGrid()
{
	UE_LOG(LogTemp, Warning, TEXT("AbilitySelection: RebuildAbilityGrid called. Grid=%s IconClass=%s"),
		*GetNameSafe(UniformGridPanel_Abilities),
		*GetNameSafe(AbilityIconWidgetClass));

	if (!UniformGridPanel_Abilities || !AbilityIconWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("AbilitySelection: Missing grid or icon widget class."));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UAeyerjiAbilityTuningSubsystem* TuningSubsystem = GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>();
	if (!TuningSubsystem)
	{
		return;
	}

	TArray<FAeyerjiAbilitySlot> AbilitySlots;
	TuningSubsystem->GetAllAbilitySlotsSorted(AbilitySlots);

	UE_LOG(LogTemp, Warning, TEXT("AbilitySelection: AbilitySlots.Num = %d"), AbilitySlots.Num());

	UniformGridPanel_Abilities->ClearChildren();

	const int32 ColumnCount = FMath::Max(1, AbilityGridColumns);
	for (int32 Index = 0; Index < AbilitySlots.Num(); ++Index)
	{
		UE_LOG(LogTemp, Warning, TEXT("AbilitySelection: Creating icon %d Tag=%s Class=%s"),
			Index,
			*AbilitySlots[Index].Tag.ToString(),
			*GetNameSafe(AbilitySlots[Index].Class));

		UUserWidget* IconWidget = GetOwningPlayer()
			? CreateWidget<UUserWidget>(GetOwningPlayer(), AbilityIconWidgetClass)
			: CreateWidget<UUserWidget>(GameInstance, AbilityIconWidgetClass);
		if (!IconWidget)
		{
			continue;
		}

		if (UW_AbilityIconNative* AbilityIcon = Cast<UW_AbilityIconNative>(IconWidget))
		{
			AbilityIcon->InitializeAbilityIcon(AbilitySlots[Index], this);
		}

		const int32 Row = Index / ColumnCount;
		const int32 Column = Index % ColumnCount;
		UniformGridPanel_Abilities->AddChildToUniformGrid(IconWidget, Row, Column);

		UE_LOG(LogTemp, Warning, TEXT("AbilitySelection: Grid child count = %d"),
			UniformGridPanel_Abilities->GetChildrenCount());
	}

	RefreshManagedUniformGrids();

	for (const TWeakObjectPtr<UUniformGridPanel>& GridPanelPtr : ManagedUniformGrids)
	{
		if (UUniformGridPanel* GridPanel = GridPanelPtr.Get())
		{
			NormalizeUniformGridChildren(*GridPanel);
			UpdateUniformGridSizing(*GridPanel);
		}
	}
}

void UW_AbilitySelectionNative::ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget)
{
	if (SlotData.Tag.IsEmpty())
	{
		return;
	}

	LastTooltipData = FAeyerjiAbilityTooltipData::FromSlot(
		AbilitySystemForTooltip.Get(),
		SlotData,
		EAbilityTooltipSource::AbilityPicker);

	SetActiveTooltipSource(SourceWidget);
	BP_ShowAbilityTooltip(LastTooltipData, ScreenPosition, SourceWidget);
}

void UW_AbilitySelectionNative::HideAbilityTooltip(UWidget* SourceWidget)
{
	if (ActiveTooltipSource.IsValid() && SourceWidget && ActiveTooltipSource.Get() != SourceWidget)
	{
		return;
	}

	BP_HideAbilityTooltip(LastTooltipData, SourceWidget);
	ActiveTooltipSource.Reset();
	LastTooltipData = FAeyerjiAbilityTooltipData();
}

void UW_AbilitySelectionNative::SetActiveTooltipSource(UWidget* SourceWidget)
{
	if (!SourceWidget)
	{
		ActiveTooltipSource.Reset();
		return;
	}

	ActiveTooltipSource = SourceWidget;
}

void UW_AbilitySelectionNative::RefreshManagedUniformGrids()
{
	ManagedUniformGrids.Reset();

	if (!WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);

	for (UWidget* Widget : AllWidgets)
	{
		if (UUniformGridPanel* GridPanel = Cast<UUniformGridPanel>(Widget))
		{
			if (GridPanel->GetChildrenCount() > 0)
			{
				ManagedUniformGrids.AddUnique(GridPanel);
			}
		}
	}
}

void UW_AbilitySelectionNative::NormalizeUniformGridChildren(UUniformGridPanel& GridPanel)
{
	for (int32 ChildIndex = GridPanel.GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
	{
		UWidget* ChildWidget = GridPanel.GetChildAt(ChildIndex);
		if (!ChildWidget)
		{
			continue;
		}

		if (USizeBox* ExistingWrapper = Cast<USizeBox>(ChildWidget))
		{
			if (UUniformGridSlot* WrapperSlot = Cast<UUniformGridSlot>(ExistingWrapper->Slot))
			{
				WrapperSlot->SetHorizontalAlignment(HAlign_Center);
				WrapperSlot->SetVerticalAlignment(VAlign_Center);
			}

			continue;
		}

		UUniformGridSlot* ExistingSlot = Cast<UUniformGridSlot>(ChildWidget->Slot);
		if (!ExistingSlot)
		{
			continue;
		}

		const int32 Row = ExistingSlot->GetRow();
		const int32 Column = ExistingSlot->GetColumn();

		GridPanel.RemoveChildAt(ChildIndex);

		const FName WrapperName = MakeUniqueObjectName(this, USizeBox::StaticClass(), TEXT("AbilitySelectionGridWrapper"));
		USizeBox* Wrapper = NewObject<USizeBox>(this, USizeBox::StaticClass(), WrapperName);
		if (!Wrapper)
		{
			GridPanel.AddChildToUniformGrid(ChildWidget, Row, Column);
			continue;
		}

		Wrapper->AddChild(ChildWidget);

		if (UUniformGridSlot* WrapperSlot = GridPanel.AddChildToUniformGrid(Wrapper, Row, Column))
		{
			WrapperSlot->SetHorizontalAlignment(HAlign_Center);
			WrapperSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
}

void UW_AbilitySelectionNative::UpdateUniformGridSizing(UUniformGridPanel& GridPanel)
{
	int32 RowCount = 0;
	int32 ColumnCount = 0;
	if (!GetUniformGridDimensions(GridPanel, RowCount, ColumnCount))
	{
		return;
	}

	const FVector2D LocalSize = GridPanel.GetCachedGeometry().GetLocalSize();
	if (LocalSize.X <= KINDA_SMALL_NUMBER || LocalSize.Y <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float CellWidth = LocalSize.X / static_cast<float>(ColumnCount);
	const float CellHeight = LocalSize.Y / static_cast<float>(RowCount);
	const float SquareSize = FMath::Max(1.f, FMath::Min(CellWidth, CellHeight));

	for (int32 ChildIndex = 0; ChildIndex < GridPanel.GetChildrenCount(); ++ChildIndex)
	{
		if (USizeBox* Wrapper = Cast<USizeBox>(GridPanel.GetChildAt(ChildIndex)))
		{
			Wrapper->SetWidthOverride(SquareSize);
			Wrapper->SetHeightOverride(SquareSize);

			if (UUniformGridSlot* WrapperSlot = Cast<UUniformGridSlot>(Wrapper->Slot))
			{
				WrapperSlot->SetHorizontalAlignment(HAlign_Center);
				WrapperSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
	}
}

bool UW_AbilitySelectionNative::GetUniformGridDimensions(const UUniformGridPanel& GridPanel, int32& OutRows, int32& OutColumns) const
{
	OutRows = 0;
	OutColumns = 0;

	for (int32 ChildIndex = 0; ChildIndex < GridPanel.GetChildrenCount(); ++ChildIndex)
	{
		const UWidget* ChildWidget = GridPanel.GetChildAt(ChildIndex);
		const UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(ChildWidget ? ChildWidget->Slot : nullptr);
		if (!GridSlot)
		{
			continue;
		}

		OutRows = FMath::Max(OutRows, GridSlot->GetRow() + 1);
		OutColumns = FMath::Max(OutColumns, GridSlot->GetColumn() + 1);
	}

	return OutRows > 0 && OutColumns > 0;
}
