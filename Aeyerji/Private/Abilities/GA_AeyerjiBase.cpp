#include "Abilities/GA_AeyerjiBase.h"

#include "AbilitySystemComponent.h"

#include "AeyerjiCharacter.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiGameplayTags.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"

#include "Engine/World.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"
#include "GAS/GE_AeyerjiAbilityCostMana.h"
#include "MouseNavBlueprintLibrary.h"

#include "GameplayTagContainer.h"

UGA_AeyerjiBase::UGA_AeyerjiBase()

{

  // All abilities are blocked while Dead.

  ActivationBlockedTags.AddTag(AeyerjiTags::State_Dead);
  ActivationBlockedTags.AddTag(AeyerjiTags::State_CrowdControl_Stunned);

  // Sensible defaults for ARPG skills; tweak as needed.

  InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
  ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
  NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
  NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
  bServerRespectsRemoteAbilityCancellation = true;
}

AAeyerjiCharacter *UGA_AeyerjiBase::GetAeyerjiCharacter(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  return ActorInfo ? Cast<AAeyerjiCharacter>(ActorInfo->AvatarActor.Get())
                   : nullptr;
}

UAbilitySystemComponent *UGA_AeyerjiBase::GetAeyerjiAbilitySystem(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  return ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
}

AAeyerjiCharacter *UGA_AeyerjiBase::BP_GetAeyerjiCharacter() const

{

  return GetAeyerjiCharacter(GetCurrentActorInfo());
}

UAbilitySystemComponent *UGA_AeyerjiBase::BP_GetAeyerjiAbilitySystem() const

{

  return GetAeyerjiAbilitySystem(GetCurrentActorInfo());
}

bool UGA_AeyerjiBase::TryCommitAbilityInternal(
    const FGameplayAbilitySpecHandle &Handle,

    const FGameplayAbilityActorInfo *ActorInfo,

    const FGameplayAbilityActivationInfo &ActivationInfo,

    bool bEndAbilityOnFailure)

{

  if (!Handle.IsValid() || !ActorInfo)

  {

    return false;
  }

  if (CommitAbility(Handle, ActorInfo, ActivationInfo))

  {

    return true;
  }

  if (bEndAbilityOnFailure)

  {

    EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/ true,
               /*bWasCancelled*/ true);
  }

  return false;
}

void UGA_AeyerjiBase::EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent*, float& OutManaCost, float& OutCooldown) const
{
  if (const FAeyerjiAbilityTableRow* Row = GetAbilityTuningRow())
  {
    OutManaCost = FMath::Max(0.f, Row->Cost.ManaCost);
    OutCooldown = FMath::Max(0.f, Row->Cost.Cooldown);
    return;
  }

  OutManaCost = 0.f;
  OutCooldown = 0.f;
}

FGameplayTag UGA_AeyerjiBase::ResolveAbilityTag() const
{
  if (AbilityTag.IsValid())
  {
    return AbilityTag;
  }

  FGameplayTag BestTag;
  int32 BestDepth = -1;
  FGameplayTagContainer Tags = GetAssetTags();
  for (const FGameplayTag& Tag : Tags)
  {
    const FString TagString = Tag.ToString();
    if (!TagString.StartsWith(TEXT("Ability.")))
    {
      continue;
    }

    int32 Depth = 1;
    for (const TCHAR Character : TagString)
    {
      if (Character == TEXT('.'))
      {
        ++Depth;
      }
    }

    if (Depth > BestDepth)
    {
      BestDepth = Depth;
      BestTag = Tag;
    }
  }

  return BestTag;
}

const FAeyerjiAbilityTableRow* UGA_AeyerjiBase::GetAbilityTuningRow(const UAbilitySystemComponent* ASC) const
{
  const FGameplayTag RowTag = ResolveAbilityTag();
  if (!RowTag.IsValid())
  {
    return nullptr;
  }

  if (ASC)
  {
    if (UWorld* World = ASC->GetWorld())
    {
      if (UGameInstance* GI = World->GetGameInstance())
      {
        if (const UAeyerjiAbilityTuningSubsystem* Tuning = GI->GetSubsystem<UAeyerjiAbilityTuningSubsystem>())
        {
          if (const FAeyerjiAbilityTableRow* Row = Tuning->FindAbilityRow(RowTag))
          {
            return Row;
          }
        }
      }
    }
  }

  return UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(
    UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable(),
    RowTag);
}

