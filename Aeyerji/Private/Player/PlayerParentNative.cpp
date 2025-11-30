// PlayerParentNative.cpp

// ReSharper disable CppTooWideScopeInitStatement

#include "Player/PlayerParentNative.h"

#include "CharacterStatsLibrary.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/AeyerjiPickupFXComponent.h"
#include "Components/WeaponEquipmentComponent.h"
#include "Items/InventoryComponent.h"

#include "Progression/AeyerjiLevelingComponent.h"

#include "Aeyerji/AeyerjiPlayerController.h"

#include "Aeyerji/AeyerjiPlayerState.h"

#include "Aeyerji/AeyerjiSaveGame.h"

#include "Blueprint/UserWidget.h"

#include "GameFramework/PlayerState.h"

#include "GUI/W_ActionBar.h"

#include "Kismet/GameplayStatics.h"

#include "Logging/AeyerjiLog.h"

#include "Player/PlayerPathAIController.h"

static const FName RHandSocket(TEXT("WeaponRHandSocket"));

APlayerParentNative::APlayerParentNative()

{

  bReplicates = true;

  bReplicateUsingRegisteredSubObjectList = true;

  InventoryComponent = CreateDefaultSubobject<UAeyerjiInventoryComponent>(
      TEXT("InventoryComponent"));

  RHandMeshComp =
      CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RHandMeshComp"));

  RHandMeshComp->SetupAttachment(GetMesh(), RHandSocket);

  RHandMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

  RHandMeshComp->SetIsReplicated(true);

  WeaponEquipmentComponent =
      CreateDefaultSubobject<UWeaponEquipmentComponent>(
          TEXT("WeaponEquipment"));
}

void APlayerParentNative::PostInitializeComponents()

{

  Super::PostInitializeComponents();

  EnsureInventoryComponent();
}

void APlayerParentNative::BeginPlay()

{

  Super::BeginPlay();

  EnsureInventoryComponent();
}

void APlayerParentNative::TryLoadingSave()

{

  if (!IsLocallyControlled())

  {

    return;
  }

  if (ActionBarWidget)

  {

    AJ_LOG(this,
           TEXT("***Calling InitWithPlayerState ((ActionBarWidget) == true)"));

    ActionBarWidget->InitWithPlayerState(GetPlayerState<AAeyerjiPlayerState>());

    return;
  }

  if (!ActionBarWidget && ActionBarClass)

  {

    auto *PlayerStateTemp = GetPlayerState<AAeyerjiPlayerState>();

    if (ActionBarWidget == nullptr)

    {

      AJ_LOG(
          this,
          TEXT("***Calling InitWithPlayerState (ActionBarWidget == nullptr)"));

      APlayerController *PC = Cast<AAeyerjiPlayerController>(GetController());

      ActionBarWidget = CreateWidget<UW_ActionBar>(PC, ActionBarClass);

      ActionBarWidget->AddToViewport();

      ActionBarWidget->InitWithPlayerState(PlayerStateTemp);
    }
  }
}


bool APlayerParentNative::ActivateActionBarSlot(int32 SlotIndex)
{
  if (!ActionBarWidget)
  {
    if (IsLocallyControlled())
    {
      TryLoadingSave();
    }

    if (!ActionBarWidget)
    {
      AJ_LOG(this, TEXT("ActivateActionBarSlot() ActionBarWidget not ready"));
      return false;
    }
  }

  return ActionBarWidget->ActivateSlotByIndex(SlotIndex);
}

UAeyerjiInventoryComponent* APlayerParentNative::EnsureInventoryComponent()

{

  UAeyerjiInventoryComponent* Resolved = ResolveInventoryComponent();

  HandleInventoryComponentResolved(Resolved);

  return Resolved;
}

UAeyerjiInventoryComponent* APlayerParentNative::GetInventoryComponent() const

{

  return const_cast<APlayerParentNative*>(this)->EnsureInventoryComponent();
}

