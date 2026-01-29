#include "AeyerjiCharacter.h"
#include "Abilities/AeyerjiRagdollHelpers.h"
#include "Abilities/GA_Death.h" // your passive death GA
#include "Abilities/GA_PrimaryMeleeBasic.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiStatEngineComponent.h"
#include "CharacterStatsLibrary.h"
#include "Components/ActorComponent.h"
#include "Components/AeyerjiPickupFXComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "GUI/AeyerjiFloatingStatusBarComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/AeyerjiLog.h"
#include "AeyerjiGameplayTags.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "TimerManager.h"

TArray<TWeakObjectPtr<AAeyerjiCharacter>>
    AAeyerjiCharacter::CorpsesPendingCleanup;
AAeyerjiCharacter::AAeyerjiCharacter(
    const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer
                .SetDefaultSubobjectClass<UAeyerjiCharacterMovementComponent>(
                    ACharacter::CharacterMovementComponentName)) {
  // ------------------------------------------------------------------
  // Ability System Component
  // ------------------------------------------------------------------
  AbilitySystemAeyerji = CreateDefaultSubobject<UAbilitySystemComponent>(
      TEXT("AbilitySystemAeyerji"));
  AbilitySystemAeyerji->SetIsReplicated(true);
  AbilitySystemAeyerji->SetReplicationMode(
      EGameplayEffectReplicationMode::Mixed);
  bReplicates = true;
  bReplicateUsingRegisteredSubObjectList = true;
  // Compute & apply derived stats via GAS
  StatEngine =
      CreateDefaultSubobject<UAeyerjiStatEngineComponent>(TEXT("StatEngine"));
  PickupFXComponent =
      CreateDefaultSubobject<UAeyerjiPickupFXComponent>(TEXT("PickupFXComponent"));

  // Default death ability (can be overridden in BP)
  if (!DeathAbilityClass)
  {
    DeathAbilityClass = UGA_Death::StaticClass();
  }
}
void AAeyerjiCharacter::BeginPlay() { Super::BeginPlay(); }
/*void AAeyerjiCharacter::InitialiseAbilitySystem()
{
        if (bASCInitialised || !AbilitySystemAeyerji) return;
        bASCInitialised = true;
        AbilitySystemAeyerji->InitAbilityActorInfo(GetOwner() ? GetOwner() :
this,
                                                                                           /*Avatar=#1#this);
        if (HasAuthority())
                AddStartupAbilities();
        BindDeathEvent();
        //UE_LOG(LogTemp, Log, TEXT("Initialised AbilitySystemComponent for
%s"), *GetName()); OnAbilitySystemReady.Broadcast();
}*/
/* --------------------------- ASC init for players --------------------------
 */
