// AeyerjiStatusBarOverlayComponent.cpp

#include "GUI/AeyerjiStatusBarOverlayComponent.h"
#include "GUI/W_AeyerjiStatusBar.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GUI/AeyerjiFloatingStatusBarComponent.h"
#include "Aeyerji/AeyerjiGameState.h"
#include "Engine/Engine.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"

namespace
{
    void ReportStatusBarProblem(const UObject* Context, const FString& Message)
    {
        const FString FullMessage = FString::Printf(TEXT("%s: %s"), *GetNameSafe(Context), *Message);
        UE_LOG(LogTemp, Warning, TEXT("StatusBarOverlay: %s"), *FullMessage);

        if (GEngine)
        {
            const FString ScreenMessage = FString::Printf(TEXT("StatusBarOverlay: %s"), *FullMessage);
            GEngine->AddOnScreenDebugMessage(
                static_cast<int32>(GetTypeHash(ScreenMessage)),
                8.f,
                FColor::Red,
                ScreenMessage);
        }
    }

    void BindOverlayWidget(UW_AeyerjiStatusBar* Widget, UAeyerjiFloatingStatusBarComponent* Source, AActor* Target)
    {
        if (!Widget || !Source || !Target)
        {
            return;
        }

        Widget->ConfigureHealthChunkDivisions(
            Source->ShouldShowHealthChunkDivisions(),
            Source->ShouldRequireAnyHealthChunkEligibilityTag(),
            Source->GetHealthChunkEligibilityTags(),
            Source->GetTargetHealthChunkCount(),
            Source->GetMinPreferredHealthChunkCount(),
            Source->GetMaxPreferredHealthChunkCount(),
            Source->GetMaxHealthChunkCount(),
            Source->GetHealthChunkSeparatorThickness(),
            Source->GetHealthChunkSeparatorVerticalInset(),
            Source->GetHealthChunkSeparatorColor());

        UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, /*LookForComponent=*/true);
        if (!ASC)
        {
            if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Target))
            {
                ASC = ASI->GetAbilitySystemComponent();
            }
        }
        if (!ASC)
        {
            return;
        }

        const FGameplayAttribute XPAttr = Source->GetXPAttr();
        const FGameplayAttribute XPMaxAttr = Source->GetXPMaxAttr();
        const FGameplayAttribute LevelAttr = Source->GetLevelAttr();

        if (XPAttr.IsValid() && XPMaxAttr.IsValid() && LevelAttr.IsValid())
        {
            Widget->BindToAttributesWithXPAndLevel(
                ASC,
                Source->GetHealthAttr(),
                Source->GetMaxHealthAttr(),
                Source->GetManaAttr(),
                Source->GetMaxManaAttr(),
                XPAttr,
                XPMaxAttr,
                LevelAttr);
        }
        else if (XPAttr.IsValid() && XPMaxAttr.IsValid())
        {
            Widget->BindToAttributesWithXP(
                ASC,
                Source->GetHealthAttr(),
                Source->GetMaxHealthAttr(),
                Source->GetManaAttr(),
                Source->GetMaxManaAttr(),
                XPAttr,
                XPMaxAttr);
        }
        else
        {
            Widget->BindToAttributes(
                ASC,
                Source->GetHealthAttr(),
                Source->GetMaxHealthAttr(),
                Source->GetManaAttr(),
                Source->GetMaxManaAttr());
        }

        if (Source->GetHPRegenAttr().IsValid() && Source->GetManaRegenAttr().IsValid())
        {
            Widget->BindRegenAttributes(ASC, Source->GetHPRegenAttr(), Source->GetManaRegenAttr());
        }
    }
}

UAeyerjiStatusBarOverlayComponent::UAeyerjiStatusBarOverlayComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UAeyerjiStatusBarOverlayComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UAeyerjiStatusBarOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    for (FTracked& T : Tracked)
    {
        if (UW_AeyerjiStatusBar* W = T.Widget.Get())
        {
            W->RemoveFromParent();
        }
    }
    Tracked.Empty();
    Super::EndPlay(EndPlayReason);
}

