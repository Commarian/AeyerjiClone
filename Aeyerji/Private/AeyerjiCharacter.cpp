#include "AeyerjiCharacter.h"
#include "Abilities/GA_Death.h" // your passive death GA
#include "Abilities/GA_PrimaryMeleeBasic.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiStatEngineComponent.h"
#include "CharacterStatsLibrary.h"
#include "Components/ActorComponent.h"
#include "Components/AeyerjiCombatCueProfileComponent.h"
#include "Components/AeyerjiPickupFXComponent.h"
#include "Components/AeyerjiNavSafetyComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Enemy/EnemyAIController.h"
#include "GUI/AeyerjiFloatingStatusBarComponent.h"
#include "GameFramework/PlayerState.h"
#include "GAS/GE_Stagger.h"
#include "GAS/GE_Stun.h"
#include "GameplayEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/AeyerjiLog.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "AeyerjiGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
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
  NavSafetyComponent =
      CreateDefaultSubobject<UAeyerjiNavSafetyComponent>(TEXT("NavSafetyComponent"));
  CombatCueProfileComponent =
      CreateDefaultSubobject<UAeyerjiCombatCueProfileComponent>(TEXT("CombatCueProfileComponent"));

  // Default death ability (can be overridden in BP)
  if (!DeathAbilityClass)
  {
    DeathAbilityClass = UGA_Death::StaticClass();
  }

  StunOverheadEffect = TSoftObjectPtr<UNiagaraSystem>(
      FSoftObjectPath(TEXT("/Game/Abilities/StunEffect/NiagaraStunEffect.NiagaraStunEffect")));

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
  CaptureBaseCollisionCapsuleSize();
  RefreshCollisionCapsuleSize();
  WarnOnScaledRootCapsule();
}

void AAeyerjiCharacter::BeginPlay() {
  Super::BeginPlay();
  CaptureBaseCollisionCapsuleSize();
  RefreshCollisionCapsuleSize();
  WarnOnScaledRootCapsule();
}

void AAeyerjiCharacter::SetArchetypeCollisionCapsuleSize(
    const float CapsuleRadius,
    const float CapsuleHalfHeight)
{
  CaptureBaseCollisionCapsuleSize();

  if (!FMath::IsFinite(CapsuleRadius) || !FMath::IsFinite(CapsuleHalfHeight)
      || CapsuleRadius <= KINDA_SMALL_NUMBER || CapsuleHalfHeight <= KINDA_SMALL_NUMBER)
  {
    ClearArchetypeCollisionCapsuleSize();
    return;
  }

  ArchetypeCollisionCapsuleRadius = FMath::Max(1.f, CapsuleRadius);
  ArchetypeCollisionCapsuleHalfHeight = FMath::Max(ArchetypeCollisionCapsuleRadius, CapsuleHalfHeight);
  RefreshCollisionCapsuleSize();
}

void AAeyerjiCharacter::ClearArchetypeCollisionCapsuleSize()
{
  ArchetypeCollisionCapsuleRadius = 0.f;
  ArchetypeCollisionCapsuleHalfHeight = 0.f;
  RefreshCollisionCapsuleSize();
}

void AAeyerjiCharacter::CaptureBaseCollisionCapsuleSize()
{
  if (bHasCapturedBaseCollisionCapsuleSize)
  {
    return;
  }

  const UCapsuleComponent* Capsule = GetCapsuleComponent();
  if (!Capsule)
  {
    return;
  }

  const float Radius = Capsule->GetUnscaledCapsuleRadius();
  const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
  if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight)
      || Radius <= KINDA_SMALL_NUMBER || HalfHeight <= KINDA_SMALL_NUMBER)
  {
    return;
  }

  BaseCollisionCapsuleRadius = Radius;
  BaseCollisionCapsuleHalfHeight = HalfHeight;
  bHasCapturedBaseCollisionCapsuleSize = true;
}

