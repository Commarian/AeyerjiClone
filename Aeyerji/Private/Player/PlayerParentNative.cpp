// PlayerParentNative.cpp

// ReSharper disable CppTooWideScopeInitStatement

#include "Player/PlayerParentNative.h"

#include "CharacterStatsLibrary.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiStatEngineComponent.h"
#include "Components/AeyerjiPickupFXComponent.h"
#include "Components/WeaponEquipmentComponent.h"
#include "Items/InventoryComponent.h"

#include "Progression/AeyerjiLevelingComponent.h"

#include "Aeyerji/AeyerjiPlayerController.h"

#include "Aeyerji/AeyerjiPlayerState.h"
#include "Aeyerji/AeyerjiGameState.h"

#include "Aeyerji/AeyerjiSaveGame.h"

#include "Blueprint/UserWidget.h"

#include "GameFramework/PlayerState.h"

#include "GUI/W_ActionBar.h"

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

#include "Logging/AeyerjiLog.h"

#include "Player/PlayerPathAIController.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiDifficultyTuning.h"

static const FName RHandSocket(TEXT("WeaponRHandSocket"));

APlayerParentNative::APlayerParentNative()

{

  bReplicates = true;

  bReplicateUsingRegisteredSubObjectList = true;

  if (AbilitySystemAeyerji)
  {
    // Prefer reliability for player ASC initial state over bandwidth savings.
    AbilitySystemAeyerji->SetReplicationMode(EGameplayEffectReplicationMode::Full);
  }

  RHandMeshComp =
      CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RHandMeshComp"));

  RHandMeshComp->SetupAttachment(GetMesh(), RHandSocket);

  RHandMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  RHandMeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
  RHandMeshComp->SetGenerateOverlapEvents(false);
  RHandMeshComp->SetCanEverAffectNavigation(false);

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

  TInlineComponentArray<UAeyerjiInventoryComponent*> InventoryComponents(this);
  if (InventoryComponents.Num() > 1)
  {
    AJ_LOG(this,
           TEXT("[InventoryPickup] Multiple inventory components found on %s Count=%d PreferredName=%s"),
           *GetName(),
           InventoryComponents.Num(),
           *InventoryComponentName.ToString());
  }

  for (UAeyerjiInventoryComponent* Existing : InventoryComponents)
  {
    if (!Existing)
    {
      continue;
    }

    if (InventoryComponentName.IsNone() || Existing->GetFName() == InventoryComponentName)
    {
      InventoryComponent = Existing;
      AJ_LOG(this,
             TEXT("[InventoryPickup] Resolved inventory component by name Owner=%s Inventory=%s Replicated=%d"),
             *GetName(),
             *GetNameSafe(InventoryComponent),
             InventoryComponent->GetIsReplicated() ? 1 : 0);
      return InventoryComponent;
    }
  }

  if (InventoryComponents.Num() > 0)
  {
    InventoryComponent = InventoryComponents[0];
    AJ_LOG(this,
           TEXT("[InventoryPickup] InventoryComponent not found by name '%s' on %s (found %d inventory component(s)); using %s."),
           *InventoryComponentName.ToString(),
           *GetName(),
           InventoryComponents.Num(),
           *GetNameSafe(InventoryComponent));
    return InventoryComponent;
  }
  else
  {
    AJ_LOG(this,
           TEXT("[InventoryPickup] InventoryComponent '%s' missing on %s. Add it in the Blueprint."),
           *InventoryComponentName.ToString(), *GetName());
  }

  return nullptr;
}

void APlayerParentNative::HandleInventoryComponentResolved(
    UAeyerjiInventoryComponent* ResolvedComponent)

