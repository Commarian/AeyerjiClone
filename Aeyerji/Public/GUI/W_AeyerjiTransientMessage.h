// Copyright (c) 2025 Aeyerji.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "W_AeyerjiTransientMessage.generated.h"

class UBorder;
class UTextBlock;
class UWidgetAnimation;

/** Shared presentation shell for short-lived toast and error messages. */
UCLASS(Abstract, Blueprintable)
class AEYERJI_API UW_AeyerjiTransientMessage : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Updates the message and restarts the widget-owned entrance animation. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Presentation")
	void PresentMessage(const FText& Message);

	/** Queues HUD feedback; this presenter owns entrance, hold and exit timing. */
	void EnqueueMessage(const FText& Message, float HoldSeconds);

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	void ResolveMessageWidgets();
	void RestartEntrance();
	FText ReadObservedMessage() const;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
	UBorder* MessageSurface = nullptr;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly)
	UTextBlock* MessageText = nullptr;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional), BlueprintReadOnly)
	UWidgetAnimation* MessageIn = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Presentation")
	bool bErrorPresentation = false;

private:
	struct FQueuedMessage
	{
		FText Text;
		float HoldSeconds = 2.f;
	};
	enum class EQueuePhase : uint8 { Idle, Entering, Holding, Exiting };
	void StartNextMessage();
	void TickMessageQueue(float DeltaSeconds);
	TArray<FQueuedMessage> PendingMessages;
	FQueuedMessage ActiveMessage;
	EQueuePhase QueuePhase = EQueuePhase::Idle;
	float PhaseElapsed = 0.f;
	float EntranceSeconds = 0.28f;
	bool bQueueOwnsPresentation = false;

	FText LastObservedMessage;
	float LastObservedRenderOpacity = 1.f;
	bool bHasObservedMessage = false;
};