void AAeyerjiCharacter::RefreshCollisionCapsuleSize()
{
  UCapsuleComponent* Capsule = GetCapsuleComponent();
  if (!Capsule)
  {
    return;
  }

  const float FallbackRadius = bHasCapturedBaseCollisionCapsuleSize
      ? BaseCollisionCapsuleRadius
      : Capsule->GetUnscaledCapsuleRadius();
  const float FallbackHalfHeight = bHasCapturedBaseCollisionCapsuleSize
      ? BaseCollisionCapsuleHalfHeight
      : Capsule->GetUnscaledCapsuleHalfHeight();
  const float DesiredRadius = (ArchetypeCollisionCapsuleRadius > 0.f)
      ? ArchetypeCollisionCapsuleRadius
      : ((CollisionCapsuleRadius > 0.f) ? CollisionCapsuleRadius : FallbackRadius);
  const float DesiredHalfHeight = (ArchetypeCollisionCapsuleHalfHeight > 0.f)
      ? ArchetypeCollisionCapsuleHalfHeight
      : ((CollisionCapsuleHalfHeight > 0.f) ? CollisionCapsuleHalfHeight : FallbackHalfHeight);

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

void AAeyerjiCharacter::Multicast_PlayAbilityCosmetics_Implementation(
    UAnimMontage* Montage,
    float MontagePlayRate,
    UNiagaraSystem* NiagaraSystem,
    FVector NiagaraWorldLocation,
    FRotator NiagaraWorldRotation,
    FVector NiagaraScale,
    bool bAttachNiagaraToMesh,
    FName NiagaraAttachSocket,
    FVector NiagaraLocalOffset)
{
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() == NM_DedicatedServer)
  {
    return;
  }

  USkeletalMeshComponent* MeshComponent = GetMesh();
  if (Montage && MeshComponent)
  {
    if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
    {
      AnimInstance->Montage_Play(Montage, FMath::Max(0.01f, MontagePlayRate));
    }
  }

  if (!NiagaraSystem)
  {
    return;
  }

  if (bAttachNiagaraToMesh && MeshComponent)
  {
    UNiagaraFunctionLibrary::SpawnSystemAttached(
        NiagaraSystem,
        MeshComponent,
        NiagaraAttachSocket,
        NiagaraLocalOffset,
        FRotator::ZeroRotator,
        NiagaraScale,
        EAttachLocation::KeepRelativeOffset,
        true,
        ENCPoolMethod::None,
        true);
    return;
  }

  UNiagaraFunctionLibrary::SpawnSystemAtLocation(
      World,
      NiagaraSystem,
      NiagaraWorldLocation,
      NiagaraWorldRotation,
      NiagaraScale,
      true);
}

void AAeyerjiCharacter::Multicast_PlayAbilityMontageByPath_Implementation(FSoftObjectPath MontagePath, float MontagePlayRate)
{
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() == NM_DedicatedServer)
  {
    return;
  }

  if (!MontagePath.IsValid())
  {
    return;
  }

  UAnimMontage* Montage = Cast<UAnimMontage>(MontagePath.TryLoad());
  if (!Montage)
  {
    const FString Message = FString::Printf(
        TEXT("Ability montage failed to load for %s: %s"),
        *GetNameSafe(this),
        *MontagePath.ToString());
    AJ_LOG(this, TEXT("%s"), *Message);
    if (GEngine)
    {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
    }
    return;
  }

  USkeletalMeshComponent* MeshComponent = GetMesh();
  UAnimInstance* AnimInstance = MeshComponent ? MeshComponent->GetAnimInstance() : nullptr;
  if (!AnimInstance)
  {
    const FString Message = FString::Printf(
        TEXT("Ability montage cannot play on %s: no AnimInstance."),
        *GetNameSafe(this));
    AJ_LOG(this, TEXT("%s"), *Message);
    if (GEngine)
    {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
    }
    return;
  }

  const float PlayedDuration = AnimInstance->Montage_Play(Montage, FMath::Max(0.01f, MontagePlayRate));
  if (PlayedDuration <= 0.f)
  {
    const FString Message = FString::Printf(
        TEXT("Ability montage did not start on %s: %s. Check skeleton and AnimBP slot."),
        *GetNameSafe(this),
        *GetNameSafe(Montage));
    AJ_LOG(this, TEXT("%s"), *Message);
    if (GEngine)
    {
      GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, Message);
    }
    return;
  }

  AJ_LOG(this, TEXT("Ability montage started: Montage=%s Duration=%.2f AnimInstance=%s"),
      *GetNameSafe(Montage),
      PlayedDuration,
      *GetNameSafe(AnimInstance));
}

