// Copyright (c) 2025 Aeyerji.

#include "GUI/W_AeyerjiMinimap.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/SizeBox.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Player/PlayerParentNative.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "World/AeyerjiSurvivalDefenseObjectiveActor.h"

namespace AeyerjiMinimapPrivate
{
	constexpr int32 CircleSegments = 48;

	FVector2f ToVector2f(const FVector2D& Value)
	{
		return FVector2f(static_cast<float>(Value.X), static_cast<float>(Value.Y));
	}

	void DrawLine(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 LayerId,
		const FVector2f& Start,
		const FVector2f& End,
		const FLinearColor& Color,
		const float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(Start);
		Points.Add(End);
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(),
			MoveTemp(Points),
			ESlateDrawEffect::None,
			Color,
			true,
			Thickness);
	}

	void DrawClosedShape(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 LayerId,
		TArray<FVector2f> Points,
		const FLinearColor& Color,
		const float Thickness)
	{
		if (Points.Num() > 1)
		{
			// Copy before Add: the append can reallocate Points and invalidate a reference to Points[0].
			const FVector2f FirstPoint = Points[0];
			Points.Add(FirstPoint);
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(),
			MoveTemp(Points),
			ESlateDrawEffect::None,
			Color,
			true,
			Thickness);
	}

	void DrawCircle(
		FSlateWindowElementList& OutDrawElements,
		const FGeometry& Geometry,
		const int32 LayerId,
		const FVector2f& Center,
		const float Radius,
		const FLinearColor& Color,
		const float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(CircleSegments + 1);
		for (int32 SegmentIndex = 0; SegmentIndex <= CircleSegments; ++SegmentIndex)
		{
			const float Angle = UE_TWO_PI * static_cast<float>(SegmentIndex) / static_cast<float>(CircleSegments);
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId,
			Geometry.ToPaintGeometry(),
			MoveTemp(Points),
			ESlateDrawEffect::None,
			Color,
			true,
			Thickness);
	}
}

TSharedRef<SWidget> UW_AeyerjiMinimap::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* GeneratedRoot = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("GeneratedMinimapRoot"));
		GeneratedRoot->SetWidthOverride(DisplaySize.X);
		GeneratedRoot->SetHeightOverride(DisplaySize.Y);
		WidgetTree->RootWidget = GeneratedRoot;
	}

	return Super::RebuildWidget();
}

void UW_AeyerjiMinimap::NativeConstruct()
{
	Super::NativeConstruct();

	// The native fallback owns its viewport placement and never blocks Diablo-style mouse commands.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ForceVolatile(true);
	// Position and size setters reset the viewport anchors, so anchor the slot only after both calls.
	SetDesiredSizeInViewport(DisplaySize);
	SetPositionInViewport(FVector2D(-ViewportEdgePadding.X, ViewportEdgePadding.Y), false);
	SetAnchorsInViewport(FAnchors(1.f, 0.f));
	SetAlignmentInViewport(FVector2D(1.f, 0.f));

	RefreshMarkers();
	SecondsUntilMarkerRefresh = FMath::Max(0.05f, MarkerRefreshInterval);
}

void UW_AeyerjiMinimap::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	SecondsUntilMarkerRefresh -= InDeltaTime;
	if (SecondsUntilMarkerRefresh <= 0.f)
	{
		RefreshMarkers();
		SecondsUntilMarkerRefresh = FMath::Max(0.05f, MarkerRefreshInterval);
	}

	Invalidate(EInvalidateWidgetReason::Paint);
}

