// Copyright (c) 2025 Aeyerji.

#include "GUI/W_AeyerjiTransientMessage.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GUI/AeyerjiUIStyleLibrary.h"
#include "UObject/UnrealType.h"

void UW_AeyerjiTransientMessage::NativePreConstruct()
{
	Super::NativePreConstruct();
	ResolveMessageWidgets();
	UAeyerjiUIStyleLibrary::StyleTransientMessage(MessageSurface, MessageText, bErrorPresentation);
}

void UW_AeyerjiTransientMessage::NativeConstruct()
{
	Super::NativeConstruct();
	ResolveMessageWidgets();
	LastObservedMessage = ReadObservedMessage();
	LastObservedRenderOpacity = GetRenderOpacity();
	bHasObservedMessage = true;
}

void UW_AeyerjiTransientMessage::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bQueueOwnsPresentation)
	{
		TickMessageQueue(InDeltaTime);
		return;
	}

	if (bErrorPresentation)
	{
		return;
	}

	const FText CurrentMessage = ReadObservedMessage();
	const float CurrentOpacity = GetRenderOpacity();
	const bool bMessageChanged = bHasObservedMessage && !CurrentMessage.EqualTo(LastObservedMessage);
	const bool bBecameVisible = LastObservedRenderOpacity < 0.99f && CurrentOpacity >= 0.99f;

	if ((!CurrentMessage.IsEmpty() && bMessageChanged) || bBecameVisible)
	{
		RestartEntrance();
	}

	LastObservedMessage = CurrentMessage;
	LastObservedRenderOpacity = CurrentOpacity;
	bHasObservedMessage = true;
}

void UW_AeyerjiTransientMessage::NativeDestruct()
{
	PendingMessages.Reset();
	ActiveMessage = FQueuedMessage();
	QueuePhase = EQueuePhase::Idle;
	bQueueOwnsPresentation = false;
	StopAnimation(MessageIn);
	Super::NativeDestruct();
}

void UW_AeyerjiTransientMessage::EnqueueMessage(const FText& Message, const float HoldSeconds)
{
	if (Message.IsEmpty()) return;
	bQueueOwnsPresentation = true;
	// Repeated held-input failures must not extend the active toast indefinitely.
	if (QueuePhase != EQueuePhase::Idle && ActiveMessage.Text.EqualTo(Message)) return;
	for (const FQueuedMessage& Pending : PendingMessages)
	{
		if (Pending.Text.EqualTo(Message)) return;
	}
	// Keep recent feedback bounded without interrupting the message being read.
	if (PendingMessages.Num() >= 4) PendingMessages.RemoveAt(0);
	PendingMessages.Add({Message, FMath::IsFinite(HoldSeconds) ? FMath::Clamp(HoldSeconds, 0.05f, 10.f) : 2.f});
	if (QueuePhase == EQueuePhase::Idle) StartNextMessage();
}

void UW_AeyerjiTransientMessage::StartNextMessage()
{
	if (PendingMessages.IsEmpty())
	{
		QueuePhase = EQueuePhase::Idle;
		SetRenderOpacity(0.f);
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	ActiveMessage = PendingMessages[0];
	PendingMessages.RemoveAt(0);
	PhaseElapsed = 0.f;
	QueuePhase = EQueuePhase::Entering;
	EntranceSeconds = UAeyerjiUIStyleLibrary::MessageEntranceSeconds;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetRenderOpacity(MessageIn ? 1.f : 0.f);
	PresentMessage(ActiveMessage.Text);
}

void UW_AeyerjiTransientMessage::TickMessageQueue(const float DeltaSeconds)
{
	if (QueuePhase == EQueuePhase::Idle) return;
	PhaseElapsed += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
	if (QueuePhase == EQueuePhase::Entering)
	{
		if (!MessageIn) SetRenderOpacity(UAeyerjiUIStyleLibrary::EasePresentation(PhaseElapsed / EntranceSeconds));
		if (PhaseElapsed >= EntranceSeconds)
		{
			QueuePhase = EQueuePhase::Holding;
			PhaseElapsed = 0.f;
		}
	}
	else if (QueuePhase == EQueuePhase::Holding && PhaseElapsed >= ActiveMessage.HoldSeconds)
	{
		QueuePhase = EQueuePhase::Exiting;
		PhaseElapsed = 0.f;
		// Reverse the existing authored entrance for a matching 0.2-second exit.
		if (MessageIn) PlayAnimation(MessageIn, 0.f, 1, EUMGSequencePlayMode::Reverse,
			FMath::Max(0.01f, MessageIn->GetEndTime() - MessageIn->GetStartTime()) / UAeyerjiUIStyleLibrary::MessageExitSeconds);
	}
	else if (QueuePhase == EQueuePhase::Exiting)
	{
		if (!MessageIn) SetRenderOpacity(1.f - UAeyerjiUIStyleLibrary::EasePresentation(PhaseElapsed / UAeyerjiUIStyleLibrary::MessageExitSeconds));
		if (PhaseElapsed >= UAeyerjiUIStyleLibrary::MessageExitSeconds)
		{
			if (MessageIn) StopAnimation(MessageIn);
			StartNextMessage();
		}
	}
}

void UW_AeyerjiTransientMessage::PresentMessage(const FText& Message)
{
	ResolveMessageWidgets();

	if (MessageText)
	{
		MessageText->SetText(Message);
	}

	if (MessageIn)
	{
		RestartEntrance();
	}
}

void UW_AeyerjiTransientMessage::RestartEntrance()
{
	if (!MessageIn)
	{
		return;
	}

	StopAnimation(MessageIn);
	const float PlaybackSpeed = bQueueOwnsPresentation
		? FMath::Max(0.01f, MessageIn->GetEndTime() - MessageIn->GetStartTime()) / UAeyerjiUIStyleLibrary::MessageEntranceSeconds
		: 1.f;
	PlayAnimation(MessageIn, 0.f, 1, EUMGSequencePlayMode::Forward, PlaybackSpeed);
}

FText UW_AeyerjiTransientMessage::ReadObservedMessage() const
{
	// WBP_ToastMessage already exposes ToastMessage as its presentation input.
	// Reading it keeps the owning HUD's established timer and fade graph intact.
	if (const FTextProperty* ToastMessageProperty = FindFProperty<FTextProperty>(GetClass(), TEXT("ToastMessage")))
	{
		return ToastMessageProperty->GetPropertyValue_InContainer(this);
	}

	return MessageText ? MessageText->GetText() : FText::GetEmpty();
}

void UW_AeyerjiTransientMessage::ResolveMessageWidgets()
{
	if (!WidgetTree)
	{
		return;
	}

	// These names predate the shared native shell. Resolving them here lets the
	// existing assets retain their animation bindings and serialized layout.
	if (!MessageSurface)
	{
		MessageSurface = Cast<UBorder>(WidgetTree->FindWidget(TEXT("Border_22")));
		if (!MessageSurface)
		{
			MessageSurface = Cast<UBorder>(WidgetTree->FindWidget(TEXT("Border_0")));
		}
	}

	if (!MessageText)
	{
		MessageText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TextBlock_51")));
		if (!MessageText)
		{
			MessageText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("TextBlock_445")));
		}
	}
}