/* --------------------------- Startup GAS wiring --------------------------- */
void AAeyerjiCharacter::AddStartupAbilities() {
  AJ_LOG(this, TEXT("AddStartupAbilities - HasAuthority: %d"), HasAuthority());
  if (!HasAuthority() || AbilitySystemAeyerji == nullptr)
    return;
  for (TSubclassOf<UGameplayAbility> AbilityClass : DefaultAbilities) {
    if (*AbilityClass && !AbilitySystemAeyerji->FindAbilitySpecFromClass(AbilityClass)) {
      FGameplayAbilitySpec Spec(AbilityClass, CharacterLevel);
      AbilitySystemAeyerji->GiveAbility(Spec);
    }
  }
  // Passive Death GA so every pawn can die/respawn
  TSubclassOf<UGameplayAbility> DeathClass = DeathAbilityClass ? DeathAbilityClass : TSubclassOf<UGameplayAbility>(UGA_Death::StaticClass());
  if (DeathClass && !AbilitySystemAeyerji->FindAbilitySpecFromClass(DeathClass))
  {
    AbilitySystemAeyerji->GiveAbility(FGameplayAbilitySpec(DeathClass, 1));
  }
}
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
  FOnGameplayEffectTagCountChanged& StaggerEvent =
      AbilitySystemAeyerji->RegisterGameplayTagEvent(
          AeyerjiTags::State_CrowdControl_Staggered,
          EGameplayTagEventType::NewOrRemoved);

  if (StunTagChangedHandle.IsValid())
  {
    StunEvent.Remove(StunTagChangedHandle);
    StunTagChangedHandle.Reset();
  }
  if (StaggerTagChangedHandle.IsValid())
  {
    StaggerEvent.Remove(StaggerTagChangedHandle);
    StaggerTagChangedHandle.Reset();
  }

  StunTagChangedHandle =
      StunEvent.AddUObject(this, &AAeyerjiCharacter::HandleStunTagChanged);
  StaggerTagChangedHandle =
      StaggerEvent.AddUObject(this, &AAeyerjiCharacter::HandleStaggerTagChanged);

  HandleStunTagChanged(
      AeyerjiTags::State_CrowdControl_Stunned,
      AbilitySystemAeyerji->GetTagCount(AeyerjiTags::State_CrowdControl_Stunned));
  HandleStaggerTagChanged(
      AeyerjiTags::State_CrowdControl_Staggered,
      AbilitySystemAeyerji->GetTagCount(AeyerjiTags::State_CrowdControl_Staggered));
}

void AAeyerjiCharacter::BindRuntimeAttributeHooks()
{
  UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
  if (!ASC)
  {
    return;
  }

  if (RunSpeedChangedHandle.IsValid())
  {
    if (UAbilitySystemComponent* PreviousASC = RuntimeAttributeHookASC.Get())
    {
      PreviousASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetRunSpeedAttribute()).Remove(RunSpeedChangedHandle);
    }
    RunSpeedChangedHandle.Reset();
  }

  RunSpeedChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetRunSpeedAttribute())
      .AddUObject(this, &AAeyerjiCharacter::HandleRunSpeedChanged);
  RuntimeAttributeHookASC = ASC;

  ApplyRunSpeedFromAttribute();
}

void AAeyerjiCharacter::UnbindRuntimeAttributeHooks()
{
  if (RunSpeedChangedHandle.IsValid())
  {
    if (UAbilitySystemComponent* ASC = RuntimeAttributeHookASC.Get())
    {
      ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetRunSpeedAttribute()).Remove(RunSpeedChangedHandle);
    }
    RunSpeedChangedHandle.Reset();
  }

  RuntimeAttributeHookASC.Reset();
}