{

  if (!ResolvedComponent)
  {
    return;
  }

  if (!ResolvedComponent->GetIsReplicated())
  {
    AJ_LOG(this,
           TEXT("[InventoryPickup] Enabling replication on inventory component Owner=%s Inventory=%s"),
           *GetName(),
           *GetNameSafe(ResolvedComponent));
    ResolvedComponent->SetIsReplicated(true);
  }

  if (bReplicateUsingRegisteredSubObjectList)
  {
    AddReplicatedSubObject(ResolvedComponent);
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
    AJ_LOG(this,
           TEXT("[InventoryPickup] Inventory component bound Owner=%s Inventory=%s Items=%d Equipped=%d Grid=%d"),
           *GetName(),
           *GetNameSafe(InventoryComponent),
           InventoryComponent->Items.Num(),
           InventoryComponent->EquippedItems.Num(),
           InventoryComponent->GridPlacements.Num());
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

  // Only the authoritative live pawn should write the save. During respawn the corpse's EndPlay
  // runs after possession has moved to the replacement pawn, so saving here would overwrite the slot
  // with stale or partially restored data.
  if (HasAuthority())

  {

    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())

    {
      if (APawn* CurrentPawn = PS->GetPawn(); CurrentPawn && CurrentPawn != this)
      {
        UE_LOG(LogTemp, Display,
               TEXT("[ProfileCheckpoint] Skip Reason=PawnEndPlay Pawn=%s CurrentPawn=%s Detail=StalePawnAfterRespawn"),
               *GetNameSafe(this),
               *GetNameSafe(CurrentPawn));
      }
      else
      {
        CaptureAndPushAuthoritativeProfile(this, EAeyerjiSaveCheckpointReason::PawnEndPlay, /*bBumpRevision=*/true);
      }
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

  // Collapse duplicate main attribute sets caused by ASC DefaultStartingData plus actor-owned subobjects.
  EnsurePrimaryAttributeSetRegistered();

  // Hook death delegate (server only)

  BindDeathEvent();
  BindCrowdControlEvents();
  BindRuntimeAttributeHooks();

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
        // Apply immediately on the authority path so freshly possessed pawns do not
        // spend a frame using constructor fallback combat stats.
        Leveling->ForceRefreshForCurrentLevel();
      }

      if (StatEngine)
      {
        StatEngine->EnsureRegenerationActive();
      }
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
  // Native death presentation is handled externally; keep the player hook logic-only.
  if (HasAuthority())
  {
    if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
    {
      GameState->Server_NotifyPlayerDeath(GetPlayerState<AAeyerjiPlayerState>());
    }
  }
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

    if (!bSaveLoaded && ApplyServerCachedProfile())
    {
      AJ_LOG(this, TEXT("HandleASCReady - Applied cached authoritative profile"));
      return;
    }
  }

  if (IsLocallyControlled())

  {

    if (!bSaveLoaded && !bSaveLoadRequested)
    {
      bSaveLoadRequested = true;
      if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
      {
        PS->SetProfileLoadState(EAeyerjiProfileLoadState::Pending);
      }
      AJ_LOG(
          this,
          TEXT(
              "HandleASCReady - Resolving local/cloud profile (locally controlled)"));
      BeginResolveAndSendProfile();
    }
  }

  AJ_LOG(this, TEXT("HandleASCReady completed"));
}

void APlayerParentNative::Server_RequestLoadCharacter_Implementation()

{
  if (bSaveLoaded || ApplyServerCachedProfile())
  {
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Applied);
    }
    Client_OnSaveLoaded(true);
    return;
  }

  if (IsLocallyControlled())
  {
    BeginResolveAndSendProfile();
    return;
  }

  UE_LOG(LogTemp, Display,
         TEXT("Server_RequestLoadCharacter: Waiting for owning client profile snapshot for %s"),
         *GetNameSafe(this));
}

void APlayerParentNative::BeginResolveAndSendProfile()