UAeyerjiInventoryComponent* APlayerParentNative::ResolveInventoryComponent()

{

  if (InventoryComponent && InventoryComponent->GetOwner() == this)
  {
    return InventoryComponent;
  }

  if (UAeyerjiInventoryComponent* Existing =
          FindComponentByClass<UAeyerjiInventoryComponent>())
  {
    InventoryComponent = Existing;
    return InventoryComponent;
  }

  return CreateRuntimeInventoryComponent(TEXT("InventoryComponentRuntime"));
}

UAeyerjiInventoryComponent* APlayerParentNative::CreateRuntimeInventoryComponent(
    const FName& ComponentName)

{

  UAeyerjiInventoryComponent* NewComponent =
      NewObject<UAeyerjiInventoryComponent>(
          this, UAeyerjiInventoryComponent::StaticClass(), ComponentName);

  if (!NewComponent)
  {
    AJ_LOG(this,
           TEXT("EnsureInventoryComponent failed - could not create component "
                "for %s"),
           *GetName());
    return nullptr;
  }

  AddInstanceComponent(NewComponent);
  NewComponent->OnComponentCreated();
  NewComponent->RegisterComponent();
  NewComponent->SetIsReplicated(true);

  return NewComponent;
}

void APlayerParentNative::HandleInventoryComponentResolved(
    UAeyerjiInventoryComponent* ResolvedComponent)

{

  if (!ResolvedComponent)
  {
    return;
  }

  if (InventoryComponent != ResolvedComponent)
  {
    if (InventoryComponent)
    {
      InventoryComponent->OnEquippedItemChanged.RemoveDynamic(
          this, &APlayerParentNative::HandleInventoryEquippedItemChanged);
    }

    InventoryComponent = ResolvedComponent;
    bInventoryBindingsInitialized = false;
  }

  BindInventoryDelegates();

  if (LastBroadcastInventory.Get() != ResolvedComponent)
  {
    LastBroadcastInventory = ResolvedComponent;

    OnInventoryComponentReady.Broadcast(ResolvedComponent);
    BP_OnInventoryComponentReady(ResolvedComponent);

    // Push the current equipped state so UI/Blueprint listeners receive an initial snapshot.
    if (ResolvedComponent)
    {
      for (const FEquippedItemEntry& Entry : ResolvedComponent->EquippedItems)
      {
        HandleInventoryEquippedItemChanged(Entry.Slot, Entry.SlotIndex, Entry.Item);
      }
    }
  }
}

void APlayerParentNative::BindInventoryDelegates()

{

  if (!InventoryComponent)
  {
    return;
  }

  if (bInventoryBindingsInitialized)
  {
    return;
  }

  InventoryComponent->OnEquippedItemChanged.RemoveDynamic(
      this, &APlayerParentNative::HandleInventoryEquippedItemChanged);

  InventoryComponent->OnEquippedItemChanged.AddDynamic(
      this, &APlayerParentNative::HandleInventoryEquippedItemChanged);

  bInventoryBindingsInitialized = true;
}

void APlayerParentNative::HandleInventoryEquippedItemChanged(
    EEquipmentSlot Slot, int32 SlotIndex, UAeyerjiItemInstance* Item)