void UW_AeyerjiMinimap::RefreshMarkers()
{
	TrackedMarkers.Reset();

	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwningPlayerPawn();
	if (!World || !IsValid(PlayerActor))
	{
		return;
	}

	TSet<AActor*> AddedActors;
	AddedActors.Add(PlayerActor);

	auto AddMarker = [this, &AddedActors](
		AActor* Actor,
		const EAeyerjiMinimapMarkerType MarkerType,
		const bool bClampToEdge,
		const UAeyerjiMinimapMarkerComponent* Component)
	{
		if (!IsValid(Actor) || AddedActors.Contains(Actor))
		{
			return;
		}

		FAeyerjiTrackedMinimapMarker& Marker = TrackedMarkers.AddDefaulted_GetRef();
		Marker.Actor = Actor;
		Marker.MarkerType = MarkerType;
		Marker.bClampToEdge = bClampToEdge;
		if (Component)
		{
			Marker.IconTexture = Component->IconTexture;
			Marker.CustomColor = Component->CustomColor;
			Marker.MarkerSize = Component->MarkerSize;
			Marker.bClampToEdge = Component->bClampToEdge;
			Marker.bShowWhenOwnerDead = Component->bShowWhenOwnerDead;
			Marker.bRotateWithOwner = Component->bRotateWithOwner;
			Marker.bUseCustomColor = Component->bUseCustomColor;
		}

		AddedActors.Add(Actor);
	};

	// One world pass keeps discovery bounded, while an explicit component always overrides class defaults.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (const UAeyerjiMinimapMarkerComponent* Component = Actor->FindComponentByClass<UAeyerjiMinimapMarkerComponent>())
		{
			if (Component->IsRegistered() && Component->bVisibleOnMinimap)
			{
				AddMarker(Actor, Component->MarkerType, Component->bClampToEdge, Component);
			}

			// A disabled explicit marker intentionally suppresses automatic discovery for this actor.
			continue;
		}

		if (bAutoDiscoverPlayers && Actor->IsA<APlayerParentNative>())
		{
			AddMarker(Actor, EAeyerjiMinimapMarkerType::Friendly, false, nullptr);
			continue;
		}

		if (bAutoDiscoverEnemies && Actor->IsA<AEnemyParentNative>())
		{
			AddMarker(Actor, EAeyerjiMinimapMarkerType::Enemy, false, nullptr);
			continue;
		}

		if (bAutoDiscoverObjectives && Actor->IsA<AAeyerjiSurvivalDefenseObjectiveActor>())
		{
			AddMarker(Actor, EAeyerjiMinimapMarkerType::Objective, true, nullptr);
		}
	}
}

bool UW_AeyerjiMinimap::IsMarkerTypeEnabled(const EAeyerjiMinimapMarkerType MarkerType) const
{
	switch (MarkerType)
	{
	case EAeyerjiMinimapMarkerType::Friendly:
		return bShowFriendlies;
	case EAeyerjiMinimapMarkerType::Enemy:
		return bShowEnemies;
	case EAeyerjiMinimapMarkerType::Objective:
		return bShowObjectives;
	case EAeyerjiMinimapMarkerType::PointOfInterest:
		return bShowPointsOfInterest;
	default:
		return false;
	}
}

bool UW_AeyerjiMinimap::IsMarkerActorVisible(const FAeyerjiTrackedMinimapMarker& Marker) const
{
	const AActor* Actor = Marker.Actor.Get();
	if (!IsValid(Actor) || Actor->IsHidden())
	{
		return false;
	}

	if (const AAeyerjiSurvivalDefenseObjectiveActor* Objective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(Actor))
	{
		if (Objective->IsObjectiveDestroyed() && !Marker.bShowWhenOwnerDead)
		{
			return false;
		}
	}

	if (!Marker.bShowWhenOwnerDead)
	{
		if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true))
		{
			if (ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
			{
				return false;
			}
		}

		if (Actor->ActorHasTag(AeyerjiTags::State_Dead.GetTag().GetTagName()))
		{
			return false;
		}
	}

	return IsMarkerTypeEnabled(Marker.MarkerType);
}