UW_AeyerjiStatusBar* UAeyerjiStatusBarOverlayComponent::RegisterSource(UAeyerjiFloatingStatusBarComponent* Source)
{
    APlayerController* PC = GetPC();
    if (!PC)
    {
        ReportStatusBarProblem(this, TEXT("Overlay component is not owned by a valid local PlayerController."));
        return nullptr;
    }

    if (!Source)
    {
        ReportStatusBarProblem(this, TEXT("Tried to register a null floating status bar source."));
        return nullptr;
    }

    AActor* Target = Source->GetOwner();
    if (!IsValid(Target))
    {
        ReportStatusBarProblem(Source, TEXT("Floating status bar source has no valid owning actor."));
        return nullptr;
    }

    TSubclassOf<UW_AeyerjiStatusBar> WidgetClass = Source->GetStatusBarWidgetClass();
    if (!*WidgetClass) WidgetClass = DefaultWidgetClass;
    if (!*WidgetClass)
    {
        ReportStatusBarProblem(
            Source,
            FString::Printf(
                TEXT("No widget class set for %s. Assign StatusBarWidgetClass on the source BP or DefaultWidgetClass on the overlay manager."),
                *GetNameSafe(Target)));
        return nullptr;
    }

    FTracked* Existing = nullptr;
    for (FTracked& Entry : Tracked)
    {
        if (Entry.Source.Get() == Source)
        {
            Existing = &Entry;
            break;
        }
    }

    UW_AeyerjiStatusBar* W = Existing ? Existing->Widget.Get() : nullptr;
    if (!W)
    {
        // Build widget for this client
        W = CreateWidget<UW_AeyerjiStatusBar>(PC, WidgetClass);
        if (!W)
        {
            ReportStatusBarProblem(
                Source,
                FString::Printf(
                    TEXT("Failed to create status bar widget %s for %s."),
                    *GetNameSafe(WidgetClass.Get()),
                    *GetNameSafe(Target)));
            return nullptr;
        }

        // Add to viewport and align bottom-center
        W->AddToViewport(BaseZOrder + Source->GetOverlayZOrder());
        W->SetAlignmentInViewport(FVector2D(0.5f, 0.0f));
        W->SetVisibility(ESlateVisibility::Visible);
    }

    BindOverlayWidget(W, Source, Target);

    if (Existing)
    {
        Existing->Target = Target;
        Existing->Widget = W;
        Existing->WorldOffset = Source->GetWorldOffset();
        Existing->ScreenPixelOffset = Source->GetOverlayPixelOffset();
        Existing->ZOrder = BaseZOrder + Source->GetOverlayZOrder();
        Existing->LastAppliedWidthScale = -1.f;
        return W;
    }

    // Store
    FTracked T;
    T.Source = Source;
    T.Target = Target;
    T.Widget = W;
    T.WorldOffset = Source->GetWorldOffset();
    T.ScreenPixelOffset = Source->GetOverlayPixelOffset();
    T.ZOrder = BaseZOrder + Source->GetOverlayZOrder();
    Tracked.Add(T);

    return W;
}

bool UAeyerjiStatusBarOverlayComponent::IsOverlayPresentationValid(const APlayerController* PC) const
{
    if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const AAeyerjiGameState* GameState = World ? World->GetGameState<AAeyerjiGameState>() : nullptr;
    if (!GameState || GameState->GetWorldFlowPhase() != EAeyerjiWorldFlowPhase::Gameplay)
    {
        return false;
    }

    switch (GameState->GetRunState())
    {
    case EAeyerjiRunState::InRun:
    case EAeyerjiRunState::BossDefeated:
    case EAeyerjiRunState::ObjectiveComplete:
        break;

    default:
        return false;
    }

    const APawn* LocalPawn = PC->GetPawn();
    const AActor* ViewTarget = PC->GetViewTarget();
    if (!IsValid(LocalPawn) || LocalPawn->IsHidden() || !IsValid(ViewTarget))
    {
        return false;
    }

    // The project normally uses the pawn itself as the view target. Accept an actor attached
    // to that pawn too, so an intentionally attached camera rig remains a valid gameplay view.
    if (ViewTarget != LocalPawn && !ViewTarget->IsAttachedTo(LocalPawn))
    {
        return false;
    }

    FVector ViewLocation;
    FRotator ViewRotation;
    PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
    return !ViewLocation.ContainsNaN() && !ViewRotation.ContainsNaN()
        && FMath::IsFinite(ViewLocation.X) && FMath::IsFinite(ViewLocation.Y) && FMath::IsFinite(ViewLocation.Z);
}

void UAeyerjiStatusBarOverlayComponent::HideAllTrackedWidgets() const
{
    for (const FTracked& Entry : Tracked)
    {
        if (UW_AeyerjiStatusBar* Widget = Entry.Widget.Get())
        {
            Widget->SetVisibility(ESlateVisibility::Hidden);
        }
    }
}

