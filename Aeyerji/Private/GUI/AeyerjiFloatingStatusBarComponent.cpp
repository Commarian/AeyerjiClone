// AeyerjiFloatingStatusBarComponent.cpp

#include "GUI/AeyerjiFloatingStatusBarComponent.h"
#include "GUI/W_AeyerjiStatusBar.h"
#include "GUI/AeyerjiStatusBarOverlayComponent.h"
#include "Aeyerji/AeyerjiGameState.h"
#include "AeyerjiCharacter.h"
#include "Attributes/AeyerjiAttributeSet.h"

#include "Components/WidgetComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Logging/AeyerjiLog.h"

namespace
{
    constexpr int32 StatusBarValueResyncCount = 12;
    constexpr int32 StatusBarMissingASCRetryCount = 20;
    constexpr float StatusBarDeferredRebindDelay = 0.10f;
}

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

    DeferredBindAttempts = 0;
    BindOwnerASCReadyDelegate();

    if (ShouldDeferHUDInitializationToRunStart())
    {
        BindToRunStateChanges();
        SetComponentTickEnabled(false);
        return;
    }

    InitializeStatusBarPresentation();
}

void UAeyerjiFloatingStatusBarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (RunStateBindRetryTimer.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(RunStateBindRetryTimer);
        }
    }

    UnbindFromRunStateChanges();

    if (DeferredBindTimer.IsValid())
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(DeferredBindTimer);
        }
    }

    UnbindOwnerASCReadyDelegate();

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
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(RetryTimer);
        }
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
    if (AActor* OwnerActor = GetOwner())
    {
        if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor, /*LookForComponent=*/true))
        {
            return ASC;
        }
    }

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
    else
    {
        if (DeferredBindAttempts == 0)
        {
            AJ_LOG(this, TEXT("Status bar bind deferred; ASC not ready on %s"), *GetNameSafe(GetOwner()));
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

void UAeyerjiFloatingStatusBarComponent::RebindLiveWidget()
{
    switch (Mode)
    {
    case EStatusBarMode::HUD:
        if (HUDWidget)
        {
            BindWidget(HUDWidget);
        }
        break;

    case EStatusBarMode::World:
        if (WidgetComp)
        {
            if (UW_AeyerjiStatusBar* WB = Cast<UW_AeyerjiStatusBar>(WidgetComp->GetWidget()))
            {
                BindWidget(WB);
            }
        }
        break;

    case EStatusBarMode::Overlay:
        if (OverlayMgr)
        {
            OverlayMgr->RegisterSource(this);
        }
        else
        {
            RegisterWithOverlay();
        }
        break;
    }
}

void UAeyerjiFloatingStatusBarComponent::BindOwnerASCReadyDelegate()
{
    AAeyerjiCharacter* OwnerCharacter = Cast<AAeyerjiCharacter>(GetOwner());
    if (!OwnerCharacter)
    {
        return;
    }

    OwnerCharacter->OnAbilitySystemReady.RemoveDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleOwnerAbilitySystemReady);
    OwnerCharacter->OnAbilitySystemReady.AddDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleOwnerAbilitySystemReady);
    BoundOwnerCharacter = OwnerCharacter;
}

void UAeyerjiFloatingStatusBarComponent::UnbindOwnerASCReadyDelegate()
{
    if (AAeyerjiCharacter* OwnerCharacter = BoundOwnerCharacter.Get())
    {
        OwnerCharacter->OnAbilitySystemReady.RemoveDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleOwnerAbilitySystemReady);
    }
    BoundOwnerCharacter = nullptr;
}

void UAeyerjiFloatingStatusBarComponent::HandleOwnerAbilitySystemReady()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredBindTimer);
    }

    DeferredBindAttempts = 0;
    AttemptDeferredRebind();
}