/*void AAeyerjiCharacter::PossessedBy(AController* NewController)
{
        Super::PossessedBy(NewController);
        InitialiseAbilitySystem();
}*/
/* --------------------------- Startup GAS wiring --------------------------- */
void AAeyerjiCharacter::AddStartupAbilities() {
  AJ_LOG(this, TEXT("AddStartupAbilities - HasAuthority: %d"), HasAuthority());
  if (AbilitySystemAeyerji == nullptr)
    return;
  bool bGrantedAnyAbility = false;
  for (TSubclassOf<UGameplayAbility> AbilityClass : DefaultAbilities) {
    if (*AbilityClass) {
      FGameplayAbilitySpec Spec(AbilityClass, CharacterLevel);
      AbilitySystemAeyerji->GiveAbility(Spec);
      bGrantedAnyAbility = true;
    }
  }
  // Passive Death GA so every pawn can die/respawn
  TSubclassOf<UGameplayAbility> DeathClass = DeathAbilityClass ? DeathAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Death::StaticClass());
  if (DeathClass)
  {
    AbilitySystemAeyerji->GiveAbility(FGameplayAbilitySpec(DeathClass, 1));
  }
}
/*
void AAeyerjiCharacter::InitAttributes() const
{
        if (!AbilitySystemAeyerji)
        {
                //UE_LOG(LogTemp, Warning, TEXT("%s:
AAeyerjiCharacter::InitAttributes() Cannot initialize attributes -
AbilitySystemAeyerji is null"), *GetName()); return;
        }
        if (!DefaultAttributesGE)
        {
                //UE_LOG(LogTemp, Warning, TEXT("%s:
AAeyerjiCharacter::InitAttributes() DefaultAttributesGE not set in character
BP"), *GetName()); return;
        }
        FGameplayEffectContextHandle Cxt =
AbilitySystemAeyerji->MakeEffectContext(); Cxt.AddSourceObject(this);
        FGameplayEffectSpecHandle SpecHandle =
AbilitySystemAeyerji->MakeOutgoingSpec(DefaultAttributesGE, CharacterLevel,
Cxt); if (SpecHandle.IsValid())
        {
                FActiveGameplayEffectHandle EffectHandle =
AbilitySystemAeyerji->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get()); if
(EffectHandle.IsValid())
                {
                        //UE_LOG(LogTemp, Log, TEXT("%s: Successfully applied
DefaultAttributesGE (level %d)"), *GetName(), CharacterLevel);
                        // Verify attribute values were set correctly
                        if (const UAeyerjiAttributeSet* AttrSet =
AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>())
                        {
                                //UE_LOG(LogTemp, Log, TEXT("%s: Attributes
initialized - HP=%.1f/%.1f, Mana=%.1f/%.1f"), *GetName(), AttrSet->GetHP(),
AttrSet->GetHPMax(), AttrSet->GetMana(), AttrSet->GetManaMax());
                        }
                }
                else
                {
                        //UE_LOG(LogTemp, Warning, TEXT("%s: Failed to apply
DefaultAttributesGE"), *GetName());
                }
        }
        else
        {
                //UE_LOG(LogTemp, Warning, TEXT("%s: Invalid DefaultAttributesGE
spec"), *GetName());
        }
}
*/
void AAeyerjiCharacter::BindDeathEvent() {
  if (!HasAuthority())
    return; // server only
  if (UAbilitySystemComponent *ASC = GetAbilitySystemComponent()) {
    if (const UAeyerjiAttributeSet *StatsConst =
            ASC->GetSet<UAeyerjiAttributeSet>()) {
      UAeyerjiAttributeSet *Stats =
          const_cast<UAeyerjiAttributeSet *>(StatsConst); // cast once
      // Avoid duplicate binding across respawns/possessions.
      Stats->OnOutOfHealth.RemoveDynamic(this,
                                         &AAeyerjiCharacter::HandleOutOfHealth);
      Stats->OnOutOfHealth.AddDynamic(this,
                                      &AAeyerjiCharacter::HandleOutOfHealth);
    }
  }
}
/* ----------------------------- Death plumbing ----------------------------- */
void AAeyerjiCharacter::ApplyDeathState(FAeyerjiDeathStateOptions Options) {
  const bool bWasAlreadyDead = bHasAppliedDeathState;
  ApplyDeathStateInternal(Options);
  if (HasAuthority() && !bWasAlreadyDead) {
    MulticastApplyDeathState(Options);
  }
}
void AAeyerjiCharacter::ApplyDeathStateInternal(
    const FAeyerjiDeathStateOptions &Options)