float UAeyerjiStatusBarOverlayComponent::ResolveOwnerWidthScale(
    const UAeyerjiFloatingStatusBarComponent* Source,
    const AActor* Target) const
{
    if (!Source || !Target || !Source->ShouldScaleOverlayWidthWithOwnerSize())
    {
        return 1.f;
    }

    float OwnerDiameter = 0.f;
    if (const ACharacter* Character = Cast<ACharacter>(Target))
    {
        if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
        {
            const float Radius = Capsule->GetScaledCapsuleRadius();
            if (FMath::IsFinite(Radius) && Radius > KINDA_SMALL_NUMBER)
            {
                OwnerDiameter = Radius * 2.f;
            }
        }
    }

    // Bounds support non-character owners and the brief window before a character capsule is valid.
    if (OwnerDiameter <= KINDA_SMALL_NUMBER)
    {
        FVector BoundsOrigin;
        FVector BoundsExtent;
        Target->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
        if (!BoundsExtent.ContainsNaN())
        {
            const float BoundsDiameter = FMath::Max(BoundsExtent.X, BoundsExtent.Y) * 2.f;
            if (FMath::IsFinite(BoundsDiameter) && BoundsDiameter > KINDA_SMALL_NUMBER)
            {
                OwnerDiameter = BoundsDiameter;
            }
        }
    }

    const float ReferenceDiameter = Source->GetOverlayReferenceOwnerDiameter();
    if (!FMath::IsFinite(OwnerDiameter) || OwnerDiameter <= KINDA_SMALL_NUMBER
        || !FMath::IsFinite(ReferenceDiameter) || ReferenceDiameter <= KINDA_SMALL_NUMBER)
    {
        return 1.f;
    }

    const float RequestedMinScale = Source->GetOverlayMinWidthScale();
    const float RequestedMaxScale = Source->GetOverlayMaxWidthScale();
    const float RequestedMultiplier = Source->GetOverlayWidthScaleMultiplier();
    const float MinScale = FMath::Max(
        0.05f,
        FMath::IsFinite(RequestedMinScale) ? RequestedMinScale : 0.60f);
    const float MaxScale = FMath::Max(
        MinScale,
        FMath::IsFinite(RequestedMaxScale) ? RequestedMaxScale : 1.75f);
    const float Multiplier = FMath::Max(
        0.05f,
        FMath::IsFinite(RequestedMultiplier) ? RequestedMultiplier : 1.f);
    return FMath::Clamp((OwnerDiameter / ReferenceDiameter) * Multiplier, MinScale, MaxScale);
}

void UAeyerjiStatusBarOverlayComponent::ApplyOwnerWidthScale(FTracked& Entry)
{
    UW_AeyerjiStatusBar* Widget = Entry.Widget.Get();
    const UAeyerjiFloatingStatusBarComponent* Source = Entry.Source.Get();
    const AActor* Target = Entry.Target.Get();
    if (!Widget || !Source || !Target)
    {
        return;
    }

    const float WidthScale = ResolveOwnerWidthScale(Source, Target);
    if (!FMath::IsNearlyEqual(Entry.LastAppliedWidthScale, WidthScale, 0.001f))
    {
        Widget->SetOverlayWidthScale(WidthScale);
        Entry.LastAppliedWidthScale = WidthScale;
    }
}

bool UAeyerjiStatusBarOverlayComponent::IsScreenPositionInViewport(
    const FVector2D& ScreenPosition,
    const APlayerController* PC) const
{
    if (!PC || ScreenPosition.ContainsNaN()
        || !FMath::IsFinite(ScreenPosition.X) || !FMath::IsFinite(ScreenPosition.Y))
    {
        return false;
    }

    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    PC->GetViewportSize(ViewportWidth, ViewportHeight);
    return ViewportWidth > 0 && ViewportHeight > 0
        && ScreenPosition.X >= 0.f && ScreenPosition.X <= static_cast<float>(ViewportWidth)
        && ScreenPosition.Y >= 0.f && ScreenPosition.Y <= static_cast<float>(ViewportHeight);
}

void UAeyerjiStatusBarOverlayComponent::UnregisterSource(UAeyerjiFloatingStatusBarComponent* Source)
{
    for (int32 i = Tracked.Num() - 1; i >= 0; --i)
    {
        if (Tracked[i].Source.Get() == Source)
        {
            if (UW_AeyerjiStatusBar* W = Tracked[i].Widget.Get())
            {
                W->RemoveFromParent();
            }
            Tracked.RemoveAtSwap(i);
        }
    }
}

bool UAeyerjiStatusBarOverlayComponent::ProjectToScreen(const FVector& WorldLoc, FVector2D& OutPos) const
{
    APlayerController* PC = GetPC();
    if (!PC) return false;

    return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
        PC, WorldLoc, OutPos, /*bPlayerViewportRelative=*/true);
}