void AAeyerjiCharacter::HandleRunSpeedChanged(const FOnAttributeChangeData& /*Data*/)
{
  ApplyRunSpeedFromAttribute();
}

void AAeyerjiCharacter::ApplyRunSpeedFromAttribute()
{
  UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
  UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
  if (!ASC || !MovementComponent)
  {
    return;
  }

  const float AttributeRunSpeed = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetRunSpeedAttribute());
  if (AttributeRunSpeed <= KINDA_SMALL_NUMBER)
  {
    return;
  }

  constexpr float MinimumPlayableRunSpeed = 50.f;
  const float NewMaxWalkSpeed = FMath::Max(MinimumPlayableRunSpeed, AttributeRunSpeed);
  if (!FMath::IsNearlyEqual(MovementComponent->MaxWalkSpeed, NewMaxWalkSpeed))
  {
    MovementComponent->MaxWalkSpeed = NewMaxWalkSpeed;
    AJ_LOG(this, TEXT("[RunSpeedHook] MaxWalkSpeed set to %.1f from RunSpeed %.1f"), NewMaxWalkSpeed, AttributeRunSpeed);
  }
}

bool AAeyerjiCharacter::IsStunned() const
{
  return AbilitySystemAeyerji
      && AbilitySystemAeyerji->HasMatchingGameplayTag(AeyerjiTags::State_CrowdControl_Stunned);
}

bool AAeyerjiCharacter::IsStaggered() const
{
  return AbilitySystemAeyerji
      && AbilitySystemAeyerji->HasMatchingGameplayTag(AeyerjiTags::State_CrowdControl_Staggered);
}

bool AAeyerjiCharacter::IsCrowdControlled() const
{
  return IsStunned() || IsStaggered();
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

void AAeyerjiCharacter::HandleStaggerTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
  if (CallbackTag != AeyerjiTags::State_CrowdControl_Staggered)
  {
    return;
  }

  if (NewCount > 0)
  {
    ApplyStaggerState();
  }
  else
  {
    ClearStaggerState();
  }
}

void AAeyerjiCharacter::CancelActiveAbilitiesForCrowdControl()
{
  if (!HasAuthority())
  {
    return;
  }

  if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
  {
    ASC->CancelAbilities(nullptr, nullptr, nullptr);
  }
}

void AAeyerjiCharacter::StopAIForCrowdControl()
{
  if (AAIController* AIController = Cast<AAIController>(GetController()))
  {
    AIController->StopMovement();
    AIController->ClearFocus(EAIFocusPriority::Gameplay);
    AIController->ClearFocus(EAIFocusPriority::Move);
    AIController->SetControlRotation(GetActorRotation());
  }
}

void AAeyerjiCharacter::SendAICrowdControlStateTreeEvent(const FGameplayTag& EventTag)
{
  if (!HasAuthority() || bHasAppliedDeathState || !EventTag.IsValid())
  {
    return;
  }

  if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
  {
    EnemyController->SendAICrowdControlEvent(EventTag);
  }
}