{

  if (bHasAppliedDeathState)

  {

    return;
  }

  bHasAppliedDeathState = true;

  SetCanBeDamaged(false);

  if (HasAuthority())

  {

    if (UAbilitySystemComponent *ASC = GetAbilitySystemComponent())

    {

      ASC->AddLooseGameplayTag(
          FGameplayTag::RequestGameplayTag(TEXT("State.Dead")));
    }

    // Mirror the gameplay tag onto the actor Tag list so AI-side checks like
    // Tags.Contains("State.Dead") work and replicate to clients.
    const FGameplayTag DeadTag = AeyerjiTags::State_Dead;
    Tags.AddUnique(DeadTag.GetTagName());

    if (UAIPerceptionStimuliSourceComponent *Stim =
            FindComponentByClass<UAIPerceptionStimuliSourceComponent>())

    {

      Stim->UnregisterFromPerceptionSystem();
    }

    if (Options.bDetachAttachments)

    {

      DetachDestroyAttachedActors();
    }

    if (Options.bRegisterCorpseForCleanup)

    {

      RegisterCorpseForCleanup();
    }

  }

  else if (Options.bDetachAttachments)

  {

    TArray<AActor *> Attached;

    GetAttachedActors(Attached, /*bResetArray=*/true);

    for (AActor *Child : Attached)

    {

      if (!Child)

      {

        continue;
      }

      Child->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    }
  }

  if (Options.bRemoveFloatingWidgets)

  {

    RemoveFloatingWidgets();
  }

  if (Options.bStopRegeneration)

  {

    StopRegeneration();
  }

  if (UCapsuleComponent *Capsule = GetCapsuleComponent())

  {

    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Capsule->SetGenerateOverlapEvents(false);
  }

  FAeyerjiRagdollHelpers::StartRagdoll(this, Options.Impulse,
                                       Options.ImpulseWorldLocation,
                                       Options.ImpulseBoneName);

  if (USkeletalMeshComponent *MeshComponent = GetMesh())

  {

    MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));

    MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

    MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

    MeshComponent->SetGenerateOverlapEvents(false);
  }

  if (Options.bDisableRagdollCollision)

  {

    ScheduleRagdollCollisionDisable(Options.RagdollCollisionDisableDelay);
  }
}

void AAeyerjiCharacter::MulticastApplyDeathState_Implementation(
    FAeyerjiDeathStateOptions Options)

{

  if (HasAuthority())

  {

    return;
  }

  ApplyDeathStateInternal(Options);
}

void AAeyerjiCharacter::MulticastOnDeath_Implementation(AActor *Killer, float DamageTaken)

{

  if (HasAuthority())

  {

    return;
  }

  BP_OnDeath(Killer, DamageTaken);
}

void AAeyerjiCharacter::RemoveFloatingWidgets()

{

  TInlineComponentArray<UAeyerjiFloatingStatusBarComponent *>
      FloatingStatusComponents(this);

  for (UAeyerjiFloatingStatusBarComponent *Component : FloatingStatusComponents)

  {

    if (Component)

    {

      Component->DestroyComponent();
    }
  }
}

void AAeyerjiCharacter::StopRegeneration()

{

  if (!HasAuthority())

  {

    return;
  }

  if (StatEngine)

  {

    StatEngine->StopRegeneration();
  }

  if (UAbilitySystemComponent *ASC = GetAbilitySystemComponent())

  {

    ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPRegenAttribute(),
                                 0.f);

    ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetManaRegenAttribute(),
                                 0.f);
  }
}

void AAeyerjiCharacter::ScheduleRagdollCollisionDisable(float DelaySeconds)

{

  GetWorldTimerManager().ClearTimer(RagdollCollisionDisableHandle);

  if (DelaySeconds <= KINDA_SMALL_NUMBER)

  {

    DisableRagdollCollisionNow();

    return;
  }

  GetWorldTimerManager().SetTimer(
      RagdollCollisionDisableHandle, this,
      &AAeyerjiCharacter::DisableRagdollCollisionNow, DelaySeconds, false);
}

void AAeyerjiCharacter::DisableRagdollCollisionNow()

{

  GetWorldTimerManager().ClearTimer(RagdollCollisionDisableHandle);

  if (USkeletalMeshComponent *MeshComponent = GetMesh())

  {

    const FTransform FinalPose = MeshComponent->GetComponentTransform();

    if (UCapsuleComponent *Capsule = GetCapsuleComponent())

    {

      Capsule->SetWorldLocationAndRotation(
          FinalPose.GetLocation(), FinalPose.GetRotation().Rotator(), false,
          nullptr, ETeleportType::TeleportPhysics);
    }

    MeshComponent->SetSimulatePhysics(false);

    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

    MeshComponent->SetGenerateOverlapEvents(false);
  }

  SetActorEnableCollision(false);
}

void AAeyerjiCharacter::RegisterCorpseForCleanup()