{
  AAeyerjiPlayerState* PreferredPS = GetPlayerState<AAeyerjiPlayerState>();

  UWorld* World = GetWorld();
  if (!World)
  {
    bSaveLoadRequested = false;
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  UGameInstance* GameInstance = World->GetGameInstance();
  if (!GameInstance)
  {
    bSaveLoadRequested = false;
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
  if (!SaveManager)
  {
    bSaveLoadRequested = false;
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  TWeakObjectPtr<APlayerParentNative> WeakThis(this);
  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] ClientResolve Begin Pawn=%s PlayerState=%s SaveSlotOverride=%s"),
         *GetNameSafe(this),
         *GetNameSafe(PreferredPS),
         PreferredPS ? *PreferredPS->GetSaveSlotOverride() : TEXT("None"));

  SaveManager->ResolveProfileForLocalOwner(
      FAeyerjiOnProfileResolved::CreateLambda(
          [WeakThis](const bool bSuccess, const bool bHadPersistedData, UAeyerjiSaveGame* SaveData)
          {
            if (!WeakThis.IsValid())
            {
              return;
            }

            APlayerParentNative* Self = WeakThis.Get();
            if (!Self)
            {
              return;
            }

            UWorld* LocalWorld = Self->GetWorld();
            UGameInstance* LocalGameInstance = LocalWorld ? LocalWorld->GetGameInstance() : nullptr;
            UAeyerjiSaveManagerSubsystem* LocalSaveManager =
                LocalGameInstance ? LocalGameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
            if (!bSuccess || !SaveData || !LocalSaveManager)
            {
              Self->bSaveLoadRequested = false;
              if (AAeyerjiPlayerState* PS = Self->GetPlayerState<AAeyerjiPlayerState>())
              {
                PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
              }
              Self->Client_OnSaveLoaded(false);
              return;
            }

            const AAeyerjiPlayerState* ResolvedPS = Self->GetPlayerState<AAeyerjiPlayerState>();
            const FString ExplicitSaveSlot = ResolvedPS
                ? UCharacterStatsLibrary::SanitizeSaveSlotName(ResolvedPS->GetSaveSlotOverride())
                : FString();

            UE_LOG(LogTemp, Display,
                   TEXT("[ProfileLoad] ClientResolve Result Pawn=%s Success=1 Persisted=%d Owner=%s ExplicitSlot=%s Revision=%lld Items=%d Equipped=%d Grid=%d"),
                   *GetNameSafe(Self),
                   bHadPersistedData ? 1 : 0,
                   *SaveData->OwnerKey,
                   *ExplicitSaveSlot,
                   SaveData->Revision,
                   SaveData->Inventory.ItemSnapshots.Num(),
                   SaveData->Inventory.EquippedItems.Num(),
                   SaveData->Inventory.GridPlacements.Num());

            FAeyerjiSaveTransportHeader Header;
            TArray<uint8> Bytes;
            if (bHadPersistedData)
            {
              if (!LocalSaveManager->BuildTransportFromProfile(SaveData, Header, Bytes))
              {
                Self->bSaveLoadRequested = false;
                if (AAeyerjiPlayerState* PS = Self->GetPlayerState<AAeyerjiPlayerState>())
                {
                  PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
                }
                Self->Client_OnSaveLoaded(false);
                return;
              }
            }
            else
            {
              Header.ArtifactKind = EAeyerjiSaveArtifactKind::Profile;
              Header.OwnerKey = SaveData->OwnerKey;
              Header.SchemaVersion = SaveData->SchemaVersion;
              Header.Revision = SaveData->Revision;
              Header.LastModifiedUtc = SaveData->LastModifiedUtc;
              Header.bHadPersistedData = false;
            }

            if (!ExplicitSaveSlot.IsEmpty())
            {
              Header.ExplicitSaveSlotOverride = ExplicitSaveSlot;
              Header.OwnerKey = ExplicitSaveSlot;
            }

            Self->SendResolvedProfileToServer(Header, Bytes, bHadPersistedData);
          }),
      PreferredPS);
}

bool APlayerParentNative::ApplyServerCachedProfile()

{
  if (!HasAuthority() || bSaveLoaded || !AbilitySystemAeyerji)
  {
    return false;
  }

  UWorld* World = GetWorld();
  UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
  UAeyerjiSaveManagerSubsystem* SaveManager =
      GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
  AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>();
  if (!SaveManager || !PS)
  {
    return false;
  }

  UAeyerjiSaveGame* CachedProfile = nullptr;
  if (!SaveManager->GetServerCachedProfile(PS, CachedProfile) || !CachedProfile)
  {
    UE_LOG(LogTemp, Verbose,
           TEXT("[ProfileLoad] State=Pending Phase=CachedApply Pawn=%s Reason=NoCachedProfile"),
           *GetNameSafe(this));
    return false;
  }

  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] State=Applying Phase=CachedApply Pawn=%s Revision=%lld OwnerKey=%s"),
         *GetNameSafe(this),
         CachedProfile->Revision,
         *CachedProfile->OwnerKey);
  PS->SetProfileLoadState(EAeyerjiProfileLoadState::Applying);
  UCharacterStatsLibrary::LoadAeyerjiChar(CachedProfile, PS, AbilitySystemAeyerji);
  bSaveLoaded = true;

  AbilitySystemAeyerji->ForceReplication();
  ForceNetUpdate();
  if (AController* OwningController = GetController())
  {
    OwningController->ForceNetUpdate();
  }

  Client_OnSaveLoaded(true);
  PS->SetProfileLoadState(EAeyerjiProfileLoadState::Applied);
  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] State=Applied Phase=CachedApply Pawn=%s Revision=%lld"),
         *GetNameSafe(this),
         CachedProfile->Revision);
  return true;
}

