// ───────────────────────────── UW_ActionSlotNative.cpp ─────────────────────
#include "GUI/W_ActionSlotNative.h"
#include "Blueprint/WidgetBlueprintLibrary.h"        // for DetectDragIfPressed
#include "Components/ProgressBar.h"
#include "GUI/W_ActionBar.h"
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
	constexpr float MaxCooldownSeconds = 604800.f;
	CooldownTotalTime = FMath::Clamp(FMath::IsFinite(TotalDuration) ? TotalDuration : 0.f, 0.f, MaxCooldownSeconds);
	CooldownTimeRemaining = FMath::Clamp(FMath::IsFinite(TimeRemaining) ? TimeRemaining : 0.f, 0.f, CooldownTotalTime);

	const bool bHasActiveCooldown = CooldownTotalTime > KINDA_SMALL_NUMBER && CooldownTimeRemaining > KINDA_SMALL_NUMBER;
	bIsCoolingDown = bHasActiveCooldown;

	CooldownPercent = bHasActiveCooldown && CooldownTotalTime > KINDA_SMALL_NUMBER
		? FMath::Clamp(CooldownTimeRemaining / CooldownTotalTime, 0.f, 1.f)
		: 0.f;

	CooldownDisplaySeconds = bHasActiveCooldown
		? FMath::Max(1, FMath::RoundToInt(FMath::Min(CooldownTimeRemaining, MaxCooldownSeconds)))
		: 0;

	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownTotalTime);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownTimeRemaining);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::bIsCoolingDown);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownPercent);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownDisplaySeconds);

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

	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownPercent);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownTimeRemaining);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownTotalTime);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::bIsCoolingDown);
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::CooldownDisplaySeconds);

	if (CooldownProgress)
	{
		CooldownProgress->SetPercent(0.f);
	}
}

FReply UW_ActionSlotNative::NativeOnMouseButtonDown(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent)
{
	if (!bIsPotionSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
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

FReply UW_ActionSlotNative::NativeOnMouseButtonDoubleClick(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (!bIsPotionSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlotRightClicked.Broadcast(StoredSlotIndex);
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotLeftClicked.Broadcast(this);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

FReply UW_ActionSlotNative::NativeOnMouseButtonUp(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}

	if (!bIsPotionSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UW_ActionSlotNative::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	if (StoredSlotData.Tag.IsEmpty())
	{
		return;
	}

	if (UW_ActionBar* OwningBar = GetTypedOuter<UW_ActionBar>())
	{
		OwningBar->ShowAbilityTooltip(StoredSlotData, InMouseEvent.GetScreenSpacePosition(), this);
	}
}

void UW_ActionSlotNative::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	if (UW_ActionBar* OwningBar = GetTypedOuter<UW_ActionBar>())
	{
		OwningBar->HideAbilityTooltip(this);
	}

	Super::NativeOnMouseLeave(InMouseEvent);
}