{

  if (!HasAuthority() || bCorpseRegisteredForCleanup)

  {

    return;
  }

  for (int32 Index = CorpsesPendingCleanup.Num() - 1; Index >= 0; --Index)

  {

    if (!CorpsesPendingCleanup[Index].IsValid())

    {

      CorpsesPendingCleanup.RemoveAtSwap(Index);
    }
  }

  CorpsesPendingCleanup.Add(this);

  bCorpseRegisteredForCleanup = true;
}

void AAeyerjiCharacter::UnregisterCorpseFromCleanup()

{

  if (!HasAuthority())

  {

    return;
  }

  for (int32 Index = CorpsesPendingCleanup.Num() - 1; Index >= 0; --Index)

  {

    const TWeakObjectPtr<AAeyerjiCharacter> &Entry =
        CorpsesPendingCleanup[Index];

    if (!Entry.IsValid() || Entry.Get() == this)

    {

      CorpsesPendingCleanup.RemoveAtSwap(Index);
    }
  }

  bCorpseRegisteredForCleanup = false;
}

void AAeyerjiCharacter::RemoveInvalidCorpses()

{

  for (int32 Index = CorpsesPendingCleanup.Num() - 1; Index >= 0; --Index)

  {

    if (!CorpsesPendingCleanup[Index].IsValid())

    {

      CorpsesPendingCleanup.RemoveAtSwap(Index);
    }
  }
}

void AAeyerjiCharacter::GetPendingCorpseCleanup(
    TArray<AAeyerjiCharacter *> &OutCorpses)

{

  OutCorpses.Reset();

  for (int32 Index = CorpsesPendingCleanup.Num() - 1; Index >= 0; --Index)

  {

    if (AAeyerjiCharacter *Corpse = CorpsesPendingCleanup[Index].Get())

    {

      OutCorpses.Add(Corpse);

    }

    else

    {

      CorpsesPendingCleanup.RemoveAtSwap(Index);
    }
  }
}

void AAeyerjiCharacter::HandleOutOfHealth(AActor *Victim, AActor *Killer, float DamageTaken) {
  ensureMsgf(Victim == this || !Victim,
             TEXT("HandleOutOfHealth expected self victim but got %s"),
             *GetNameSafe(Victim));
  BP_OnDeath(Killer, DamageTaken);
  FAeyerjiDeathStateOptions DeathOptions;
  ApplyDeathState(DeathOptions);
  if (HasAuthority() && AbilitySystemAeyerji)
  {
    TSubclassOf<UGameplayAbility> DeathClass = DeathAbilityClass ? DeathAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Death::StaticClass());
    if (FGameplayAbilitySpec* DeathSpec = AbilitySystemAeyerji->FindAbilitySpecFromClass(DeathClass))
    {
      if (!DeathSpec->IsActive())
      {
        const bool bActivated = AbilitySystemAeyerji->TryActivateAbility(DeathSpec->Handle);
        if (!bActivated)
        {
          AJ_LOG(this, TEXT("HandleOutOfHealth: GA_Death spec found but activation failed."));
        }
      }
      else
      {
        AJ_LOG(this, TEXT("HandleOutOfHealth: GA_Death already active, skipping manual activation."));
      }
    }
    else
    {
      AJ_LOG(this, TEXT("HandleOutOfHealth: GA_Death spec missing on server."));
    }
  }
  if (HasAuthority()) {
    MulticastOnDeath(Killer, DamageTaken);
  }
}
void AAeyerjiCharacter::OnRep_Controller() {
  Super::OnRep_Controller();
  // InitialiseAbilitySystem();
}
void AAeyerjiCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  if (HasAuthority()) {
    UnregisterCorpseFromCleanup();
  }
  GetWorldTimerManager().ClearTimer(RagdollCollisionDisableHandle);
  Super::EndPlay(EndPlayReason);
}
void AAeyerjiCharacter::DetachDestroyAttachedActors() {
  TArray<AActor *> Attached;
  GetAttachedActors(Attached, /*bResetArray=*/true);
  for (AActor *Child : Attached) {
    if (!Child) {
      continue;
    }
    Child->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    if (Child->HasAuthority()) {
      Child->Destroy();
    } else {
      Child->SetLifeSpan(0.1f);
    }
  }
}