{

  if (WeaponEquipmentComponent)
  {
    WeaponEquipmentComponent->HandleEquippedItemChanged(Slot, SlotIndex, Item);
  }

  if (UAeyerjiPickupFXComponent* PickupFX = FindComponentByClass<UAeyerjiPickupFXComponent>())
  {
    if (GetNetMode() != NM_DedicatedServer)
    {
      if (Item)
      {
        FAeyerjiPickupVisualConfig Visuals = Item->GetPickupVisualConfig();

        if (UAeyerjiInventoryComponent* Inventory = EnsureInventoryComponent())
        {
          int32 StackCount = 0;
          FLinearColor SynergyColor = FLinearColor::White;
          FName SynergyParam = NAME_None;

          if (Inventory->GetEquipSynergyForItem(Item, StackCount, SynergyColor, SynergyParam))
          {
            Visuals.FXColor = SynergyColor;

            if (!SynergyParam.IsNone())
            {
              Visuals.ColorParameter = SynergyParam;
            }
          }
        }

        if (Visuals.HasEquipVisuals())
        {
          PickupFX->PlayEquipFX(Visuals, Slot, SlotIndex);
        }
      }
      else
      {
        PickupFX->StopEquipFX(Slot, SlotIndex);
      }
    }
  }

  OnInventoryEquippedItemChanged.Broadcast(Slot, SlotIndex, Item);
  BP_OnInventoryEquippedItemChanged(Slot, SlotIndex, Item);
}

/* Server: controller just possessed the pawn */

void APlayerParentNative::PossessedBy(AController *NewController)

{

  Super::PossessedBy(NewController);

  bASCInitialised = false; // force re-init on fresh possession/respawn

  EnsureInventoryComponent();

  AJ_LOG(this, TEXT("PawnClientRestart - HasAuthority: %d"), HasAuthority());

  InitAbilityActorInfo();

  if (AAeyerjiPlayerController *AeyerjiPC =
          Cast<AAeyerjiPlayerController>(NewController))

  {

    AeyerjiPC->SetViewTarget(this);
  }
}

void APlayerParentNative::PawnClientRestart()

{

  Super::PawnClientRestart();

  bASCInitialised = false; // ensure ASC rebinds after restart/respawn

  EnsureInventoryComponent();

  AJ_LOG(this, TEXT("OnRep_PlayerState - PlayerState: %s"),
         *GetNameSafe(GetPlayerState()));

  InitAbilityActorInfo();

  if (IsLocallyControlled())

  {

    FTimerHandle DelayTimer;

    GetWorldTimerManager().SetTimer(
        DelayTimer,
        [this]()

        { TryLoadingSave(); },
        0.2f, false);

  }

}

void APlayerParentNative::OnRep_PlayerState()

{

  Super::OnRep_PlayerState();

  bASCInitialised = false; // playerstate swap -> rebind ASC

  EnsureInventoryComponent();

  InitAbilityActorInfo();
}

void APlayerParentNative::EndPlay(const EEndPlayReason::Type EndPlayReason)

{

  CancelInitAbilityActorInfoRetry();

  if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())

  {

    const FString Slot = UCharacterStatsLibrary::MakeStableCharSlotName(PS);

    bool bLoadedExisting = false;
    if (UAeyerjiSaveGame *Data = UCharacterStatsLibrary::LoadOrCreateAeyerjiSave(Slot, bLoadedExisting))
    {
      UCharacterStatsLibrary::SaveAeyerjiChar(Data, PS, Slot);
    }
  }

  Super::EndPlay(EndPlayReason);
}

/* ------------------------------------------------------------------ */

void APlayerParentNative::InitAbilityActorInfo()