void AAeyerjiCharacter::SendCurrentCrowdControlStateTreeEvent()
{
  if (IsStunned())
  {
    SendAICrowdControlStateTreeEvent(AeyerjiTags::Event_AI_CrowdControl_Stunned);
  }
  else if (IsStaggered())
  {
    SendAICrowdControlStateTreeEvent(AeyerjiTags::Event_AI_CrowdControl_Staggered);
  }
  else
  {
    SendAICrowdControlStateTreeEvent(AeyerjiTags::Event_AI_CrowdControl_Cleared);
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

  bPreStunUseControllerRotationYaw = bUseControllerRotationYaw;
  bUseControllerRotationYaw = false;

  CancelActiveAbilitiesForCrowdControl();

  if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
  {
    PreStunMovementMode = MovementComponent->MovementMode;
    PreStunCustomMovementMode = MovementComponent->CustomMovementMode;
    bStunShouldRestoreMovement = MovementComponent->MovementMode != MOVE_None;
    bPreStunOrientRotationToMovement = MovementComponent->bOrientRotationToMovement;
    bPreStunUseControllerDesiredRotation = MovementComponent->bUseControllerDesiredRotation;

    MovementComponent->StopMovementImmediately();
    MovementComponent->bOrientRotationToMovement = false;
    MovementComponent->bUseControllerDesiredRotation = false;
    MovementComponent->DisableMovement();
  }

  if (AController* OwningController = GetController())
  {
    if (Cast<AAIController>(OwningController))
    {
      StopAIForCrowdControl();
    }
    else if (APlayerController* PlayerController = Cast<APlayerController>(OwningController))
    {
      PlayerController->StopMovement();
      PlayerController->SetIgnoreMoveInput(true);
      PlayerController->SetIgnoreLookInput(true);
      bStunIgnoredControllerInput = true;
    }
  }

  SpawnStunOverheadEffect();
  SendAICrowdControlStateTreeEvent(AeyerjiTags::Event_AI_CrowdControl_Stunned);
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
    bUseControllerRotationYaw = bPreStunUseControllerRotationYaw;

    if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
    {
      MovementComponent->bOrientRotationToMovement = bPreStunOrientRotationToMovement;
      MovementComponent->bUseControllerDesiredRotation = bPreStunUseControllerDesiredRotation;

      if (bStunShouldRestoreMovement)
      {
        MovementComponent->SetMovementMode(PreStunMovementMode, PreStunCustomMovementMode);
      }
    }

    if (AController* OwningController = GetController())
    {
      if (AAIController* AIController = Cast<AAIController>(OwningController))
      {
        AIController->SetControlRotation(GetActorRotation());
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
  DestroyStunOverheadEffect();
  AJ_LOG(this, TEXT("Stun cleared: restoring movement/input where appropriate."));
  SendCurrentCrowdControlStateTreeEvent();
  BP_OnStunStateChanged(false);
}

void AAeyerjiCharacter::ApplyStaggerState()
{
  if (bStaggerStateApplied || bHasAppliedDeathState)
  {
    return;
  }

  bStaggerStateApplied = true;

  AJ_LOG(this, TEXT("Stagger applied: interrupting actions and notifying StateTree."));
  CancelActiveAbilitiesForCrowdControl();

  if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
  {
    MovementComponent->StopMovementImmediately();
  }

  StopAIForCrowdControl();
  SendAICrowdControlStateTreeEvent(AeyerjiTags::Event_AI_CrowdControl_Staggered);
}

void AAeyerjiCharacter::ClearStaggerState()
{
  if (!bStaggerStateApplied)
  {
    return;
  }

  bStaggerStateApplied = false;
  AJ_LOG(this, TEXT("Stagger cleared: notifying StateTree to reselect behavior."));
  SendCurrentCrowdControlStateTreeEvent();
}

void AAeyerjiCharacter::ClearCrowdControlStateForDeath()
{
  const bool bHadStunState = bStunStateApplied;
  const bool bHadStunVisual = ActiveStunOverheadEffect != nullptr;

  // Death bypasses ClearStunState, but player-controller input ignores are stacked.
  // Release the exact pair owned by this stun before discarding the tracking flag so
  // a respawned player cannot inherit blocked movement or camera/look input.
  if (bStunIgnoredControllerInput)
  {
    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
      PlayerController->SetIgnoreMoveInput(false);
      PlayerController->SetIgnoreLookInput(false);
    }
  }

  bStunStateApplied = false;
  bStaggerStateApplied = false;
  bStunShouldRestoreMovement = false;
  bStunIgnoredControllerInput = false;
  DestroyStunOverheadEffect();

  if (bHadStunState || bHadStunVisual)
  {
    BP_OnStunStateChanged(false);
  }

  if (HasAuthority())
  {
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
    {
      FGameplayTagContainer CrowdControlTags;
      CrowdControlTags.AddTag(AeyerjiTags::State_CrowdControl_Stunned);
      CrowdControlTags.AddTag(AeyerjiTags::State_CrowdControl_Staggered);
      ASC->RemoveActiveEffectsWithGrantedTags(CrowdControlTags);
      ASC->SetLooseGameplayTagCount(AeyerjiTags::State_CrowdControl_Stunned, 0);
      ASC->SetLooseGameplayTagCount(AeyerjiTags::State_CrowdControl_Staggered, 0);
      ASC->RemoveGameplayCue(AeyerjiTags::GameplayCue_Combat_Hit_Staggered);
    }
  }
}

void AAeyerjiCharacter::SpawnStunOverheadEffect()
{
  UWorld* World = GetWorld();
  if (!World || World->GetNetMode() == NM_DedicatedServer || ActiveStunOverheadEffect)
  {
    return;
  }

  UNiagaraSystem* StunSystem = StunOverheadEffect.Get();
  if (!StunSystem && !StunOverheadEffect.IsNull())
  {
    StunSystem = StunOverheadEffect.LoadSynchronous();
  }

  if (!StunSystem)
  {
    if (!bWarnedStunOverheadEffectLoadFailure)
    {
      bWarnedStunOverheadEffectLoadFailure = true;
      const FString Message = FString::Printf(
          TEXT("Stun FX failed to load for %s: %s"),
          *GetNameSafe(this),
          *StunOverheadEffect.ToSoftObjectPath().ToString());
      AJ_LOG(this, TEXT("%s"), *Message);

      if (GEngine)
      {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, Message);
      }
    }
    return;
  }

  USceneComponent* AttachComponent = GetRootComponent();
  if (!AttachComponent)
  {
    return;
  }

  FVector SampleWorldLocation = GetActorLocation();

  if (USkeletalMeshComponent* MeshComponent = GetMesh())
  {
    if (!StunOverheadSocketName.IsNone() && MeshComponent->DoesSocketExist(StunOverheadSocketName))
    {
      SampleWorldLocation = MeshComponent->GetSocketLocation(StunOverheadSocketName);
    }
    else if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
      SampleWorldLocation.Z = GetActorLocation().Z + Capsule->GetScaledCapsuleHalfHeight();
    }
  }

  const FVector DesiredWorldLocation(
      SampleWorldLocation.X + StunOverheadOffset.X,
      SampleWorldLocation.Y + StunOverheadOffset.Y,
      SampleWorldLocation.Z + StunOverheadOffset.Z);

  const FVector AttachOffset = AttachComponent->GetComponentTransform().InverseTransformPositionNoScale(DesiredWorldLocation);

  ActiveStunOverheadEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
      StunSystem,
      AttachComponent,
      NAME_None,
      AttachOffset,
      FRotator::ZeroRotator,
      EAttachLocation::KeepRelativeOffset,
      true,
      true,
      ENCPoolMethod::None,
      true);

  if (ActiveStunOverheadEffect)
  {
    ActiveStunOverheadEffect->SetUsingAbsoluteRotation(true);
    ActiveStunOverheadEffect->SetWorldRotation(FRotator::ZeroRotator);
  }
}