void UGA_AeyerjiBase::ApplyAbilitySetByCallerToSpec(FGameplayEffectSpecHandle& SpecHandle, float InManaCost, float InCooldown) const
{
  if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

  if (AeyerjiTags::SBC_Cost_Mana.GetTag().IsValid())
  {
    Spec->SetSetByCallerMagnitude(AeyerjiTags::SBC_Cost_Mana, -FMath::Abs(InManaCost));
  }

  if (AeyerjiTags::SBC_CooldownSeconds.GetTag().IsValid())
  {
    Spec->SetSetByCallerMagnitude(AeyerjiTags::SBC_CooldownSeconds, FMath::Max(0.f, InCooldown));
  }
}

void UGA_AeyerjiBase::ApplyDamageTypeTagToSpec(FGameplayEffectSpecHandle& SpecHandle, const FGameplayTag& DamageTypeTag) const
{
  if (!DamageTypeTag.IsValid() || !SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
}

bool UGA_AeyerjiBase::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return false;
  }

  UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();

  float EvaluatedManaCost = 0.f;
  float EvaluatedCooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ASC, EvaluatedManaCost, EvaluatedCooldownSeconds);

  // Simple mana gate to avoid SetByCaller warnings from GE-based checks.
  if (EvaluatedManaCost > 0.f)
  {
    const FGameplayAttribute ManaAttr = UAeyerjiAttributeSet::GetManaAttribute();
    if (ASC->HasAttributeSetForAttribute(ManaAttr))
    {
      const float CurrentMana = ASC->GetNumericAttribute(ManaAttr);
      if (CurrentMana + KINDA_SMALL_NUMBER < EvaluatedManaCost)
      {
        return false;
      }
    }
  }

  // If a cost GE exists, skip Super::CheckCost to avoid evaluating it without SetByCaller.
  if (GetCostGameplayEffect() != nullptr)
  {
    return true;
  }

  return Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
}

bool UGA_AeyerjiBase::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
  const FGameplayTagContainer* CooldownTags = GetCooldownTags();
  if (CooldownTags && !CooldownTags->IsEmpty() && ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
  {
    const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
    const TArray<float> RemainingTimes = ASC->GetActiveEffectsTimeRemaining(Query);
    for (const float RemainingTime : RemainingTimes)
    {
      if (RemainingTime > KINDA_SMALL_NUMBER)
      {
        if (OptionalRelevantTags)
        {
          OptionalRelevantTags->AppendTags(*CooldownTags);
        }
        return false;
      }
    }
  }

  return Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
}

void UGA_AeyerjiBase::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return;
  }

  float EvaluatedManaCost = 0.f;
  float EvaluatedCooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), EvaluatedManaCost, EvaluatedCooldownSeconds);

  TSubclassOf<UGameplayEffect> CostGEClass = nullptr;
  if (const UGameplayEffect* CostGE = GetCostGameplayEffect())
  {
    CostGEClass = CostGE->GetClass();
  }
  else if (EvaluatedManaCost > 0.f)
  {
    CostGEClass = UGE_AeyerjiAbilityCostMana::StaticClass();
  }

  if (CostGEClass)
  {
    FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CostGEClass, GetAbilityLevel(Handle, ActorInfo));
    ApplyAbilitySetByCallerToSpec(SpecHandle, EvaluatedManaCost, EvaluatedCooldownSeconds);

    if (SpecHandle.IsValid())
    {
      ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
    }
  }
}

void UGA_AeyerjiBase::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const      FGameplayAbilityActivationInfo ActivationInfo) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return;
  }

  float EvaluatedManaCost = 0.f;
  float EvaluatedCooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), EvaluatedManaCost, EvaluatedCooldownSeconds);

  // Default path mirrors UGameplayAbility::ApplyCooldown but adds SetByCaller.
  TSubclassOf<UGameplayEffect> CooldownGEClass = nullptr;
  if (const UGameplayEffect* CooldownGE = GetCooldownGameplayEffect())
  {
    CooldownGEClass = CooldownGE->GetClass();
  }
  else if (EvaluatedCooldownSeconds > 0.f)
  {
    CooldownGEClass = UGE_AeyerjiAbilityCooldown::StaticClass();
  }

  if (CooldownGEClass)
  {
    FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGEClass, GetAbilityLevel(Handle, ActorInfo));
    ApplyAbilitySetByCallerToSpec(SpecHandle, EvaluatedManaCost, EvaluatedCooldownSeconds);

    if (SpecHandle.IsValid())
    {
      if (const FAeyerjiAbilityTableRow* Row = GetAbilityTuningRow(ActorInfo->AbilitySystemComponent.Get()))
      {
        const FGameplayTag CooldownTag = UAeyerjiAbilityTuningSubsystem::NormalizeCooldownTag(Row->CooldownTag);
        if (CooldownTag.IsValid() && SpecHandle.Data.IsValid())
        {
          SpecHandle.Data->DynamicGrantedTags.AddTag(CooldownTag);
        }
      }
      ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
    }
  }
}