{

  AJ_LOG(this,
         TEXT("InitAbilityActorInfo starting - HasAuthority: %d, NetMode: %d"),
         HasAuthority(),

         (int32)GetNetMode());

  // If ASC thinks it's initialised but the avatar/owner are not this pawn (can happen after respawn),
  // clear the flag so we rebind cleanly.
  if (bASCInitialised && AbilitySystemAeyerji)
  {
    const FGameplayAbilityActorInfo* Info = AbilitySystemAeyerji->AbilityActorInfo.Get();
    const bool bInfoValidForThisPawn =
        Info && Info->AvatarActor.Get() == this && Info->OwnerActor.Get() == this;
    if (!bInfoValidForThisPawn)
    {
      AJ_LOG(this, TEXT("InitAbilityActorInfo detected stale ActorInfo - forcing reinit"));
      bASCInitialised = false;
    }
  }

  if (bASCInitialised)

  {

    AJ_LOG(this,
           TEXT("InitAbilityActorInfo skipped - bASCInitialised already true"));

    CancelInitAbilityActorInfoRetry();

    return;
  }

  if (!AbilitySystemAeyerji)

  {

    AJ_LOG(this, TEXT("InitAbilityActorInfo failed - AbilitySystemAeyerji is "
                      "null (retry queued)"));

    QueueInitAbilityActorInfoRetry();

    return;
  }

  APlayerState *PS = GetPlayerState();

  if (!PS)

  {

    AJ_LOG(this, TEXT("InitAbilityActorInfo waiting - PlayerState is null "
                      "(retry queued)"));

    QueueInitAbilityActorInfoRetry();

    return;
  }

  CancelInitAbilityActorInfoRetry();

  AJ_LOG(this, TEXT("Initializing AbilityActorInfo with PlayerState: %s"),
         *GetNameSafe(PS));

  AbilitySystemAeyerji->InitAbilityActorInfo(this, this);

  // Ensure the AttributeSet instance exists so downstream code (load/leveling)

  // can read/write attributes immediately.

  if (!AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>())

  {

    UAeyerjiAttributeSet *NewSet = NewObject<UAeyerjiAttributeSet>(this);

    AbilitySystemAeyerji->AddAttributeSetSubobject(NewSet);
  }

  // Hook death delegate (server only)

  BindDeathEvent();

  // Configure leveling component from BP (if present)

  if (UAeyerjiLevelingComponent *Leveling =
          FindComponentByClass<UAeyerjiLevelingComponent>())

  {

    CachedLevelingComponent = Leveling;

    if (HasAuthority())

    {

      if (GE_PrimaryAttributes_Infinite)

      {

        Leveling->AddReapplyInfiniteEffect(GE_PrimaryAttributes_Infinite);
      }

      const int32 StartLvl = FMath::Max(1, StartLevelOnBeginPlay);

      Leveling->SetLevel(StartLvl); // also refreshes infinite effects/abilities
    }

  }

  else

  {

#if !(UE_BUILD_SHIPPING)

    UE_LOG(LogTemp, Error,
           TEXT("%s has no UAeyerjiLevelingComponent (expected on BP child)."),
           *GetName());

    if (GEngine)

    {

      GEngine->AddOnScreenDebugMessage(
          -1, 5.f, FColor::Red,

          FString::Printf(TEXT("%s: Missing AeyerjiLeveling component"),
                          *GetName()));
    }

#endif
  }

  bASCInitialised = true;

  AJ_LOG(
      this,
      TEXT(
          "InitAbilityActorInfo completed, broadcasting OnAbilitySystemReady"));

  OnAbilitySystemReady.Broadcast();

  HandleASCReady();

  AJ_LOG(this, TEXT("InitAbilityActorInfo finished"));
}

void APlayerParentNative::QueueInitAbilityActorInfoRetry() {

  if (bASCInitialised || bASCInitRetryQueued)

  {

    return;
  }

  if (UWorld *World = GetWorld())

  {

    bASCInitRetryQueued = true;

    World->GetTimerManager().SetTimer(
        ASCInitRetryHandle, this,
        &APlayerParentNative::RetryInitAbilityActorInfo, 0.1f, false);
  }
}

void APlayerParentNative::CancelInitAbilityActorInfoRetry()

{

  if (!bASCInitRetryQueued)

  {

    return;
  }

  if (UWorld *World = GetWorld())

  {

    World->GetTimerManager().ClearTimer(ASCInitRetryHandle);
  }

  bASCInitRetryQueued = false;
}

void APlayerParentNative::RetryInitAbilityActorInfo()

{

  bASCInitRetryQueued = false;

  InitAbilityActorInfo();
}

/* ------------------  DEATH  ------------------ */

void APlayerParentNative::OnDeath_Implementation()

{

  // Default native reaction (rag-doll, disable input).
}

float APlayerParentNative::GetHealthPercent()