void UAeyerjiFloatingStatusBarComponent::AttemptDeferredRebind()
{
    UWorld* World = GetWorld();
    if (!World || World->IsNetMode(NM_DedicatedServer))
    {
        return;
    }

    RebindLiveWidget();
    ++DeferredBindAttempts;

    const bool bHasASC = (FindASC() != nullptr);
    // Values can arrive a few frames after the ASC itself during load/replication.
    const int32 MaxAttempts = bHasASC ? StatusBarValueResyncCount : StatusBarMissingASCRetryCount;
    if (DeferredBindAttempts < MaxAttempts)
    {
        World->GetTimerManager().SetTimer(
            DeferredBindTimer,
            this,
            &UAeyerjiFloatingStatusBarComponent::AttemptDeferredRebind,
            StatusBarDeferredRebindDelay,
            false);
    }
    else if (!bHasASC)
    {
        AJ_LOG(this, TEXT("Status bar bind retries exhausted for %s"), *GetNameSafe(GetOwner()));
    }
}

void UAeyerjiFloatingStatusBarComponent::HandleRunStateChanged(EAeyerjiRunState NewState, EAeyerjiRunState OldState)
{
    static_cast<void>(OldState);

    if (NewState == EAeyerjiRunState::InRun)
    {
        InitializeStatusBarPresentation();
    }
}

void UAeyerjiFloatingStatusBarComponent::RetryBindToRunStateChanges()
{
    BindToRunStateChanges();
}

void UAeyerjiFloatingStatusBarComponent::LogMissingWidget() const
{
    if (!StatusBarWidgetClass)
    {
        AJ_LOG(this, TEXT("StatusBarWidgetClass is NULL. Set it on the component (BP or CDO)."));
    }
}

bool UAeyerjiFloatingStatusBarComponent::ShouldDeferHUDInitializationToRunStart() const
{
    if (Mode != EStatusBarMode::HUD || !bInitializeHUDOnRunStart)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    if (!World || World->IsNetMode(NM_DedicatedServer))
    {
        return false;
    }

    return true;
}

void UAeyerjiFloatingStatusBarComponent::InitializeStatusBarPresentation()
{
    if (bStatusBarInitialized)
    {
        RebindLiveWidget();
        return;
    }

    if (!StatusBarWidgetClass)
    {
        LogMissingWidget();
    }

    switch (Mode)
    {
    case EStatusBarMode::HUD:
        CreateHUDWidget();
        bStatusBarInitialized = (HUDWidget != nullptr);
        break;

    case EStatusBarMode::World:
        CreateWorldWidget();
        bStatusBarInitialized = (WidgetComp != nullptr);
        break;

    case EStatusBarMode::Overlay:
        RegisterWithOverlay();
        bStatusBarInitialized = (OverlayMgr != nullptr) || RetryTimer.IsValid();
        break;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredBindTimer);
    }

    DeferredBindAttempts = 0;
    AttemptDeferredRebind();

    // Only tick to billboard in legacy World mode after the presentation exists.
    SetComponentTickEnabled(bStatusBarInitialized && Mode == EStatusBarMode::World && bFaceCamera);
}

void UAeyerjiFloatingStatusBarComponent::BindToRunStateChanges()
{
    if (bStatusBarInitialized)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || World->IsNetMode(NM_DedicatedServer))
    {
        return;
    }

    AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>();
    if (!GameState)
    {
        if (!RunStateBindRetryTimer.IsValid())
        {
            World->GetTimerManager().SetTimer(
                RunStateBindRetryTimer,
                this,
                &UAeyerjiFloatingStatusBarComponent::RetryBindToRunStateChanges,
                0.1f,
                false);
        }
        return;
    }

    World->GetTimerManager().ClearTimer(RunStateBindRetryTimer);

    GameState->OnRunStateChanged.RemoveDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleRunStateChanged);
    GameState->OnRunStateChanged.AddDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleRunStateChanged);
    BoundGameState = GameState;

    if (GameState->GetRunState() == EAeyerjiRunState::InRun)
    {
        InitializeStatusBarPresentation();
    }
}

void UAeyerjiFloatingStatusBarComponent::UnbindFromRunStateChanges()
{
    if (AAeyerjiGameState* GameState = BoundGameState.Get())
    {
        GameState->OnRunStateChanged.RemoveDynamic(this, &UAeyerjiFloatingStatusBarComponent::HandleRunStateChanged);
    }

    BoundGameState.Reset();
}