void APlayerParentNative::SendResolvedProfileToServer(
    const FAeyerjiSaveTransportHeader& Header,
    const TArray<uint8>& Bytes,
    const bool bHadPersistedData)
{
  const int32 TotalBytes = Bytes.Num();
  if (TotalBytes > LegacyProfileRpcWarningBytes)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] Resolved profile payload is %d bytes for %s; using chunked transport to avoid legacy RPC array limits."),
           TotalBytes,
           *GetNameSafe(this));
  }

  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] ClientTransfer Begin Pawn=%s Owner=%s ExplicitSlot=%s Persisted=%d Revision=%lld PayloadBytes=%d"),
         *GetNameSafe(this),
         *Header.OwnerKey,
         *Header.ExplicitSaveSlotOverride,
         bHadPersistedData ? 1 : 0,
         Header.Revision,
         TotalBytes);

  Server_BeginResolvedProfileTransfer(Header, TotalBytes, ProfileTransportChunkSize, bHadPersistedData);

  for (int32 Offset = 0, ChunkIndex = 0; Offset < TotalBytes; Offset += ProfileTransportChunkSize, ++ChunkIndex)
  {
    const int32 ChunkBytes = FMath::Min(ProfileTransportChunkSize, TotalBytes - Offset);
    TArray<uint8> Chunk;
    Chunk.Append(Bytes.GetData() + Offset, ChunkBytes);
    Server_SendResolvedProfileChunk(ChunkIndex, Chunk);
  }

  Server_FinalizeResolvedProfileTransfer();
}

bool APlayerParentNative::CaptureAndPushAuthoritativeProfile(
    const APawn* SourcePawn,
    const EAeyerjiSaveCheckpointReason Reason,
    const bool bBumpRevision)

{
  if (!HasAuthority())
  {
    return false;
  }

  AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>();
  if (!PS)
  {
    return false;
  }

  return PS->CommitCheckpointProfileFromPawn(Reason, SourcePawn, bBumpRevision);
}

void APlayerParentNative::Server_ApplyResolvedProfile_Implementation(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes, const bool bHadPersistedData)

{
  UE_LOG(LogTemp, Warning,
         TEXT("[ProfileLoad] Legacy single-RPC profile apply received for %s PayloadBytes=%d. Prefer chunked transfer."),
         *GetNameSafe(this),
         Bytes.Num());
  ApplyResolvedProfilePayload(Header, Bytes, bHadPersistedData);
}

void APlayerParentNative::Server_BeginResolvedProfileTransfer_Implementation(
    const FAeyerjiSaveTransportHeader& Header,
    const int32 TotalBytes,
    const int32 ChunkSize,
    const bool bHadPersistedData)
{
  ResetPendingProfileTransfer();

  const int32 SafeChunkSize = FMath::Clamp(ChunkSize, 1, ProfileTransportChunkSize);
  AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>();
  if (bSaveLoaded || !AbilitySystemAeyerji)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=BeginTransfer Pawn=%s Loaded=%d ASC=%s"),
           *GetNameSafe(this),
           bSaveLoaded ? 1 : 0,
           *GetNameSafe(AbilitySystemAeyerji));
    if (PS)
    {
      PS->SetProfileLoadState(bSaveLoaded ? EAeyerjiProfileLoadState::Applied : EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(bSaveLoaded);
    return;
  }

  if (TotalBytes < 0)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=BeginTransfer Pawn=%s Reason=NegativeSize Size=%d"),
           *GetNameSafe(this),
           TotalBytes);
    if (PS)
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  PendingProfileTransferHeader = Header;
  PendingProfileTransferExpectedBytes = TotalBytes;
  PendingProfileTransferChunkSize = SafeChunkSize;
  PendingProfileTransferExpectedChunks = TotalBytes > 0 ? FMath::DivideAndRoundUp(TotalBytes, SafeChunkSize) : 0;
  PendingProfileTransferReceivedChunks.Reset();
  PendingProfileTransferBytes.Reset();
  PendingProfileTransferBytes.SetNumZeroed(TotalBytes);
  bPendingProfileTransferHadPersistedData = bHadPersistedData;
  bProfileTransferActive = true;
  if (PS)
  {
    PS->SetProfileLoadState(EAeyerjiProfileLoadState::Pending);
  }

  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] State=Pending Phase=BeginTransfer Pawn=%s Owner=%s ExplicitSlot=%s PayloadBytes=%d Chunks=%d Persisted=%d Revision=%lld"),
         *GetNameSafe(this),
         *Header.OwnerKey,
         *Header.ExplicitSaveSlotOverride,
         TotalBytes,
         PendingProfileTransferExpectedChunks,
         bHadPersistedData ? 1 : 0,
         Header.Revision);
}