FLinearColor UW_AeyerjiMinimap::ResolveMarkerColor(const FAeyerjiTrackedMinimapMarker& Marker) const
{
	if (Marker.bUseCustomColor)
	{
		return Marker.CustomColor;
	}

	switch (Marker.MarkerType)
	{
	case EAeyerjiMinimapMarkerType::Friendly:
		return FriendlyColor;
	case EAeyerjiMinimapMarkerType::Enemy:
		return EnemyColor;
	case EAeyerjiMinimapMarkerType::Objective:
		return ObjectiveColor;
	case EAeyerjiMinimapMarkerType::PointOfInterest:
		return PointOfInterestColor;
	default:
		return FLinearColor::White;
	}
}

float UW_AeyerjiMinimap::ResolveMarkerSize(const FAeyerjiTrackedMinimapMarker& Marker) const
{
	if (Marker.MarkerSize > 0.f)
	{
		return Marker.MarkerSize;
	}

	return Marker.MarkerType == EAeyerjiMinimapMarkerType::Objective
		? ObjectiveMarkerSize
		: DefaultMarkerSize;
}

bool UW_AeyerjiMinimap::ProjectMarkerToMap(
	const AActor& MarkerActor,
	const AActor& PlayerActor,
	const FVector2f& Center,
	const float DrawRadius,
	const bool bClampToEdge,
	FVector2f& OutPosition,
	bool& bOutAtEdge) const
{
	const FVector RelativeLocation = MarkerActor.GetActorLocation() - PlayerActor.GetActorLocation();
	const FVector2f WorldOffset(static_cast<float>(RelativeLocation.X), static_cast<float>(RelativeLocation.Y));
	const float Distance = WorldOffset.Size();
	const float SafeWorldRadius = FMath::Max(100.f, MapWorldRadius);
	if (Distance > SafeWorldRadius && !bClampToEdge)
	{
		return false;
	}

	FVector2f MapDirection;
	if (Distance > UE_KINDA_SMALL_NUMBER)
	{
		const FVector2f NormalizedWorldOffset = WorldOffset / Distance;
		if (bRotateWithPlayer)
		{
			const float InverseYawRadians = FMath::DegreesToRadians(-PlayerActor.GetActorRotation().Yaw);
			const float CosYaw = FMath::Cos(InverseYawRadians);
			const float SinYaw = FMath::Sin(InverseYawRadians);
			const float LocalForward = CosYaw * NormalizedWorldOffset.X - SinYaw * NormalizedWorldOffset.Y;
			const float LocalRight = SinYaw * NormalizedWorldOffset.X + CosYaw * NormalizedWorldOffset.Y;
			MapDirection = FVector2f(LocalRight, -LocalForward);
		}
		else
		{
			MapDirection = FVector2f(NormalizedWorldOffset.Y, -NormalizedWorldOffset.X);
		}
	}
	else
	{
		MapDirection = FVector2f::ZeroVector;
	}

	bOutAtEdge = Distance > SafeWorldRadius;
	const float NormalizedDistance = bOutAtEdge ? 1.f : FMath::Clamp(Distance / SafeWorldRadius, 0.f, 1.f);
	OutPosition = Center + MapDirection * (DrawRadius * NormalizedDistance);
	return true;
}

