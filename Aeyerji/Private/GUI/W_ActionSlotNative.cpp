// ───────────────────────────── UW_ActionSlotNative.cpp ─────────────────────
#include "GUI/W_ActionSlotNative.h"
#include "Blueprint/WidgetBlueprintLibrary.h"        // for DetectDragIfPressed
#include "Components/ProgressBar.h"
#include "Input/Events.h"
#include "Logging/AeyerjiLog.h"

UW_ActionSlotNative::UW_ActionSlotNative(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)                     // ← always call Super
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> Dummy(
		TEXT("/Game/Abilities/AG_Locked_Ability"));      // your placeholder
	if (Dummy.Succeeded())
	{
		PlaceholderIcon = Dummy.Object;
	}
}
void UW_ActionSlotNative::NativeConstruct()
{
	Super::NativeConstruct();

	ClearCooldownDisplay();

	/* Initialise slot with placeholder icon */
	if (Icon && PlaceholderIcon)
	{
		Icon->SetBrushFromTexture(PlaceholderIcon, false);
	}
}

void UW_ActionSlotNative::UpdateCooldownDisplay(float TimeRemaining, float TotalDuration)
{
	CooldownTotalTime = FMath::Max(0.f, TotalDuration);
	CooldownTimeRemaining = FMath::Max(0.f, TimeRemaining);

	const bool bHasActiveCooldown = CooldownTotalTime > KINDA_SMALL_NUMBER && CooldownTimeRemaining > KINDA_SMALL_NUMBER;
	bIsCoolingDown = bHasActiveCooldown;

	CooldownPercent = bHasActiveCooldown && CooldownTotalTime > KINDA_SMALL_NUMBER
		? FMath::Clamp(CooldownTimeRemaining / CooldownTotalTime, 0.f, 1.f)
		: 0.f;

	CooldownDisplaySeconds = bHasActiveCooldown
		? FMath::Max(1, FMath::RoundToInt(CooldownTimeRemaining))
		: 0;

	// AJ_LOG(this, TEXT("UpdateCooldownDisplay SlotIndex=%d Remaining=%.2f Total=%.2f Percent=%.2f DisplaySeconds=%d"),
	// 	StoredSlotIndex,
	// 	CooldownTimeRemaining,
	// 	CooldownTotalTime,
	// 	CooldownPercent,
	// 	CooldownDisplaySeconds);

	if (CooldownProgress)
	{
		CooldownProgress->SetPercent(CooldownPercent);
	}
}

void UW_ActionSlotNative::ClearCooldownDisplay()
{
	//AJ_LOG(this, TEXT("ClearCooldownDisplay SlotIndex=%d"), StoredSlotIndex);
	CooldownPercent = 0.f;
	CooldownTimeRemaining = 0.f;
	CooldownTotalTime = 0.f;
	bIsCoolingDown = false;
	CooldownDisplaySeconds = 0;

	if (CooldownProgress)
	{
		CooldownProgress->SetPercent(0.f);
	}
}

FReply UW_ActionSlotNative::NativeOnMouseButtonDown(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent)
{
	AJ_LOG(this, TEXT("UW_ActionSlotNative::NativeOnMouseButtonDown"));
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlotRightClicked.Broadcast(StoredSlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotLeftClicked.Broadcast(this);
		return FReply::Handled();
	}

	/* Optional: Detect drag with left mouse
	   UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton); */
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