void APlayerParentNative::Server_SendResolvedProfileChunk_Implementation(const int32 ChunkIndex, const TArray<uint8>& ChunkBytes)
{
  if (!bProfileTransferActive)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Chunk Pawn=%s Reason=NoActiveTransfer Chunk=%d Bytes=%d"),
           *GetNameSafe(this),
           ChunkIndex,
           ChunkBytes.Num());
    return;
  }

  if (ChunkIndex < 0 || ChunkIndex >= PendingProfileTransferExpectedChunks)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Chunk Pawn=%s Reason=BadIndex Chunk=%d ExpectedChunks=%d"),
           *GetNameSafe(this),
           ChunkIndex,
           PendingProfileTransferExpectedChunks);
    ResetPendingProfileTransfer();
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  if (PendingProfileTransferReceivedChunks.Contains(ChunkIndex))
  {
    UE_LOG(LogTemp, Verbose,
           TEXT("[ProfileLoad] Duplicate profile chunk ignored Pawn=%s Chunk=%d"),
           *GetNameSafe(this),
           ChunkIndex);
    return;
  }

  const int32 Offset = ChunkIndex * PendingProfileTransferChunkSize;
  const int32 ExpectedBytes = FMath::Min(PendingProfileTransferChunkSize, PendingProfileTransferExpectedBytes - Offset);
  if (ChunkBytes.Num() != ExpectedBytes)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Chunk Pawn=%s Reason=BadChunkSize Chunk=%d Bytes=%d Expected=%d"),
           *GetNameSafe(this),
           ChunkIndex,
           ChunkBytes.Num(),
           ExpectedBytes);
    ResetPendingProfileTransfer();
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  if (ExpectedBytes > 0)
  {
    FMemory::Memcpy(PendingProfileTransferBytes.GetData() + Offset, ChunkBytes.GetData(), ExpectedBytes);
  }
  PendingProfileTransferReceivedChunks.Add(ChunkIndex);
}

void APlayerParentNative::Server_FinalizeResolvedProfileTransfer_Implementation()
{
  if (!bProfileTransferActive)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Finalize Pawn=%s Reason=NoActiveTransfer"),
           *GetNameSafe(this));
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  if (PendingProfileTransferReceivedChunks.Num() != PendingProfileTransferExpectedChunks)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Finalize Pawn=%s Reason=MissingChunks Received=%d Expected=%d"),
           *GetNameSafe(this),
           PendingProfileTransferReceivedChunks.Num(),
           PendingProfileTransferExpectedChunks);
    ResetPendingProfileTransfer();
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return;
  }

  const FAeyerjiSaveTransportHeader Header = PendingProfileTransferHeader;
  const TArray<uint8> Bytes = PendingProfileTransferBytes;
  const bool bHadPersistedData = bPendingProfileTransferHadPersistedData;
  ResetPendingProfileTransfer();

  ApplyResolvedProfilePayload(Header, Bytes, bHadPersistedData);
}

