// W_AbilitySelectionNative.cpp

#include "GUI/W_AbilitySelectionNative.h"

#include "Aeyerji/AeyerjiPlayerState.h"
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

	if (AAeyerjiPlayerState* PlayerState = ResolveOwningPlayerState())
	{
		PlayerState->OnAbilityProgressionChanged.RemoveDynamic(this, &UW_AbilitySelectionNative::HandleAbilityProgressionChanged);
		PlayerState->OnAbilityProgressionChanged.AddDynamic(this, &UW_AbilitySelectionNative::HandleAbilityProgressionChanged);
		BoundPlayerState = PlayerState;
	}

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

void UW_AbilitySelectionNative::NativeDestruct()
{
	if (AAeyerjiPlayerState* PlayerState = BoundPlayerState.Get())
	{
		PlayerState->OnAbilityProgressionChanged.RemoveDynamic(this, &UW_AbilitySelectionNative::HandleAbilityProgressionChanged);
	}

	BoundPlayerState.Reset();
	Super::NativeDestruct();
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

void UW_AbilitySelectionNative::SetPotionSlotContext(const bool bInEditingPotionSlot)
{
	bEditingPotionSlot = bInEditingPotionSlot;
}

void UW_AbilitySelectionNative::SetPickerMode(const EAeyerjiAbilityPickerMode InPickerMode)
{
	if (PickerMode == InPickerMode)
	{
		return;
	}

	PickerMode = InPickerMode;
	RebuildAbilityGrid();
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
	int32 VisibleIndex = 0;
	for (int32 Index = 0; Index < AbilitySlots.Num(); ++Index)
	{
		const bool bIsPotionAbility = AeyerjiAbilitySlotUtils::IsPotionAbilityTagContainer(AbilitySlots[Index].Tag);
		if (bIsPotionAbility != bEditingPotionSlot)
		{
			continue;
		}

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

		FAeyerjiAbilityPickerEntryData EntryData;
		BuildPickerEntryData(AbilitySlots[Index], EntryData);

		if (UW_AbilityIconNative* AbilityIcon = Cast<UW_AbilityIconNative>(IconWidget))
		{
			AbilityIcon->InitializeAbilityIcon(EntryData.Slot, EntryData, this);
		}

		const int32 Row = VisibleIndex / ColumnCount;
		const int32 Column = VisibleIndex % ColumnCount;
		UniformGridPanel_Abilities->AddChildToUniformGrid(IconWidget, Row, Column);
		++VisibleIndex;

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

void UW_AbilitySelectionNative::RequestUpgradeAbility(const FAeyerjiAbilitySlot& SlotData)
{
	if (AAeyerjiPlayerState* PlayerState = ResolveOwningPlayerState())
	{
		for (const FGameplayTag& Tag : SlotData.Tag)
		{
			if (Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Ability.")))
			{
				OnAbilityUpgradeRequested.Broadcast(Tag);
				PlayerState->Server_RequestAbilityRankUp(Tag);
				break;
			}
		}
	}
}

bool UW_AbilitySelectionNative::BuildPickerEntryData(const FAeyerjiAbilitySlot& SlotData, FAeyerjiAbilityPickerEntryData& OutEntryData) const
{
	OutEntryData = FAeyerjiAbilityPickerEntryData();
	OutEntryData.Slot = SlotData;

	FGameplayTag AbilityTag;
	for (const FGameplayTag& Tag : SlotData.Tag)
	{
		if (Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Ability.")))
		{
			AbilityTag = Tag;
			break;
		}
	}

	OutEntryData.AbilityTag = AbilityTag;
	if (!AbilityTag.IsValid())
	{
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UAeyerjiAbilityTuningSubsystem* TuningSubsystem = GameInstance ? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>() : nullptr;
	AAeyerjiPlayerState* PlayerState = ResolveOwningPlayerState();
	if (!TuningSubsystem || !PlayerState)
	{
		return false;
	}

	OutEntryData.CurrentRank = PlayerState->GetAbilityRank(AbilityTag);
	OutEntryData.MaxRank = TuningSubsystem->GetMaxAbilityRank(AbilityTag);
	OutEntryData.bBaseUnlocked = PlayerState->IsAbilityBaseUnlocked(AbilityTag);
	OutEntryData.RemainingAbilityPoints = PlayerState->GetUnspentAbilityPoints();
	OutEntryData.Slot.Level = FMath::Max(1, OutEntryData.CurrentRank);

	const int32 NextRank = OutEntryData.CurrentRank > 0 ? OutEntryData.CurrentRank + 1 : 2;
	if (const FAeyerjiAbilityRankTableRow* NextRankRow = TuningSubsystem->FindAbilityRankRow(AbilityTag, NextRank))
	{
		OutEntryData.PointCost = NextRankRow->PointCost;
		OutEntryData.RequiredPlayerLevel = NextRankRow->RequiredPlayerLevel;
	}

	FText FailureReason;
	OutEntryData.bCanUpgrade = PlayerState->CanUpgradeAbility(AbilityTag, FailureReason);
	return true;
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

AAeyerjiPlayerState* UW_AbilitySelectionNative::ResolveOwningPlayerState() const
{
	if (AAeyerjiPlayerState* PlayerState = BoundPlayerState.Get())
	{
		return PlayerState;
	}

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		return PlayerController->GetPlayerState<AAeyerjiPlayerState>();
	}

	return nullptr;
}

void UW_AbilitySelectionNative::HandleAbilityProgressionChanged(const TArray<FAeyerjiAbilityProgressEntry>& ProgressEntries, int32 RemainingPoints, int32 TotalPointSpends)
{
	(void)ProgressEntries;
	(void)RemainingPoints;
	(void)TotalPointSpends;
	RebuildAbilityGrid();
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