void AAeyerjiCharacter::DestroyStunOverheadEffect()
{
  if (!ActiveStunOverheadEffect)
  {
    return;
  }

  ActiveStunOverheadEffect->DeactivateImmediate();
  ActiveStunOverheadEffect->DestroyComponent();
  ActiveStunOverheadEffect = nullptr;
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

void AAeyerjiCharacter::OnDeath_Implementation(AActor* Killer, float DamageTaken) {
  static_cast<void>(Killer);
  static_cast<void>(DamageTaken);
}

bool AAeyerjiCharacter::PrepareDeathPresentation(
    AActor* Killer,
    FRotator& OutFacingRotation)
{
  static_cast<void>(Killer);
  OutFacingRotation = GetActorRotation();
  return false;
}

/* ----------------------------- Death plumbing ----------------------------- */
FAeyerjiDeathStateOptions AAeyerjiCharacter::BuildDeathStateOptionsForOutOfHealth() const
{
  return FAeyerjiDeathStateOptions();
}

void AAeyerjiCharacter::ApplyDeathState(FAeyerjiDeathStateOptions Options) {
  const bool bWasAlreadyDead = bHasAppliedDeathState;
  ApplyDeathStateInternal(Options);
  if (HasAuthority() && !bWasAlreadyDead) {
    MulticastApplyDeathState(Options);
  }
}

void AAeyerjiCharacter::ResetDeathStateForReuse()
{
  ResetDeathStateForReuseInternal();

  if (HasAuthority())
  {
    MulticastResetDeathStateForReuse();
    ForceNetUpdate();
  }
}

void AAeyerjiCharacter::CancelActiveAbilitiesForDeath()
{
  if (!HasAuthority())
  {
    return;
  }

  UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
  if (!ASC)
  {
    return;
  }

  FGameplayTagContainer DeathAbilityTags;
  DeathAbilityTags.AddTag(AeyerjiTags::Ability_Death);
  ASC->CancelAbilities(nullptr, &DeathAbilityTags, nullptr);
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
  ClearCrowdControlStateForDeath();

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

      CancelActiveAbilitiesForDeath();
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

  // Presentation visibility is independent of component lifetime. Pooled enemies retain
  // their component for reuse, but a dead actor must never retain a zero-health bar.
  SetFloatingWidgetsPresentationVisible(false);

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

void AAeyerjiCharacter::MulticastResetDeathStateForReuse_Implementation()
{
  if (HasAuthority())
  {
    return;
  }

  ResetDeathStateForReuseInternal();
}

void AAeyerjiCharacter::MulticastOnDeath_Implementation(
    AActor* Killer,
    float DamageTaken,
    bool bApplyFacingRotation,
    FRotator FacingRotation)

{

  if (HasAuthority())

  {

    return;
  }

  // Movement replication is not ordered against this RPC. Apply the server-resolved
  // yaw here before Blueprint spawns any detached death geometry from our transform.
  if (bApplyFacingRotation)
  {
    SetActorRotation(FacingRotation, ETeleportType::None);
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

void AAeyerjiCharacter::SetFloatingWidgetsPresentationVisible(const bool bVisible)
{
  TInlineComponentArray<UAeyerjiFloatingStatusBarComponent *>
      FloatingStatusComponents(this);

  for (UAeyerjiFloatingStatusBarComponent *Component : FloatingStatusComponents)
  {
    if (Component)
    {
      Component->SetStatusBarPresentationVisible(bVisible);
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

void AAeyerjiCharacter::EnableLivingCollision()
{
  SetActorEnableCollision(true);

  if (UCapsuleComponent *Capsule = GetCapsuleComponent())
  {
    Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
    Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Capsule->SetGenerateOverlapEvents(true);
  }

  if (USkeletalMeshComponent *MeshComponent = GetMesh())
  {
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeshComponent->SetGenerateOverlapEvents(false);
    MeshComponent->SetVisibility(true, true);
    MeshComponent->SetComponentTickEnabled(true);
  }

  RefreshCollisionCapsuleSize();
}

void AAeyerjiCharacter::RestartControllerLogicForReuse()
{
  if (UCharacterMovementComponent *MovementComponent = GetCharacterMovement())
  {
    // Pool/death Blueprint hooks may deactivate the component or detach its updated primitive.
    // A living checkout must restore the complete CharacterMovement contract, not only MOVE_Walking.
    MovementComponent->Activate(true);
    if (UCapsuleComponent *Capsule = GetCapsuleComponent();
        Capsule && MovementComponent->UpdatedComponent.Get() != Capsule)
    {
      MovementComponent->SetUpdatedComponent(Capsule);
    }
    MovementComponent->SetComponentTickEnabled(true);
    MovementComponent->SetMovementMode(MOVE_Walking);
    MovementComponent->StopMovementImmediately();

    if (UAeyerjiCharacterMovementComponent *AeyerjiMovement =
            Cast<UAeyerjiCharacterMovementComponent>(MovementComponent))
    {
      // Transient gameplay effects are cleared immediately before reuse. Refresh the cached
      // root tag now so a prior checkout cannot retain a stale movement-blocked cache entry.
      AeyerjiMovement->ForceRootedStateRefresh();
    }
  }

  if (AAIController *AIController = Cast<AAIController>(GetController()))
  {
    AIController->StopMovement();
    AIController->ClearFocus(EAIFocusPriority::Gameplay);

    if (UAIPerceptionComponent *Perception = AIController->GetPerceptionComponent())
    {
      Perception->SetComponentTickEnabled(true);
    }

    if (UBrainComponent *Brain = AIController->GetBrainComponent())
    {
      Brain->RestartLogic();
    }
  }
  else if (APlayerController *PlayerController = Cast<APlayerController>(GetController()))
  {
    EnableInput(PlayerController);
  }
}

void AAeyerjiCharacter::ResetDeathStateForReuseInternal()
{
  if (HasAuthority())
  {
    UnregisterCorpseFromCleanup();
    // Pooled actors can be checked out before their normal possession/init path has registered
    // the default attribute set. Register it before writing numeric bases to avoid a GAS ensure.
    EnsurePrimaryAttributeSetRegistered();
  }

  bHasAppliedDeathState = false;
  bCorpseRegisteredForCleanup = false;
  SetCanBeDamaged(true);
  const FGameplayTag DeadTag = AeyerjiTags::State_Dead;
  Tags.Remove(DeadTag.GetTagName());
  SetActorHiddenInGame(false);
  SetActorTickEnabled(true);

  if (UAbilitySystemComponent *ASC = GetAbilitySystemComponent();
      ASC && ASC->GetSet<UAeyerjiAttributeSet>())
  {
    ASC->SetLooseGameplayTagCount(AeyerjiTags::State_Dead, 0);
    ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(),
                                 ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()));
    ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetManaAttribute(),
                                 ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute()));
    ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetPoiseAttribute(),
                                 ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetPoiseMaxAttribute()));
  }

  if (AttributeSetAeyerji)
  {
    AttributeSetAeyerji->ResetDeathStateForReuse();
  }

  // Restore retained pooled status bars only after HP and other live attributes have reset,
  // preventing a one-frame zero-health presentation during checkout.
  SetFloatingWidgetsPresentationVisible(true);

  ClearStunState();
  ClearStaggerState();
  EnableLivingCollision();
  RestartControllerLogicForReuse();

  if (HasAuthority())
  {
    if (UAIPerceptionStimuliSourceComponent *Stim =
            FindComponentByClass<UAIPerceptionStimuliSourceComponent>())
    {
      Stim->RegisterWithPerceptionSystem();
    }
  }

  ApplyRunSpeedFromAttribute();
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
  FRotator DeathFacingRotation = GetActorRotation();
  const bool bApplyDeathFacingRotation =
      PrepareDeathPresentation(Killer, DeathFacingRotation);
  BP_OnDeath(Killer, DamageTaken);
  OnDeath_Implementation(Killer, DamageTaken);
  const FAeyerjiDeathStateOptions DeathOptions = BuildDeathStateOptionsForOutOfHealth();
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
    MulticastOnDeath(
        Killer,
        DamageTaken,
        bApplyDeathFacingRotation,
        DeathFacingRotation);
  }
}
void AAeyerjiCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  DestroyStunOverheadEffect();
  UnbindRuntimeAttributeHooks();

  if (AbilitySystemAeyerji && StunTagChangedHandle.IsValid())
  {
    AbilitySystemAeyerji->RegisterGameplayTagEvent(
        AeyerjiTags::State_CrowdControl_Stunned,
        EGameplayTagEventType::NewOrRemoved).Remove(StunTagChangedHandle);
    StunTagChangedHandle.Reset();
  }
  if (AbilitySystemAeyerji && StaggerTagChangedHandle.IsValid())
  {
    AbilitySystemAeyerji->RegisterGameplayTagEvent(
        AeyerjiTags::State_CrowdControl_Staggered,
        EGameplayTagEventType::NewOrRemoved).Remove(StaggerTagChangedHandle);
    StaggerTagChangedHandle.Reset();
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

