// AeyerjiFloatingStatusBarComponent.cpp

#include "GUI/AeyerjiFloatingStatusBarComponent.h"
#include "GUI/W_AeyerjiStatusBar.h"
#include "GUI/AeyerjiStatusBarOverlayComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"

#include "Components/WidgetComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Logging/AeyerjiLog.h"

UAeyerjiFloatingStatusBarComponent::UAeyerjiFloatingStatusBarComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    // Defaults that designers can override in BP
    HealthAttr    = UAeyerjiAttributeSet::GetHPAttribute();
    MaxHealthAttr = UAeyerjiAttributeSet::GetHPMaxAttribute();
    ManaAttr      = UAeyerjiAttributeSet::GetManaAttribute();
    MaxManaAttr   = UAeyerjiAttributeSet::GetManaMaxAttribute();
    XPAttr        = UAeyerjiAttributeSet::GetXPAttribute();
    XPMaxAttr     = UAeyerjiAttributeSet::GetXPMaxAttribute();
    LevelAttr     = UAeyerjiAttributeSet::GetLevelAttribute();
    HPRegenAttr   = UAeyerjiAttributeSet::GetHPRegenAttribute();
    ManaRegenAttr = UAeyerjiAttributeSet::GetManaRegenAttribute();
}

void UAeyerjiFloatingStatusBarComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!StatusBarWidgetClass)
    {
        LogMissingWidget();
    }

    switch (Mode)
    {
    case EStatusBarMode::HUD:     CreateHUDWidget();               break;
    case EStatusBarMode::World:   CreateWorldWidget();             break;
    case EStatusBarMode::Overlay: RegisterWithOverlay();           break;
    }

    // Only tick to billboard in legacy World mode
    SetComponentTickEnabled(Mode == EStatusBarMode::World && bFaceCamera);
}

void UAeyerjiFloatingStatusBarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // HUD cleanup
    if (HUDWidget)
    {
        HUDWidget->RemoveFromParent();
        HUDWidget = nullptr;
    }
    // World cleanup
    if (WidgetComp)
    {
        WidgetComp->DestroyComponent();
        WidgetComp = nullptr;
    }
    // Overlay cleanup
    CleanupOverlay();

    if (RetryTimer.IsValid())
    {
        GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
    }

    Super::EndPlay(EndPlayReason);
}

void UAeyerjiFloatingStatusBarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
    Super::TickComponent(DeltaTime, TickType, ThisTick);

    // Simple billboard for legacy world-widget mode
    if (Mode == EStatusBarMode::World && WidgetComp && bFaceCamera)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector CamLoc; FRotator CamRot;
            PC->GetPlayerViewPoint(CamLoc, CamRot);
            FRotator LookAt = (CamLoc - WidgetComp->GetComponentLocation()).Rotation();
            if (bYawOnly) { LookAt.Pitch = 0.f; LookAt.Roll = 0.f; } else { LookAt.Roll = 0.f; }
            WidgetComp->SetWorldRotation(LookAt);
        }
    }
}

UAbilitySystemComponent* UAeyerjiFloatingStatusBarComponent::FindASC() const
{
    if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetOwner()))
    {
        return ASI->GetAbilitySystemComponent();
    }
    return nullptr;
}

UUserWidget* UAeyerjiFloatingStatusBarComponent::GetStatusBarWidget() const
{
    if (HUDWidget)
    {
        return HUDWidget;
    }

    if (WidgetComp)
    {
        return WidgetComp->GetUserWidgetObject();
    }

    return nullptr;
}

void UAeyerjiFloatingStatusBarComponent::BindWidget(UW_AeyerjiStatusBar* WB)
{
    if (!WB) return;
    if (UAbilitySystemComponent* ASC = FindASC())
    {
        if (XPAttr.IsValid() && XPMaxAttr.IsValid() && LevelAttr.IsValid())
        {
            WB->BindToAttributesWithXPAndLevel(ASC, HealthAttr, MaxHealthAttr, ManaAttr, MaxManaAttr, XPAttr, XPMaxAttr, LevelAttr);
        }
        else
        {
            WB->BindToAttributes(ASC, HealthAttr, MaxHealthAttr, ManaAttr, MaxManaAttr);
        }

        // Optional regen binding (if attributes are valid)
        if (HPRegenAttr.IsValid() && ManaRegenAttr.IsValid())
        {
            WB->BindRegenAttributes(ASC, HPRegenAttr, ManaRegenAttr);
        }
    }
}