const FGameplayTagContainer* UGA_AeyerjiBase::GetCooldownTags() const
{
  RuntimeCooldownTags.Reset();

  if (const FAeyerjiAbilityTableRow* Row = GetAbilityTuningRow())
  {
    const FGameplayTag CooldownTag = UAeyerjiAbilityTuningSubsystem::NormalizeCooldownTag(Row->CooldownTag);
    if (CooldownTag.IsValid())
    {
      RuntimeCooldownTags.AddTag(CooldownTag);
    }
  }

  if (RuntimeCooldownTags.IsEmpty())
  {
    return Super::GetCooldownTags();
  }

  const FGameplayTagContainer* ParentTags = Super::GetCooldownTags();
  if (ParentTags)
  {
    RuntimeCooldownTags.AppendTags(*ParentTags);
  }

  return &RuntimeCooldownTags;
}

bool UGA_AeyerjiBase::BP_TryCommitAbility(bool bEndAbilityOnFailure)

{

  return TryCommitAbilityInternal(

      GetCurrentAbilitySpecHandle(),

      GetCurrentActorInfo(),

      GetCurrentActivationInfo(),

      bEndAbilityOnFailure);
}

bool UGA_AeyerjiBase::TeleportCharacterSafely(

    AAeyerjiCharacter *Character,

    const FVector &DesiredLocation,

    const FRotator &DesiredRotation,

    float GroundTraceDistance,

    float CapsuleInflation,

    FVector &OutFinalLocation) const

{

  if (!Character)

  {

    OutFinalLocation = DesiredLocation;

    return false;
  }
  FVector TargetLocation = DesiredLocation;
  const float DefaultTraceHeight = 200.f;
  const float DefaultTraceDepth = 300.f;
  const float TraceHeight =
      GroundTraceDistance > 0.f ? GroundTraceDistance : DefaultTraceHeight;
  const float TraceDepth =
      GroundTraceDistance > 0.f ? GroundTraceDistance : DefaultTraceDepth;
  const float AdditionalOffset =
      FMath::Max(2.f, CapsuleInflation);

  if (!UMouseNavBlueprintLibrary::ResolveGroundedTeleportLocation(
          Character,
          DesiredLocation,
          Character,
          TargetLocation,
          TraceHeight,
          TraceDepth,
          AdditionalOffset))

  {

    OutFinalLocation = DesiredLocation;

    return false;
  }

  if (!UMouseNavBlueprintLibrary::IsTeleportLocationClear(
          Character,
          TargetLocation,
          Character,
          CapsuleInflation))

  {

    OutFinalLocation = DesiredLocation;

    return false;
  }

  OutFinalLocation = TargetLocation;

  const bool bTeleported =
      Character->TeleportTo(TargetLocation, DesiredRotation);

  if (bTeleported)

  {

    if (UCharacterMovementComponent *MoveComp =
            Character->GetCharacterMovement())

    {

      MoveComp->StopMovementImmediately();
      MoveComp->UpdateComponentVelocity();
    }
  }

  return bTeleported;
}

bool UGA_AeyerjiBase::BP_TeleportOwnerSafely(

    FVector DesiredLocation,

    FRotator DesiredRotation,

    FVector &OutFinalLocation,

    float GroundTraceDistance,

    float CapsuleInflation) const

{

  return TeleportCharacterSafely(

      GetAeyerjiCharacter(GetCurrentActorInfo()),

      DesiredLocation,

      DesiredRotation,

      GroundTraceDistance,

      CapsuleInflation,

      OutFinalLocation);
}

bool UGA_AeyerjiBase::IsOwnerDead(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  if (const UAbilitySystemComponent *ASC = GetAeyerjiAbilitySystem(ActorInfo))

  {

    return ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead);
  }

  return false;
}
