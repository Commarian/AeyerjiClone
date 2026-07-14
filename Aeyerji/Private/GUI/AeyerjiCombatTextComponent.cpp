#include "GUI/AeyerjiCombatTextComponent.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "GUI/W_AeyerjiCombatText.h"
#include "GameFramework/PlayerController.h"

UAeyerjiCombatTextComponent::UAeyerjiCombatTextComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	WidgetClass = UW_AeyerjiCombatText::StaticClass();
}

UAeyerjiCombatTextComponent* UAeyerjiCombatTextComponent::GetOrCreateForPlayerController(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}

	if (UAeyerjiCombatTextComponent* Existing = PlayerController->FindComponentByClass<UAeyerjiCombatTextComponent>())
	{
		return Existing;
	}

	UAeyerjiCombatTextComponent* Component = NewObject<UAeyerjiCombatTextComponent>(
		PlayerController,
		UAeyerjiCombatTextComponent::StaticClass(),
		TEXT("CombatText"));
	if (!Component)
	{
		return nullptr;
	}

	Component->RegisterComponent();
	return Component;
}

bool UAeyerjiCombatTextComponent::ShowCombatText(
	AActor* TargetActor,
	const FText& Text,
	const FLinearColor Color,
	const float Scale,
	const EAeyerjiCombatTextResultType ResultType,
	const float Magnitude)
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC || !IsValid(TargetActor) || Text.IsEmpty())
	{
		return false;
	}

	TSubclassOf<UW_AeyerjiCombatText> ResolvedWidgetClass = WidgetClass;
	if (!*ResolvedWidgetClass)
	{
		ResolvedWidgetClass = UW_AeyerjiCombatText::StaticClass();
	}

	UW_AeyerjiCombatText* Widget = CreateWidget<UW_AeyerjiCombatText>(PC, ResolvedWidgetClass);
	if (!Widget)
	{
		return false;
	}

	Widget->ApplyCombatText(Text, Color, Scale, ResultType, Magnitude);
	Widget->AddToViewport(ZOrder);
	Widget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	Widget->SetVisibility(ESlateVisibility::HitTestInvisible);

	const int32 StackIndex = CountActiveTextsForTarget(TargetActor);
	const float Direction = (StackIndex % 2 == 0) ? 1.f : -1.f;
	const float HorizontalOffset = Direction * HorizontalSpreadPixels * FMath::Min(StackIndex + 1, 3) / 3.f;

	FActiveCombatText Entry;
	Entry.TargetActor = TargetActor;
	Entry.Widget = Widget;
	Entry.WorldOffset = WorldOffset;
	Entry.InitialScreenOffset = ScreenOffset + FVector2D(HorizontalOffset, -14.f * StackIndex);
	Entry.LifetimeSeconds = FMath::Max(0.05f, LifetimeSeconds);
	Entry.FloatDistancePixels = FloatDistancePixels;
	ActiveTexts.Add(Entry);

	TrimToMaxEntries();
	SetComponentTickEnabled(true);
	return true;
}

void UAeyerjiCombatTextComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FActiveCombatText& Entry : ActiveTexts)
	{
		if (UW_AeyerjiCombatText* Widget = Entry.Widget.Get())
		{
			Widget->RemoveFromParent();
		}
	}
	ActiveTexts.Empty();

	Super::EndPlay(EndPlayReason);
}

void UAeyerjiCombatTextComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		SetComponentTickEnabled(false);
		return;
	}

	for (int32 Index = ActiveTexts.Num() - 1; Index >= 0; --Index)
	{
		FActiveCombatText& Entry = ActiveTexts[Index];
		UW_AeyerjiCombatText* Widget = Entry.Widget.Get();
		if (!Widget)
		{
			ActiveTexts.RemoveAtSwap(Index);
			continue;
		}

		Entry.AgeSeconds += DeltaTime;
		const float Alpha = Entry.LifetimeSeconds > 0.f
			? FMath::Clamp(Entry.AgeSeconds / Entry.LifetimeSeconds, 0.f, 1.f)
			: 1.f;
		if (Alpha >= 1.f)
		{
			RemoveActiveTextAt(Index);
			continue;
		}

		bool bCanShow = false;
		FVector2D ScreenPosition = Entry.LastScreenPosition;
		if (AActor* TargetActor = Entry.TargetActor.Get())
		{
			if (MaxDrawDistance > 0.f)
			{
				FVector ViewLocation;
				FRotator ViewRotation;
				PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
				if (FVector::DistSquared(ViewLocation, TargetActor->GetActorLocation()) > FMath::Square(MaxDrawDistance))
				{
					Widget->SetVisibility(ESlateVisibility::Hidden);
					continue;
				}
			}

			const FVector WorldLocation = TargetActor->GetActorLocation() + Entry.WorldOffset;
			if (ProjectToScreen(WorldLocation, ScreenPosition))
			{
				Entry.LastScreenPosition = ScreenPosition;
				Entry.bHasLastScreenPosition = true;
				bCanShow = true;
			}
		}
		else if (Entry.bHasLastScreenPosition)
		{
			bCanShow = true;
		}

		if (!bCanShow)
		{
			Widget->SetVisibility(ESlateVisibility::Hidden);
			continue;
		}

		const float EaseOut = 1.f - FMath::Square(1.f - Alpha);
		const FVector2D WantedPosition = ScreenPosition
			+ Entry.InitialScreenOffset
			+ FVector2D(0.f, -Entry.FloatDistancePixels * EaseOut);

		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		Widget->SetRenderOpacity(1.f - Alpha);
		Widget->SetPositionInViewport(WantedPosition, /*bRemoveDPIScale=*/false);
		Widget->NotifyCombatTextTick(Alpha);
	}

	if (ActiveTexts.IsEmpty())
	{
		SetComponentTickEnabled(false);
	}
}

APlayerController* UAeyerjiCombatTextComponent::GetLocalPlayerController() const
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC && PC->IsLocalController() ? PC : nullptr;
}

bool UAeyerjiCombatTextComponent::ProjectToScreen(const FVector& WorldLocation, FVector2D& OutPosition) const
{
	APlayerController* PC = GetLocalPlayerController();
	if (!PC)
	{
		return false;
	}

	return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		PC,
		WorldLocation,
		OutPosition,
		/*bPlayerViewportRelative=*/true);
}

int32 UAeyerjiCombatTextComponent::CountActiveTextsForTarget(const AActor* TargetActor) const
{
	int32 Count = 0;
	for (const FActiveCombatText& Entry : ActiveTexts)
	{
		if (Entry.TargetActor.Get() == TargetActor)
		{
			++Count;
		}
	}
	return Count;
}

void UAeyerjiCombatTextComponent::RemoveActiveTextAt(const int32 Index)
{
	if (!ActiveTexts.IsValidIndex(Index))
	{
		return;
	}

	if (UW_AeyerjiCombatText* Widget = ActiveTexts[Index].Widget.Get())
	{
		Widget->RemoveFromParent();
	}
	ActiveTexts.RemoveAtSwap(Index);
}

void UAeyerjiCombatTextComponent::TrimToMaxEntries()
{
	const int32 ClampedMax = FMath::Max(1, MaxActiveEntries);
	while (ActiveTexts.Num() > ClampedMax)
	{
		RemoveActiveTextAt(0);
	}
}