void UAeyerjiFloatingStatusBarComponent::CreateWorldWidget()
{
    if (GetWorld()->IsNetMode(NM_DedicatedServer)) return;

    if (!StatusBarWidgetClass)
    {
        LogMissingWidget(); // will print owner name
        return;
    }

    if (!WidgetComp)
    {
        WidgetComp = NewObject<UWidgetComponent>(GetOwner(), UWidgetComponent::StaticClass(), TEXT("FloatingStatusBar"));
        WidgetComp->RegisterComponent();
        WidgetComp->AttachToComponent(GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    }

    WidgetComp->SetVisibility(true);
    WidgetComp->SetHiddenInGame(false);
    WidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    WidgetComp->SetWidgetSpace(EWidgetSpace::World);
    WidgetComp->SetRelativeLocation(WorldOffset);
    WidgetComp->SetDrawAtDesiredSize(bDrawAtDesiredSize);
    if (!bDrawAtDesiredSize) WidgetComp->SetDrawSize(DrawSize);
    WidgetComp->SetTwoSided(bTwoSided);
    WidgetComp->SetPivot(Pivot);
    WidgetComp->SetWorldScale3D(WorldScale);
    WidgetComp->SetTranslucentSortPriority(SortPriority);

    // Build on each client
    WidgetComp->SetWidgetClass(StatusBarWidgetClass);
    WidgetComp->InitWidget();

    AJ_LOG(this, TEXT("World status bar using widget class: %s"), *GetNameSafe(StatusBarWidgetClass.Get()));

    if (UW_AeyerjiStatusBar* WB = Cast<UW_AeyerjiStatusBar>(WidgetComp->GetWidget()))
    {
        BindWidget(WB);
    }
}

void UAeyerjiFloatingStatusBarComponent::CreateHUDWidget()
{
    if (GetWorld()->IsNetMode(NM_DedicatedServer)) return;
    if (!StatusBarWidgetClass)
    {
        LogMissingWidget(); return;
    }

    APlayerController* PC = nullptr;
    if (const APawn* AsPawn = Cast<APawn>(GetOwner())) { PC = Cast<APlayerController>(AsPawn->GetController()); }
    if (!PC) { PC = UGameplayStatics::GetPlayerController(GetWorld(), 0); }
    if (!PC) { AJ_LOG(this, TEXT("No PlayerController for HUD mode.")); return; }

    HUDWidget = CreateWidget<UW_AeyerjiStatusBar>(PC, StatusBarWidgetClass);
    if (!HUDWidget)
    {
        AJ_LOG(this, TEXT("Failed to create HUD widget.")); return;
    }

    HUDWidget->AddToViewport(/*ZOrder=*/0);
    BindWidget(HUDWidget);
}

void UAeyerjiFloatingStatusBarComponent::RegisterWithOverlay()
{
    if (GetWorld()->IsNetMode(NM_DedicatedServer)) return;
    if (!StatusBarWidgetClass) { LogMissingWidget(); /* allow manager's DefaultWidgetClass if set */ }

    APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    if (!PC)
    {
        // PlayerController not ready yet (PIE network client timing) - retry next tick.
        if (!RetryTimer.IsValid())
        {
            GetWorld()->GetTimerManager().SetTimer(
                RetryTimer, this, &UAeyerjiFloatingStatusBarComponent::RegisterWithOverlay, 0.02f, false);
        }
        return;
    }

    // Find/create local overlay manager on the PC
    OverlayMgr = PC->FindComponentByClass<UAeyerjiStatusBarOverlayComponent>();
    if (!OverlayMgr)
    {
        OverlayMgr = NewObject<UAeyerjiStatusBarOverlayComponent>(PC, UAeyerjiStatusBarOverlayComponent::StaticClass(), TEXT("StatusBarOverlay"));
        OverlayMgr->RegisterComponent();
    }
    if (OverlayMgr)
    {
        OverlayMgr->RegisterSource(this);
    }
}

void UAeyerjiFloatingStatusBarComponent::CleanupOverlay()
{
    if (OverlayMgr)
    {
        OverlayMgr->UnregisterSource(this);
        OverlayMgr = nullptr;
    }
}

void UAeyerjiFloatingStatusBarComponent::LogMissingWidget() const
{
    if (!StatusBarWidgetClass)
    {
        AJ_LOG(this, TEXT("StatusBarWidgetClass is NULL. Set it on the component (BP or CDO)."));
    }
}