{

  // TODO Fix this placeholder

  return 69.69f;
}

void APlayerParentNative::HandleASCReady()

{

  EnsureInventoryComponent();

  AJ_LOG(this,
         TEXT("HandleASCReady - HasAuthority: %d, IsLocallyControlled: %d, "
              "NetMode: %d"),

         HasAuthority(), IsLocallyControlled(), (int32)GetNetMode());

  if (HasAuthority())

  {

    AJ_LOG(this, TEXT("HandleASCReady - Adding startup abilities (server)"));

    AddStartupAbilities();
  }

  if (IsLocallyControlled())

  {

    AJ_LOG(
        this,
        TEXT(
            "HandleASCReady - Requesting character load (locally controlled)"));

    Server_RequestLoadCharacter();
  }

  AJ_LOG(this, TEXT("HandleASCReady completed"));
}

void APlayerParentNative::Server_RequestLoadCharacter_Implementation()

{

  const FString Slot =

      UCharacterStatsLibrary::MakeStableCharSlotName(GetPlayerState());

  bool bLoadedOK = false;

  if (AAeyerjiPlayerState *PS = GetPlayerState<AAeyerjiPlayerState>())

  {

    bool bLoadedExisting = false;
    if (UAeyerjiSaveGame *Data = UCharacterStatsLibrary::LoadOrCreateAeyerjiSave(Slot, bLoadedExisting))

    {

      if (AbilitySystemAeyerji)

      {
        UE_LOG(LogTemp, Display,
               TEXT("Server_RequestLoadCharacter: Slot=%s Existing=%d PreResetXP=%f PreResetLevel=%d"),
               *Slot, bLoadedExisting, Data->Attributes.XP, Data->Attributes.Level);

        if (!bLoadedExisting)
        {
          // Brand-new slot: ensure the save data starts from clean defaults
          Data->ActionBar.Reset();
          Data->Attributes = FAttrSnapshot();

          UE_LOG(LogTemp, Display,
                 TEXT("Server_RequestLoadCharacter: Slot=%s newly created -> zeroing XP/Level"),
                 *Slot);
        }

        // Always load from the save object so fresh slots use their defaults
        UCharacterStatsLibrary::LoadAeyerjiChar(Data, PS, AbilitySystemAeyerji);

        if (const UAeyerjiAttributeSet *AttrSet = AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>())
        {
          UE_LOG(LogTemp, Display,
                 TEXT("Server_RequestLoadCharacter: Slot=%s post-load ASC XP=%f Level=%f"),
                 *Slot, AttrSet->GetXP(), AttrSet->GetLevel());
        }

        if (!bLoadedExisting)
        {
          // Persist the freshly initialized defaults to disk for brand new slots
          UCharacterStatsLibrary::SaveAeyerjiChar(Data, PS, Slot);
        }

        bLoadedOK = true;

      }

      else

      {

        UE_LOG(LogTemp, Warning, TEXT("Server_RequestLoadCharacter: AbilitySystemAeyerji is null for slot %s"), *Slot);

      }

    }

  }

  Client_OnSaveLoaded(bLoadedOK);
}

void APlayerParentNative::Client_OnSaveLoaded_Implementation(bool bSuccess)

{

  TryLoadingSave();

  if (bSuccess)

  {

    AJ_LOG(this, TEXT("Save-game loaded & replicated"));

  }

  else

  {

    AJ_LOG(this, TEXT("Save-game load failed"));
  }
}

UAeyerjiLevelingComponent *APlayerParentNative::GetLevelingComponent() const

{

  if (CachedLevelingComponent)

  {

    return CachedLevelingComponent;
  }

  if (UAeyerjiLevelingComponent *Comp =
          FindComponentByClass<UAeyerjiLevelingComponent>())

  {

    const_cast<APlayerParentNative *>(this)->CachedLevelingComponent = Comp;

    return Comp;
  }

  return nullptr;
}
