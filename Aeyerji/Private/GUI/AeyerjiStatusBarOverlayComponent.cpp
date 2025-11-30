// AeyerjiStatusBarOverlayComponent.cpp

#include "GUI/AeyerjiStatusBarOverlayComponent.h"
#include "GUI/W_AeyerjiStatusBar.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GUI/AeyerjiFloatingStatusBarComponent.h"

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
    if (!PC || !Source) return nullptr;

    AActor* Target = Source->GetOwner();
    if (!IsValid(Target)) return nullptr;

    TSubclassOf<UW_AeyerjiStatusBar> WidgetClass = Source->GetStatusBarWidgetClass();
    if (!*WidgetClass) WidgetClass = DefaultWidgetClass;
    if (!*WidgetClass) return nullptr;

    // Build widget for this client
    UW_AeyerjiStatusBar* W = CreateWidget<UW_AeyerjiStatusBar>(PC, WidgetClass);
    if (!W) return nullptr;

    // Add to viewport and align bottom-center
    W->AddToViewport(BaseZOrder + Source->GetOverlayZOrder());
    W->SetAlignmentInViewport(FVector2D(0.5f, 0.0f));
    W->SetVisibility(ESlateVisibility::Visible);

    // Bind attributes
    UAbilitySystemComponent* ASC = nullptr;
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Target))
    {
        ASC = ASI->GetAbilitySystemComponent();
    }
    if (ASC)
    {
        const FGameplayAttribute XPAttr     = Source->GetXPAttr();
        const FGameplayAttribute XPMaxAttr  = Source->GetXPMaxAttr();
        const FGameplayAttribute LevelAttr  = Source->GetLevelAttr();

        if (XPAttr.IsValid() && XPMaxAttr.IsValid() && LevelAttr.IsValid())
        {
            W->BindToAttributesWithXPAndLevel(
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
            W->BindToAttributesWithXP(
                ASC,
                Source->GetHealthAttr(),
                Source->GetMaxHealthAttr(),
                Source->GetManaAttr(),
                Source->GetMaxManaAttr(),
                XPAttr,
                XPMaxAttr);
        }
        
        // Optional regen binding
        if (Source->GetHPRegenAttr().IsValid() && Source->GetManaRegenAttr().IsValid())
        {
            W->BindRegenAttributes(ASC, Source->GetHPRegenAttr(), Source->GetManaRegenAttr());
        }
else
        {
            W->BindToAttributes(
                ASC,
                Source->GetHealthAttr(),
                Source->GetMaxHealthAttr(),
                Source->GetManaAttr(),
                Source->GetMaxManaAttr());
        }
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
    if (!bOcclusionCheck || !PC) return false;

    FVector CamLoc; FRotator CamRot;
    PC->GetPlayerViewPoint(CamLoc, CamRot);

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(StatusBarOcclusion), /*bTraceComplex=*/false);
    if (Ignore) Params.AddIgnoredActor(Ignore);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, WorldLoc, ECC_Visibility, Params);
    return bHit && Hit.GetActor() != Ignore;
}

void UAeyerjiStatusBarOverlayComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(DeltaTime, TickType, ThisTick);
    
    APlayerController* PC = GetPC();
    if (!PC) return;

    for (int32 i = Tracked.Num() - 1; i >= 0; --i)
    {
        FTracked& T = Tracked[i];

        AActor* Target = T.Target.Get();
        UW_AeyerjiStatusBar* W = T.Widget.Get();
        if (!Target || !W)
        {
            Tracked.RemoveAtSwap(i);
            continue;
        }

        // Distance LOD
        if (MaxDrawDistance > 0.f)
        {
            const float DistSq = FVector::DistSquared(
                PC->PlayerCameraManager->GetCameraLocation(),
                Target->GetActorLocation());
            if (DistSq > FMath::Square(MaxDrawDistance))
            {
                W->SetVisibility(ESlateVisibility::Hidden);
                continue;
            }
        }

        // Anchor: actor location + per-source offset
        const FVector WorldLoc = Target->GetActorLocation() + T.WorldOffset;
        // Project and place on screen
        FVector2D ScreenPos;
        
        if (ProjectToScreen(WorldLoc, ScreenPos))
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


