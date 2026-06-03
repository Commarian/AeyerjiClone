#include "AeyerjiCharacter.h"
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
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "BrainComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

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
  AttributeSetAeyerji =
      CreateDefaultSubobject<UAeyerjiAttributeSet>(TEXT("AeyerjiAttributeSet"));
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

  if (USkeletalMeshComponent* MeshComponent = GetMesh())
  {
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeshComponent->SetGenerateOverlapEvents(false);
    MeshComponent->SetCanEverAffectNavigation(false);
  }
}

void AAeyerjiCharacter::OnConstruction(const FTransform& Transform)
{
  Super::OnConstruction(Transform);
  RefreshCollisionCapsuleSize();
  WarnOnScaledRootCapsule();
}

void AAeyerjiCharacter::BeginPlay() {
  Super::BeginPlay();
  RefreshCollisionCapsuleSize();
  WarnOnScaledRootCapsule();
}

void AAeyerjiCharacter::RefreshCollisionCapsuleSize()
{
  UCapsuleComponent* Capsule = GetCapsuleComponent();
  if (!Capsule)
  {
    return;
  }

  const float DesiredRadius =
      (CollisionCapsuleRadius > 0.f) ? CollisionCapsuleRadius : Capsule->GetUnscaledCapsuleRadius();
  const float DesiredHalfHeight =
      (CollisionCapsuleHalfHeight > 0.f) ? CollisionCapsuleHalfHeight : Capsule->GetUnscaledCapsuleHalfHeight();

  if (!FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), DesiredRadius)
      || !FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(), DesiredHalfHeight))
  {
    Capsule->SetCapsuleSize(DesiredRadius, DesiredHalfHeight, /*bUpdateOverlaps=*/true);
  }
}

void AAeyerjiCharacter::WarnOnScaledRootCapsule() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
  const UCapsuleComponent* Capsule = GetCapsuleComponent();
  if (!Capsule)
  {
    return;
  }

  const FVector CapsuleScale = Capsule->GetComponentScale();
  if (!CapsuleScale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("%s root capsule scale is %s. Prefer capsule size properties over component scale."),
        *GetNameSafe(this),
        *CapsuleScale.ToCompactString());
  }
#endif
}
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

void AAeyerjiCharacter::BindCrowdControlEvents()
{
  if (!AbilitySystemAeyerji)
  {
    return;
  }

  FOnGameplayEffectTagCountChanged& StunEvent =
      AbilitySystemAeyerji->RegisterGameplayTagEvent(
          AeyerjiTags::State_CrowdControl_Stunned,
          EGameplayTagEventType::NewOrRemoved);

  if (StunTagChangedHandle.IsValid())
  {
    StunEvent.Remove(StunTagChangedHandle);
    StunTagChangedHandle.Reset();
  }

  StunTagChangedHandle =
      StunEvent.AddUObject(this, &AAeyerjiCharacter::HandleStunTagChanged);

  HandleStunTagChanged(
      AeyerjiTags::State_CrowdControl_Stunned,
      AbilitySystemAeyerji->GetTagCount(AeyerjiTags::State_CrowdControl_Stunned));
}

bool AAeyerjiCharacter::IsStunned() const
{
  return AbilitySystemAeyerji
      && AbilitySystemAeyerji->HasMatchingGameplayTag(AeyerjiTags::State_CrowdControl_Stunned);
}

void AAeyerjiCharacter::HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
  if (CallbackTag != AeyerjiTags::State_CrowdControl_Stunned)
  {
    return;
  }

  if (NewCount > 0)
  {
    ApplyStunState();
  }
  else
  {
    ClearStunState();
  }
}

void AAeyerjiCharacter::ApplyStunState()
{
  if (bStunStateApplied || bHasAppliedDeathState)
  {
    return;
  }

  bStunStateApplied = true;

  AJ_LOG(this, TEXT("Stun applied: disabling movement/input and cancelling active abilities."));

  if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
  {
    if (HasAuthority())
    {
      ASC->CancelAbilities(nullptr, nullptr, nullptr);
    }
  }

  if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
  {
    PreStunMovementMode = MovementComponent->MovementMode;
    PreStunCustomMovementMode = MovementComponent->CustomMovementMode;
    bStunShouldRestoreMovement = MovementComponent->MovementMode != MOVE_None;

    MovementComponent->StopMovementImmediately();
    MovementComponent->DisableMovement();
  }

  if (AController* OwningController = GetController())
  {
    if (AAIController* AIController = Cast<AAIController>(OwningController))
    {
      AIController->StopMovement();

      if (UBrainComponent* Brain = AIController->GetBrainComponent())
      {
        Brain->PauseLogic(TEXT("Stunned"));
      }
    }
    else if (APlayerController* PlayerController = Cast<APlayerController>(OwningController))
    {
      PlayerController->StopMovement();
      PlayerController->SetIgnoreMoveInput(true);
      PlayerController->SetIgnoreLookInput(true);
      bStunIgnoredControllerInput = true;
    }
  }

  BP_OnStunStateChanged(true);
}

