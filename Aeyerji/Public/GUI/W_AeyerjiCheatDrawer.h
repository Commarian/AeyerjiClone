#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"

#include "W_AeyerjiCheatDrawer.generated.h"

class AAeyerjiPlayerController;
class UEditableTextBox;
class UScrollBox;
class USpinBox;
class UTextBlock;
class UVerticalBox;

/** Clickable development cheats kept local while gameplay mutations remain server-authoritative. */
UCLASS()
class AEYERJI_API UW_AeyerjiCheatDrawer : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Supplies the local Aeyerji controller that owns this drawer. */
	void InitializeForController(AAeyerjiPlayerController* InController);

	/** Routes an item row through the controller's authoritative pickup-spawn command. */
	void RequestSpawnItem(const FString& ItemDefinitionPath);

private:
	float EntranceElapsed = 0.f;
	FWidgetTransform RestTransform;
	float RestOpacity = 1.f;
	void BuildNativeLayout();
	void RebuildItemList(const FString& SearchText);
	UButton* MakeTextButton(UVerticalBox& Parent, const FString& Label, const FString& ToolTip = FString());
	void MaybeCloseAfterAction();
	int32 GetSelectedCharacterLevel() const;
	int32 GetSelectedItemLevel() const;

	UFUNCTION()
	void HandlePinClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UFUNCTION()
	void HandleFullHealClicked();

	UFUNCTION()
	void HandleSetLevelClicked();

	UFUNCTION()
	void HandleSpeedDoubleClicked();

	UFUNCTION()
	void HandleSpeedResetClicked();

	UFUNCTION()
	void HandleRestartRiftClicked();

	UFUNCTION()
	void HandleBalanceRunStartClicked();

	UFUNCTION()
	void HandleProgressionRunStartClicked();

	UFUNCTION()
	void HandleBalanceRunStatusClicked();

	UFUNCTION()
	void HandleBalanceRunStopClicked();

	UFUNCTION()
	void HandleItemSearchChanged(const FText& SearchText);

	UPROPERTY(Transient)
	TObjectPtr<AAeyerjiPlayerController> AeyerjiController = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PinButtonLabel = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USpinBox> CharacterLevelSpinBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USpinBox> ItemLevelSpinBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> ItemSearchBox = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ItemList = nullptr;

	bool bPinned = true;
};

/** Item-list button that retains the asset path represented by its row. */
UCLASS()
class AEYERJI_API UAeyerjiCheatSpawnItemButton : public UButton
{
	GENERATED_BODY()

public:
	void InitializeItemButton(UW_AeyerjiCheatDrawer* InDrawer, const FString& InItemDefinitionPath);

private:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY(Transient)
	TObjectPtr<UW_AeyerjiCheatDrawer> Drawer = nullptr;

	FString ItemDefinitionPath;
};