bool APlayerParentNative::ApplyResolvedProfilePayload(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes, const bool bHadPersistedData)
{
  if (bSaveLoaded || !AbilitySystemAeyerji)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Apply Pawn=%s Loaded=%d ASC=%s"),
           *GetNameSafe(this),
           bSaveLoaded ? 1 : 0,
           *GetNameSafe(AbilitySystemAeyerji));
    if (AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>())
    {
      PS->SetProfileLoadState(bSaveLoaded ? EAeyerjiProfileLoadState::Applied : EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(bSaveLoaded);
    return bSaveLoaded;
  }

  UWorld* World = GetWorld();
  UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
  UAeyerjiSaveManagerSubsystem* SaveManager =
      GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
  AAeyerjiPlayerState* PS = GetPlayerState<AAeyerjiPlayerState>();
  if (!SaveManager || !PS)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Apply Pawn=%s SaveManager=%s PlayerState=%s"),
           *GetNameSafe(this),
           *GetNameSafe(SaveManager),
           *GetNameSafe(PS));
    if (PS)
    {
      PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    }
    Client_OnSaveLoaded(false);
    return false;
  }

  const FString ExplicitSaveSlot = UCharacterStatsLibrary::SanitizeSaveSlotName(Header.ExplicitSaveSlotOverride);
  if (!ExplicitSaveSlot.IsEmpty())
  {
    PS->RequestSetSaveSlotOverride(ExplicitSaveSlot);
  }

  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] State=Applying Pawn=%s Owner=%s ExplicitSlot=%s ServerSlot=%s Persisted=%d PayloadBytes=%d Revision=%lld"),
         *GetNameSafe(this),
         *Header.OwnerKey,
         *ExplicitSaveSlot,
         *PS->GetSaveSlotOverride(),
         bHadPersistedData ? 1 : 0,
         Bytes.Num(),
         Header.Revision);
  PS->SetProfileLoadState(EAeyerjiProfileLoadState::Applying);

  UAeyerjiSaveGame* Data = nullptr;
  if (bHadPersistedData)
  {
    Data = SaveManager->DeserializeProfileFromTransport(Header, Bytes);
  }

  if (!Data)
  {
    Data = SaveManager->CreateDefaultProfile(SaveManager->ResolveOwnerKey(PS), StartLevelOnBeginPlay);
    if (Data)
    {
      Data->Attributes.Level = UAeyerjiDifficultySettings::ClampGameplayLevel(StartLevelOnBeginPlay);
    }
  }

  if (!Data)
  {
    UE_LOG(LogTemp, Warning,
           TEXT("[ProfileLoad] State=Failed Phase=Apply Pawn=%s Reason=NoProfileData"),
           *GetNameSafe(this));
    PS->SetProfileLoadState(EAeyerjiProfileLoadState::Failed);
    Client_OnSaveLoaded(false);
    return false;
  }

  UCharacterStatsLibrary::LoadAeyerjiChar(Data, PS, AbilitySystemAeyerji);

  const bool bNeedsImmediateCommit = !bHadPersistedData || !SaveManager->IsManagerEraProfile(Data);
  if (bNeedsImmediateCommit)
  {
    UE_LOG(LogTemp, Display, TEXT("APlayerParentNative: committing created/migrated profile checkpoint for %s"), *GetNameSafe(PS));
    PS->CommitPreparedCheckpointProfile(Data, EAeyerjiSaveCheckpointReason::ProfileCreatedOrMigrated, /*bBumpRevision=*/true);
  }
  else
  {
    FAeyerjiSaveTransportHeader IgnoredHeader;
    TArray<uint8> IgnoredBytes;
    SaveManager->PrepareProfileForServerCommit(PS, Data, /*bBumpRevision=*/false, IgnoredHeader, IgnoredBytes);
  }

  bSaveLoaded = true;

  AbilitySystemAeyerji->ForceReplication();
  ForceNetUpdate();
  if (AController* OwningController = GetController())
  {
    OwningController->ForceNetUpdate();
  }

  Client_OnSaveLoaded(true);
  PS->SetProfileLoadState(EAeyerjiProfileLoadState::Applied);
  UE_LOG(LogTemp, Display,
         TEXT("[ProfileLoad] State=Applied Pawn=%s OwnerKey=%s Revision=%lld Persisted=%d"),
         *GetNameSafe(this),
         *Data->OwnerKey,
         Data->Revision,
         bHadPersistedData ? 1 : 0);
  return true;
}

void APlayerParentNative::ResetPendingProfileTransfer()
{
  PendingProfileTransferHeader = FAeyerjiSaveTransportHeader();
  PendingProfileTransferBytes.Reset();
  PendingProfileTransferReceivedChunks.Reset();
  PendingProfileTransferExpectedBytes = 0;
  PendingProfileTransferExpectedChunks = 0;
  PendingProfileTransferChunkSize = 0;
  bPendingProfileTransferHadPersistedData = false;
  bProfileTransferActive = false;
}

void APlayerParentNative::Client_OnSaveLoaded_Implementation(bool bSuccess)

{

  TryLoadingSave();

  bSaveLoaded = bSuccess;
  if (!bSuccess)
  {
    bSaveLoadRequested = false;
  }

  if (bSuccess)

  {
    // Fire a second ASC-ready signal after server-side load has completed.
    // UI listeners can re-pull authoritative attributes here.
    OnAbilitySystemReady.Broadcast();

    if (AbilitySystemAeyerji)
    {
      if (const UAeyerjiAttributeSet *Attr = AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>())
      {
        AJ_LOG(this, TEXT("Client_OnSaveLoaded: Post-load vitals HP=%0.1f/%0.1f Mana=%0.1f/%0.1f AttackSpeed=%0.2f AttackCooldown=%0.2f"),
               Attr->GetHP(), Attr->GetHPMax(), Attr->GetMana(), Attr->GetManaMax(),
               Attr->GetAttackSpeed(), Attr->GetAttackCooldown());
      }
    }

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