void AAeyerjiCharacter::ClearStunState()
{
  if (!bStunStateApplied)
  {
    return;
  }

  bStunStateApplied = false;

  if (!bHasAppliedDeathState)
  {
    if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
    {
      if (bStunShouldRestoreMovement)
      {
        MovementComponent->SetMovementMode(PreStunMovementMode, PreStunCustomMovementMode);
      }
    }

    if (AController* OwningController = GetController())
    {
      if (AAIController* AIController = Cast<AAIController>(OwningController))
      {
        if (UBrainComponent* Brain = AIController->GetBrainComponent())
        {
          Brain->ResumeLogic(TEXT("StunEnded"));
        }
      }
      else if (APlayerController* PlayerController = Cast<APlayerController>(OwningController))
      {
        if (bStunIgnoredControllerInput)
        {
          PlayerController->SetIgnoreMoveInput(false);
          PlayerController->SetIgnoreLookInput(false);
        }
      }
    }
  }

  bStunShouldRestoreMovement = false;
  bStunIgnoredControllerInput = false;
  AJ_LOG(this, TEXT("Stun cleared: restoring movement/input where appropriate."));
  BP_OnStunStateChanged(false);
}

void AAeyerjiCharacter::EnsurePrimaryAttributeSetRegistered() {
  if (!AbilitySystemAeyerji) {
    return;
  }

  UAeyerjiAttributeSet *PrimarySet =
      const_cast<UAeyerjiAttributeSet *>(AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>());
  if (!PrimarySet) {
    if (AttributeSetAeyerji) {
      AbilitySystemAeyerji->AddAttributeSetSubobject(AttributeSetAeyerji.Get());
      PrimarySet = AttributeSetAeyerji.Get();
    } else {
      const FName AttributeSetName = MakeUniqueObjectName(this, UAeyerjiAttributeSet::StaticClass(), TEXT("AeyerjiAttributeSet"));
      UAeyerjiAttributeSet *NewSet = NewObject<UAeyerjiAttributeSet>(this, UAeyerjiAttributeSet::StaticClass(), AttributeSetName);
      AttributeSetAeyerji = NewSet;
      AbilitySystemAeyerji->AddAttributeSetSubobject(NewSet);
      PrimarySet = NewSet;
    }
  }

  if (!PrimarySet) {
    return;
  }

  AttributeSetAeyerji = PrimarySet;

  TArray<UAttributeSet *> DuplicateSets;
  for (UAttributeSet *Set : AbilitySystemAeyerji->GetSpawnedAttributes()) {
    if (Set && Set->IsA(UAeyerjiAttributeSet::StaticClass()) && Set != PrimarySet) {
      DuplicateSets.Add(Set);
    }
  }

  for (UAttributeSet *Set : DuplicateSets) {
    AbilitySystemAeyerji->RemoveSpawnedAttribute(Set);
  }
}

void AAeyerjiCharacter::OnDeath_Implementation() {
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

  // Mirror the gameplay dead state onto actor tags so systems without ASC
  // access can still identify dead pawns immediately.
  const FGameplayTag DeadTag = AeyerjiTags::State_Dead;
  Tags.AddUnique(DeadTag.GetTagName());

  if (HasAuthority())
  {
    if (UAbilitySystemComponent *ASC = GetAbilitySystemComponent())
    {
      if (!ASC->HasMatchingGameplayTag(DeadTag))
      {
        ASC->AddLooseGameplayTag(DeadTag);
      }
    }

    if (UAIPerceptionStimuliSourceComponent *Stim =
            FindComponentByClass<UAIPerceptionStimuliSourceComponent>())
    {
      Stim->UnregisterFromPerceptionSystem();
    }

    if (Options.bRegisterCorpseForCleanup)
    {
      RegisterCorpseForCleanup();
    }
  }
  if (Options.bDetachAttachments)
  {
    DetachDestroyAttachedActors();
  }

  if (Options.bRemoveFloatingWidgets)
  {
    RemoveFloatingWidgets();
  }

  if (Options.bStopRegeneration)
  {
    StopRegeneration();
  }

  if (Options.bDisableControllerLogic)
  {
    ShutdownControllerLogic();
  }

  if (Options.bDisableMovement)
  {
    StopMovementAndInput();
  }

  if (Options.bDisableCollision)
  {
    DisableDeathCollision();
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

void AAeyerjiCharacter::StopMovementAndInput()

{
  if (UCharacterMovementComponent *MovementComponent = GetCharacterMovement())
  {
    MovementComponent->StopMovementImmediately();
    MovementComponent->DisableMovement();
  }

  if (APlayerController *PlayerController =
          Cast<APlayerController>(GetController()))
  {
    DisableInput(PlayerController);
  }
}

void AAeyerjiCharacter::ShutdownControllerLogic()

{
  if (AAIController *AIController = Cast<AAIController>(GetController()))
  {
    AIController->StopMovement();
    AIController->ClearFocus(EAIFocusPriority::Gameplay);

    if (UBrainComponent *Brain = AIController->GetBrainComponent())
    {
      Brain->StopLogic(TEXT("DeathState"));
    }

    if (UAIPerceptionComponent *Perception =
            AIController->GetPerceptionComponent())
    {
      Perception->SetComponentTickEnabled(false);
    }
  }
}

void AAeyerjiCharacter::DisableDeathCollision()

{
  if (UCapsuleComponent *Capsule = GetCapsuleComponent())
  {
    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
    Capsule->SetGenerateOverlapEvents(false);
  }

  if (USkeletalMeshComponent *MeshComponent = GetMesh())
  {
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
  OnDeath_Implementation();
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
  if (AbilitySystemAeyerji && StunTagChangedHandle.IsValid())
  {
    AbilitySystemAeyerji->RegisterGameplayTagEvent(
        AeyerjiTags::State_CrowdControl_Stunned,
        EGameplayTagEventType::NewOrRemoved).Remove(StunTagChangedHandle);
    StunTagChangedHandle.Reset();
  }

  if (HasAuthority()) {
    UnregisterCorpseFromCleanup();
  }
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

