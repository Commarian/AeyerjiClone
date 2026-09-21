#include "GUI/W_AeyerjiCheatDrawer.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GUI/AeyerjiUIStyleLibrary.h"
#include "Items/ItemDefinition.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateBrush.h"

namespace
{
	UTextBlock* MakeDrawerText(UWidgetTree& WidgetTree, const FString& Value, const int32 FontSize = 14)
	{
		UTextBlock* Text = WidgetTree.ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Value));
		UAeyerjiUIStyleLibrary::StyleBodyText(Text);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		return Text;
	}

	void SetDrawerFontSize(UTextBlock* Text, const int32 FontSize)
	{
		if (!Text)
		{
			return;
		}
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
	}

	UTextBlock* MakeDrawerSectionHeading(UWidgetTree& WidgetTree, const FString& Value)
	{
		UTextBlock* Heading = MakeDrawerText(WidgetTree, Value, 16);
		UAeyerjiUIStyleLibrary::StyleMetricLabel(Heading);
		SetDrawerFontSize(Heading, 16);
		return Heading;
	}

	UButton* MakeDrawerButton(UWidgetTree& WidgetTree, const FString& Label, UTextBlock*& OutLabel)
	{
		UButton* Button = WidgetTree.ConstructWidget<UButton>();
		UAeyerjiUIStyleLibrary::ApplyMenuButtonStyle(Button, false);
		OutLabel = MakeDrawerText(WidgetTree, Label);
		UAeyerjiUIStyleLibrary::StyleMenuButtonLabel(OutLabel);
		SetDrawerFontSize(OutLabel, 14);
		OutLabel->SetJustification(ETextJustify::Center);
		Button->AddChild(OutLabel);
		return Button;
	}

	void ConfigureActionSlot(UVerticalBoxSlot* Slot)
	{
		if (Slot)
		{
			Slot->SetPadding(FMargin(0.f, 3.f));
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void UW_AeyerjiCheatDrawer::NativeConstruct()
{
	Super::NativeConstruct();

	if (!WidgetTree->RootWidget)
	{
		BuildNativeLayout();
	}

	if (!AeyerjiController)
	{
		AeyerjiController = Cast<AAeyerjiPlayerController>(GetOwningPlayer());
	}

	RebuildItemList(ItemSearchBox ? ItemSearchBox->GetText().ToString() : FString());
	// Animate the native panel without changing its authored slot or input behavior.
	EntranceElapsed = 0.f;
	RestTransform = GetRenderTransform();
	RestOpacity = GetRenderOpacity();
	SetRenderOpacity(0.f);
}

void UW_AeyerjiCheatDrawer::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (EntranceElapsed >= UAeyerjiUIStyleLibrary::PanelEntranceSeconds) return;
	EntranceElapsed += FMath::IsFinite(InDeltaTime) ? FMath::Max(0.f, InDeltaTime) : 0.f;
	const float Alpha = UAeyerjiUIStyleLibrary::EasePresentation(EntranceElapsed / UAeyerjiUIStyleLibrary::PanelEntranceSeconds);
	FWidgetTransform Transform = RestTransform;
	Transform.Translation.X += 18.f * (1.f - Alpha);
	SetRenderTransform(Transform);
	SetRenderOpacity(RestOpacity * Alpha);
}

void UW_AeyerjiCheatDrawer::InitializeForController(AAeyerjiPlayerController* InController)
{
	AeyerjiController = InController;
	SetOwningPlayer(InController);
}

void UW_AeyerjiCheatDrawer::NativeDestruct()
{
	SetRenderTransform(RestTransform);
	SetRenderOpacity(RestOpacity);
	Super::NativeDestruct();
}

void UW_AeyerjiCheatDrawer::BuildNativeLayout()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CheatDrawerRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	USizeBox* DrawerSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CheatDrawerSize"));
	DrawerSize->SetWidthOverride(470.f);
	if (UOverlaySlot* DrawerSlot = Root->AddChildToOverlay(DrawerSize))
	{
		DrawerSlot->SetHorizontalAlignment(HAlign_Right);
		DrawerSlot->SetVerticalAlignment(VAlign_Fill);
		DrawerSlot->SetPadding(FMargin(12.f, 18.f));
	}

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CheatDrawerBackground"));
	FSlateBrush BackgroundBrush;
	BackgroundBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	BackgroundBrush.TintColor = FSlateColor(FLinearColor(0.025f, 0.012f, 0.045f, 0.96f));
	BackgroundBrush.OutlineSettings.Color = FSlateColor(FLinearColor(0.20f, 0.82f, 1.f, 0.65f));
	BackgroundBrush.OutlineSettings.Width = 1.f;
	BackgroundBrush.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	Background->SetBrush(BackgroundBrush);
	Background->SetPadding(FMargin(14.f));
	DrawerSize->AddChild(Background);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CheatDrawerContent"));
	Background->AddChild(Content);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CheatDrawerHeader"));
	Content->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UTextBlock* Title = MakeDrawerText(*WidgetTree, TEXT("AEYERJI CHEATS"), 20);
	UAeyerjiUIStyleLibrary::StyleMissionHeading(Title);
	SetDrawerFontSize(Title, 20);
	if (UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* UnusedLabel = nullptr;
	UTextBlock* PinLabel = nullptr;
	UButton* PinButton = MakeDrawerButton(*WidgetTree, TEXT("Pinned: ON"), PinLabel);
	PinButtonLabel = PinLabel;
	PinButton->SetToolTipText(FText::FromString(TEXT("When unpinned, an action closes the drawer.")));
	PinButton->OnClicked.AddDynamic(this, &ThisClass::HandlePinClicked);
	Header->AddChildToHorizontalBox(PinButton)->SetPadding(FMargin(3.f, 0.f));
	UButton* CloseButton = MakeDrawerButton(*WidgetTree, TEXT("X"), UnusedLabel);
	CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	Header->AddChildToHorizontalBox(CloseButton)->SetPadding(FMargin(3.f, 0.f, 0.f, 0.f));

	Content->AddChildToVerticalBox(MakeDrawerSectionHeading(*WidgetTree, TEXT("PLAYER")))->SetPadding(FMargin(0.f, 5.f));
	UButton* HealButton = MakeTextButton(*Content, TEXT("Full Heal"), TEXT("Restore HP to the current HPMax."));
	HealButton->OnClicked.AddDynamic(this, &ThisClass::HandleFullHealClicked);

	UHorizontalBox* LevelRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CharacterLevelRow"));
	Content->AddChildToVerticalBox(LevelRow)->SetPadding(FMargin(0.f, 3.f));
	CharacterLevelSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("CharacterLevel"));
	CharacterLevelSpinBox->SetMinValue(1.f);
	CharacterLevelSpinBox->SetMaxValue(10000.f);
	CharacterLevelSpinBox->SetMinSliderValue(1.f);
	CharacterLevelSpinBox->SetMaxSliderValue(100.f);
	CharacterLevelSpinBox->SetDelta(1.f);
	CharacterLevelSpinBox->SetValue(1.f);
	LevelRow->AddChildToHorizontalBox(CharacterLevelSpinBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UTextBlock* SetLevelLabel = nullptr;
	UButton* SetLevelButton = MakeDrawerButton(*WidgetTree, TEXT("Set Character Level"), SetLevelLabel);
	SetLevelButton->OnClicked.AddDynamic(this, &ThisClass::HandleSetLevelClicked);
	LevelRow->AddChildToHorizontalBox(SetLevelButton)->SetPadding(FMargin(6.f, 0.f, 0.f, 0.f));

	UButton* SpeedButton = MakeTextButton(*Content, TEXT("Movement Speed x2"), TEXT("Doubles the original RunSpeed base without stacking repeated clicks."));
	SpeedButton->OnClicked.AddDynamic(this, &ThisClass::HandleSpeedDoubleClicked);
	UButton* SpeedResetButton = MakeTextButton(*Content, TEXT("Reset Movement Speed"));
	SpeedResetButton->OnClicked.AddDynamic(this, &ThisClass::HandleSpeedResetClicked);

	Content->AddChildToVerticalBox(MakeDrawerSectionHeading(*WidgetTree, TEXT("RIFT")))->SetPadding(FMargin(0.f, 10.f, 0.f, 5.f));
	UButton* RestartButton = MakeTextButton(*Content, TEXT("Restart Current Rift"), TEXT("Reloads the current gameplay map at its current entry zone."));
	RestartButton->OnClicked.AddDynamic(this, &ThisClass::HandleRestartRiftClicked);
	UButton* StartBalanceRunButton = MakeTextButton(*Content, TEXT("Start L1 Baseline Recording"), TEXT("Observes the normal Rift without changing or owning production enemies."));
	StartBalanceRunButton->OnClicked.AddDynamic(this, &ThisClass::HandleBalanceRunStartClicked);
	UButton* StartProgressionRunButton = MakeTextButton(*Content, TEXT("Start Natural Progression Recording"), TEXT("Tracks normal loot, equipment, XP, levels, live stats, healing, and enemy scaling through the Rift."));
	StartProgressionRunButton->OnClicked.AddDynamic(this, &ThisClass::HandleProgressionRunStartClicked);
	UButton* BalanceRunStatusButton = MakeTextButton(*Content, TEXT("Show Balance Run Status"));
	BalanceRunStatusButton->OnClicked.AddDynamic(this, &ThisClass::HandleBalanceRunStatusClicked);
	UButton* StopBalanceRunButton = MakeTextButton(*Content, TEXT("Stop + Save Balance Run"), TEXT("Writes AJBR reports under Saved/CombatTests and leaves normal enemies untouched."));
	StopBalanceRunButton->OnClicked.AddDynamic(this, &ThisClass::HandleBalanceRunStopClicked);

	Content->AddChildToVerticalBox(MakeDrawerSectionHeading(*WidgetTree, TEXT("ITEM PICKUPS")))->SetPadding(FMargin(0.f, 10.f, 0.f, 5.f));
	UHorizontalBox* ItemLevelRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ItemLevelRow"));
	Content->AddChildToVerticalBox(ItemLevelRow)->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
	ItemLevelRow->AddChildToHorizontalBox(MakeDrawerText(*WidgetTree, TEXT("Item level (0 = player): ")))->SetVerticalAlignment(VAlign_Center);
	ItemLevelSpinBox = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("ItemLevel"));
	ItemLevelSpinBox->SetMinValue(0.f);
	ItemLevelSpinBox->SetMaxValue(10000.f);
	ItemLevelSpinBox->SetMinSliderValue(0.f);
	ItemLevelSpinBox->SetMaxSliderValue(100.f);
	ItemLevelSpinBox->SetDelta(1.f);
	ItemLevelRow->AddChildToHorizontalBox(ItemLevelSpinBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	ItemSearchBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ItemSearch"));
	ItemSearchBox->SetHintText(FText::FromString(TEXT("Search item names or asset paths...")));
	ItemSearchBox->OnTextChanged.AddDynamic(this, &ThisClass::HandleItemSearchChanged);
	Content->AddChildToVerticalBox(ItemSearchBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	ItemList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ItemList"));
	ItemList->SetScrollBarVisibility(ESlateVisibility::Visible);
	if (UVerticalBoxSlot* ItemListSlot = Content->AddChildToVerticalBox(ItemList))
	{
		ItemListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
}

UButton* UW_AeyerjiCheatDrawer::MakeTextButton(UVerticalBox& Parent, const FString& Label, const FString& ToolTip)
{
	UTextBlock* LabelText = nullptr;
	UButton* Button = MakeDrawerButton(*WidgetTree, Label, LabelText);
	if (!ToolTip.IsEmpty())
	{
		Button->SetToolTipText(FText::FromString(ToolTip));
	}
	ConfigureActionSlot(Parent.AddChildToVerticalBox(Button));
	return Button;
}

void UW_AeyerjiCheatDrawer::RebuildItemList(const FString& SearchText)
{
	if (!ItemList)
	{
		return;
	}

	ItemList->ClearChildren();
	FARFilter Filter;
	Filter.ClassPaths.Add(UItemDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);

	struct FItemEntry
	{
		FString Label;
		FString SearchValue;
		FString ObjectPath;
	};
	TArray<FItemEntry> Entries;
	const FString NormalizedSearch = SearchText.TrimStartAndEnd().ToLower();
	for (const FAssetData& Asset : Assets)
	{
		UItemDefinition* Definition = Cast<UItemDefinition>(Asset.GetAsset());
		if (!Definition)
		{
			continue;
		}

		const FString DisplayName = Definition->DisplayName.IsEmpty() ? Asset.AssetName.ToString() : Definition->DisplayName.ToString();
		const FString ObjectPath = Asset.GetSoftObjectPath().ToString();
		const FString SearchValue = (DisplayName + TEXT(" ") + Asset.AssetName.ToString() + TEXT(" ") + ObjectPath).ToLower();
		if (!NormalizedSearch.IsEmpty() && !SearchValue.Contains(NormalizedSearch))
		{
			continue;
		}

		Entries.Add({FString::Printf(TEXT("%s  [Req %d]"), *DisplayName, Definition->GetEffectiveRequiredLevel()), SearchValue, ObjectPath});
	}

	Entries.Sort([](const FItemEntry& Left, const FItemEntry& Right)
	{
		return Left.Label < Right.Label;
	});

	for (const FItemEntry& Entry : Entries)
	{
		UAeyerjiCheatSpawnItemButton* Button = WidgetTree->ConstructWidget<UAeyerjiCheatSpawnItemButton>();
		Button->SetBackgroundColor(FLinearColor(0.085f, 0.018f, 0.15f, 0.96f));
		Button->SetToolTipText(FText::FromString(Entry.ObjectPath));
		Button->InitializeItemButton(this, Entry.ObjectPath);
		UTextBlock* Label = MakeDrawerText(*WidgetTree, Entry.Label, 13);
		UAeyerjiUIStyleLibrary::StyleMenuButtonLabel(Label);
		SetDrawerFontSize(Label, 13);
		Label->SetJustification(ETextJustify::Left);
		Button->AddChild(Label);
		ItemList->AddChild(Button);
	}

	if (Entries.IsEmpty())
	{
		ItemList->AddChild(MakeDrawerText(*WidgetTree, TEXT("No matching item definitions."), 13));
	}
}

void UW_AeyerjiCheatDrawer::RequestSpawnItem(const FString& ItemDefinitionPath)
{
	if (AeyerjiController)
	{
		AeyerjiController->AJ_SpawnItem(ItemDefinitionPath, GetSelectedItemLevel());
	}
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::MaybeCloseAfterAction()
{
	if (!bPinned)
	{
		RemoveFromParent();
	}
}

int32 UW_AeyerjiCheatDrawer::GetSelectedCharacterLevel() const
{
	return CharacterLevelSpinBox ? FMath::RoundToInt(CharacterLevelSpinBox->GetValue()) : 1;
}

int32 UW_AeyerjiCheatDrawer::GetSelectedItemLevel() const
{
	return ItemLevelSpinBox ? FMath::RoundToInt(ItemLevelSpinBox->GetValue()) : 0;
}

void UW_AeyerjiCheatDrawer::HandlePinClicked()
{
	bPinned = !bPinned;
	if (PinButtonLabel)
	{
		PinButtonLabel->SetText(FText::FromString(bPinned ? TEXT("Pinned: ON") : TEXT("Pinned: OFF")));
	}
}

void UW_AeyerjiCheatDrawer::HandleCloseClicked()
{
	RemoveFromParent();
}

void UW_AeyerjiCheatDrawer::HandleFullHealClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_FullHeal();
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleSetLevelClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_SetLevel(GetSelectedCharacterLevel());
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleSpeedDoubleClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_SetMoveSpeedMultiplier(2.f);
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleSpeedResetClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_SetMoveSpeedMultiplier(1.f);
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleRestartRiftClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_RestartRift();
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleBalanceRunStartClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_BalanceRunStart(TEXT("L1_Naked_Baseline"));
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleProgressionRunStartClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_BalanceRunStart(TEXT("L1_NaturalProgression"));
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleBalanceRunStatusClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_BalanceRunStatus();
}

void UW_AeyerjiCheatDrawer::HandleBalanceRunStopClicked()
{
	if (AeyerjiController) AeyerjiController->AJ_BalanceRunStop();
	MaybeCloseAfterAction();
}

void UW_AeyerjiCheatDrawer::HandleItemSearchChanged(const FText& SearchText)
{
	RebuildItemList(SearchText.ToString());
}

void UAeyerjiCheatSpawnItemButton::InitializeItemButton(UW_AeyerjiCheatDrawer* InDrawer, const FString& InItemDefinitionPath)
{
	Drawer = InDrawer;
	ItemDefinitionPath = InItemDefinitionPath;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleClicked);
}

void UAeyerjiCheatSpawnItemButton::HandleClicked()
{
	if (Drawer)
	{
		Drawer->RequestSpawnItem(ItemDefinitionPath);
	}
}