int32 UW_AeyerjiMinimap::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	int32 DrawLayer = Super::NativePaint(
		Args,
		AllottedGeometry,
		MyCullingRect,
		OutDrawElements,
		LayerId,
		InWidgetStyle,
		bParentEnabled);

	const FVector2f LocalSize = AeyerjiMinimapPrivate::ToVector2f(AllottedGeometry.GetLocalSize());
	const float PanelDiameter = FMath::Min(LocalSize.X, LocalSize.Y);
	if (PanelDiameter <= 1.f)
	{
		return DrawLayer;
	}

	const FVector2f Center = LocalSize * 0.5f;
	const FVector2f PanelSize(PanelDiameter, PanelDiameter);
	const FVector2f PanelPosition = Center - PanelSize * 0.5f;
	const float PanelRadius = PanelDiameter * 0.5f;
	const float DrawRadius = FMath::Max(1.f, PanelRadius - FMath::Max(0.f, MarkerEdgePadding));
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor StyledGridColor = GridColor * WidgetTint;
	const FLinearColor StyledFrameColor = FrameColor * WidgetTint;
	const FLinearColor StyledPlayerColor = PlayerColor * WidgetTint;

	const FSlateRoundedBoxBrush PanelBrush(BackgroundColor, FrameColor, LineThickness, PanelSize);
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++DrawLayer,
		AllottedGeometry.ToPaintGeometry(PanelSize, FSlateLayoutTransform(PanelPosition)),
		&PanelBrush,
		ESlateDrawEffect::None,
		WidgetTint);

	// Procedural grid chords stay within the circular frame and provide useful scale without authored map art.
	for (const float OffsetFraction : { -0.5f, 0.f, 0.5f })
	{
		const float Offset = DrawRadius * OffsetFraction;
		const float HalfChord = FMath::Sqrt(FMath::Max(0.f, FMath::Square(DrawRadius) - FMath::Square(Offset)));
		AeyerjiMinimapPrivate::DrawLine(
			OutDrawElements,
			AllottedGeometry,
			DrawLayer,
			Center + FVector2f(-HalfChord, Offset),
			Center + FVector2f(HalfChord, Offset),
			StyledGridColor,
			LineThickness * 0.65f);
		AeyerjiMinimapPrivate::DrawLine(
			OutDrawElements,
			AllottedGeometry,
			DrawLayer,
			Center + FVector2f(Offset, -HalfChord),
			Center + FVector2f(Offset, HalfChord),
			StyledGridColor,
			LineThickness * 0.65f);
	}

	AeyerjiMinimapPrivate::DrawCircle(
		OutDrawElements,
		AllottedGeometry,
		DrawLayer,
		Center,
		DrawRadius * 0.5f,
		StyledGridColor,
		LineThickness * 0.75f);

	const AActor* PlayerActor = GetOwningPlayerPawn();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	if (IsValid(PlayerActor))
	{
		for (const FAeyerjiTrackedMinimapMarker& Marker : TrackedMarkers)
		{
			AActor* MarkerActor = Marker.Actor.Get();
			if (!IsValid(MarkerActor) || !IsMarkerActorVisible(Marker))
			{
				continue;
			}

			const float MarkerSize = ResolveMarkerSize(Marker);
			const float MarkerDrawRadius = FMath::Max(1.f, DrawRadius - MarkerSize * 0.5f);
			FVector2f MarkerPosition;
			bool bAtEdge = false;
			if (!ProjectMarkerToMap(*MarkerActor, *PlayerActor, Center, MarkerDrawRadius, Marker.bClampToEdge, MarkerPosition, bAtEdge))
			{
				continue;
			}

			FLinearColor MarkerColor = ResolveMarkerColor(Marker);
			if (bAtEdge)
			{
				MarkerColor.A *= 0.82f;
			}
			MarkerColor *= WidgetTint;

			if (UTexture2D* IconTexture = Marker.IconTexture.Get())
			{
				FSlateBrush IconBrush;
				IconBrush.DrawAs = ESlateBrushDrawType::Image;
				IconBrush.SetResourceObject(IconTexture);
				IconBrush.SetImageSize(FVector2f(MarkerSize, MarkerSize));
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					++DrawLayer,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(MarkerSize, MarkerSize),
						FSlateLayoutTransform(MarkerPosition - FVector2f(MarkerSize * 0.5f))),
					&IconBrush,
					ESlateDrawEffect::None,
					MarkerColor);
			}
			else if (Marker.MarkerType == EAeyerjiMinimapMarkerType::Enemy)
			{
				const float HalfSize = MarkerSize * 0.5f;
				AeyerjiMinimapPrivate::DrawClosedShape(
					OutDrawElements,
					AllottedGeometry,
					++DrawLayer,
					{
						MarkerPosition + FVector2f(0.f, -HalfSize),
						MarkerPosition + FVector2f(HalfSize, 0.f),
						MarkerPosition + FVector2f(0.f, HalfSize),
						MarkerPosition + FVector2f(-HalfSize, 0.f)
					},
					MarkerColor,
					FMath::Max(1.5f, LineThickness));
			}
			else
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					++DrawLayer,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(MarkerSize, MarkerSize),
						FSlateLayoutTransform(MarkerPosition - FVector2f(MarkerSize * 0.5f))),
					WhiteBrush,
					ESlateDrawEffect::None,
					MarkerColor);

				if (Marker.MarkerType == EAeyerjiMinimapMarkerType::Objective)
				{
					const float HalfSize = MarkerSize * 0.72f;
					AeyerjiMinimapPrivate::DrawLine(OutDrawElements, AllottedGeometry, DrawLayer, MarkerPosition - FVector2f(HalfSize, 0.f), MarkerPosition + FVector2f(HalfSize, 0.f), MarkerColor, LineThickness);
					AeyerjiMinimapPrivate::DrawLine(OutDrawElements, AllottedGeometry, DrawLayer, MarkerPosition - FVector2f(0.f, HalfSize), MarkerPosition + FVector2f(0.f, HalfSize), MarkerColor, LineThickness);
				}
			}

			if (Marker.bRotateWithOwner)
			{
				float RelativeYaw = MarkerActor->GetActorRotation().Yaw;
				if (bRotateWithPlayer)
				{
					RelativeYaw -= PlayerActor->GetActorRotation().Yaw;
				}
				const float HeadingRadians = FMath::DegreesToRadians(RelativeYaw);
				const FVector2f Heading(FMath::Sin(HeadingRadians), -FMath::Cos(HeadingRadians));
				AeyerjiMinimapPrivate::DrawLine(
					OutDrawElements,
					AllottedGeometry,
					DrawLayer,
					MarkerPosition,
					MarkerPosition + Heading * MarkerSize,
					MarkerColor,
					LineThickness);
			}
		}

		const float PlayerYaw = bRotateWithPlayer ? 0.f : PlayerActor->GetActorRotation().Yaw;
		const float PlayerYawRadians = FMath::DegreesToRadians(PlayerYaw);
		const FVector2f PlayerForward(FMath::Sin(PlayerYawRadians), -FMath::Cos(PlayerYawRadians));
		const FVector2f PlayerRight(-PlayerForward.Y, PlayerForward.X);
		const float ArrowLength = PlayerMarkerSize;
		const float ArrowHalfWidth = PlayerMarkerSize * 0.48f;
		AeyerjiMinimapPrivate::DrawClosedShape(
			OutDrawElements,
			AllottedGeometry,
			++DrawLayer,
			{
				Center + PlayerForward * ArrowLength,
				Center - PlayerForward * (ArrowLength * 0.55f) + PlayerRight * ArrowHalfWidth,
				Center - PlayerForward * (ArrowLength * 0.30f),
				Center - PlayerForward * (ArrowLength * 0.55f) - PlayerRight * ArrowHalfWidth
			},
			StyledPlayerColor,
			FMath::Max(2.f, LineThickness * 1.5f));
	}

	// A brighter rim and top-orientation tick keep the placeholder readable over bright world scenes.
	AeyerjiMinimapPrivate::DrawCircle(
		OutDrawElements,
		AllottedGeometry,
		++DrawLayer,
		Center,
		PanelRadius - LineThickness,
		StyledFrameColor,
		LineThickness);
	AeyerjiMinimapPrivate::DrawLine(
		OutDrawElements,
		AllottedGeometry,
		DrawLayer,
		Center + FVector2f(0.f, -PanelRadius + 3.f),
		Center + FVector2f(0.f, -PanelRadius + 11.f),
		StyledFrameColor,
		LineThickness * 2.f);

	return DrawLayer;
}