static FVector2D ClampToViewportForWidget(
    const UUserWidget* W,
    const APlayerController* PC,
    const FVector2D InPos,
    const float EdgePadding)
{
    if (!W || !PC) return InPos;

    int32 ViewW=0, ViewH=0;
    PC->GetViewportSize(ViewW, ViewH);

    const float Scale = UWidgetLayoutLibrary::GetViewportScale(const_cast<APlayerController*>(PC)); // DPI
    const FVector2D Desired = W->GetDesiredSize() * Scale;

    // Alignment the widget was set to (defaults to 0,0 if not set)
    FVector2D Align(0.f, 0.f);
    if (const UUserWidget* UW = Cast<UUserWidget>(W))
    {
        Align = UW->GetAlignmentInViewport();
    }

    const float MinX = EdgePadding + Desired.X * Align.X;
    const float MaxX = ViewW - EdgePadding - Desired.X * (1.f - Align.X);
    const float MinY = EdgePadding + Desired.Y * Align.Y;
    const float MaxY = ViewH - EdgePadding - Desired.Y * (1.f - Align.Y);

    FVector2D Out = InPos;
    Out.X = FMath::Clamp(Out.X, MinX, MaxX);
    Out.Y = FMath::Clamp(Out.Y, MinY, MaxY);
    return Out;
}

bool UAeyerjiStatusBarOverlayComponent::IsOccluded(const FVector& WorldLoc, const AActor* Ignore) const
{
    const APlayerController* PC = GetPC();
    UWorld* World = GetWorld();
    if (!bOcclusionCheck || !PC || !World) return false;

    FVector CamLoc; FRotator CamRot;
    PC->GetPlayerViewPoint(CamLoc, CamRot);

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(StatusBarOcclusion), /*bTraceComplex=*/false);
    if (Ignore) Params.AddIgnoredActor(Ignore);
    if (const APawn* LocalPawn = PC->GetPawn())
    {
        Params.AddIgnoredActor(LocalPawn);
    }
    if (const AActor* ViewTarget = PC->GetViewTarget())
    {
        Params.AddIgnoredActor(ViewTarget);
    }

    const bool bHit = World->LineTraceSingleByChannel(Hit, CamLoc, WorldLoc, ECC_Visibility, Params);
    return bHit && Hit.GetActor() != Ignore;
}

void UAeyerjiStatusBarOverlayComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(DeltaTime, TickType, ThisTick);
    
    APlayerController* PC = GetPC();
    if (!PC)
    {
        ReportStatusBarProblem(this, TEXT("Overlay tick skipped because owner is not a valid local PlayerController."));
        return;
    }

    if (!IsOverlayPresentationValid(PC))
    {
        HideAllTrackedWidgets();
        return;
    }

    FVector CameraLocation;
    FRotator CameraRotation;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

    for (int32 i = Tracked.Num() - 1; i >= 0; --i)
    {
        FTracked& T = Tracked[i];

        UAeyerjiFloatingStatusBarComponent* Source = T.Source.Get();
        AActor* Target = T.Target.Get();
        UW_AeyerjiStatusBar* W = T.Widget.Get();
        if (!Source || !Target || !W)
        {
            if (W)
            {
                W->RemoveFromParent();
            }
            Tracked.RemoveAtSwap(i);
            continue;
        }

        if (Target->IsHidden() || Target->IsActorBeingDestroyed())
        {
            W->SetVisibility(ESlateVisibility::Hidden);
            continue;
        }

        // Distance LOD
        if (MaxDrawDistance > 0.f)
        {
            const float DistSq = FVector::DistSquared(
                CameraLocation,
                Target->GetActorLocation());
            if (DistSq > FMath::Square(MaxDrawDistance))
            {
                W->SetVisibility(ESlateVisibility::Hidden);
                continue;
            }
        }

        // Anchor: actor location + per-source offset
        const FVector WorldLoc = Target->GetActorLocation() + T.WorldOffset;
        if (WorldLoc.ContainsNaN())
        {
            W->SetVisibility(ESlateVisibility::Hidden);
            continue;
        }

        const FVector CameraToAnchor = WorldLoc - CameraLocation;
        if (CameraToAnchor.IsNearlyZero() || FVector::DotProduct(CameraRotation.Vector(), CameraToAnchor) <= 0.f)
        {
            W->SetVisibility(ESlateVisibility::Hidden);
            continue;
        }

        ApplyOwnerWidthScale(T);

        // Project and place on screen
        FVector2D ScreenPos;
        
        if (ProjectToScreen(WorldLoc, ScreenPos) && IsScreenPositionInViewport(ScreenPos, PC))
        {
            W->SetVisibility(ESlateVisibility::HitTestInvisible);

            // Apply your per-source pixel offset, then clamp using size + alignment + DPI
            const FVector2D Wanted = ScreenPos + T.ScreenPixelOffset;
            //const FVector2D Clamped = ClampToViewportForWidget(W, PC, Wanted, EdgePadding);

            // Note: keep 'bRemoveDPIScale=false' because we already worked in viewport pixels.
            W->SetPositionInViewport(Wanted, /*bRemoveDPIScale=*/false);
        }
        else
        {
            W->SetVisibility(ESlateVisibility::Hidden);
        }
        // Hide when occluded (optional)
        if (IsOccluded(WorldLoc, Target))
        {
            W->SetVisibility(ESlateVisibility::Hidden);
        }
    }
}


