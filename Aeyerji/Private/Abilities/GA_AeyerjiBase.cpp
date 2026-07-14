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
  ActivationBlockedTags.AddTag(AeyerjiTags::State_CrowdControl_Staggered);

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

void UGA_AeyerjiBase::EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, float& OutManaCost, float& OutCooldown) const
{
  EvaluateAbilityCostAndCooldown(ASC, ResolveAbilityRank(ASC), OutManaCost, OutCooldown);
}

void UGA_AeyerjiBase::EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, int32 AbilityRank, float& OutManaCost, float& OutCooldown) const
{
  FAeyerjiAbilityResolvedConfig Config;
  if (GetAbilityResolvedConfig(ASC, AbilityRank, Config))
  {
    OutManaCost = FMath::Max(0.f, Config.Cost.ManaCost);
    OutCooldown = FMath::Max(0.f, Config.Cost.Cooldown);
    if (ASC && OutCooldown > 0.f)
    {
      const float CooldownReduction = FMath::Clamp(
          ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetCooldownReductionAttribute()),
          0.f,
          0.40f);
      OutCooldown = ResolveCooldownWithReduction(OutCooldown, CooldownReduction);
    }
    return;
  }

  OutManaCost = 0.f;
  OutCooldown = 0.f;
}

float UGA_AeyerjiBase::ResolveCooldownWithReduction(const float BaseCooldown, const float CooldownReduction)
{
  return FMath::Max(0.f, BaseCooldown) * (1.f - FMath::Clamp(CooldownReduction, 0.f, 0.40f));
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

int32 UGA_AeyerjiBase::ResolveAbilityRank(const UAbilitySystemComponent* ASC) const
{
  if (!ASC)
  {
    return 1;
  }

  if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(GetClass()))
  {
    return FMath::Max(1, Spec->Level);
  }

  const FGameplayTag AbilityRowTag = ResolveAbilityTag();
  if (AbilityRowTag.IsValid())
  {
    for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
    {
      if (Spec.Ability && Spec.Ability->GetClass() == GetClass())
      {
        return FMath::Max(1, Spec.Level);
      }

      if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(AbilityRowTag))
      {
        return FMath::Max(1, Spec.Level);
      }
    }
  }

  return 1;
}

int32 UGA_AeyerjiBase::ResolveAbilityRank(const FGameplayAbilitySpecHandle& Handle, const FGameplayAbilityActorInfo* ActorInfo) const
{
  if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
  {
    if (const FGameplayAbilitySpec* Spec = ActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(Handle))
    {
      return FMath::Max(1, Spec->Level);
    }
  }

  return ResolveAbilityRank(ActorInfo && ActorInfo->AbilitySystemComponent.IsValid() ? ActorInfo->AbilitySystemComponent.Get() : nullptr);
}

bool UGA_AeyerjiBase::GetAbilityResolvedConfig(const UAbilitySystemComponent* ASC, int32 AbilityRank, FAeyerjiAbilityResolvedConfig& OutConfig) const
{
  const FGameplayTag RowTag = ResolveAbilityTag();
  if (!RowTag.IsValid())
  {
    OutConfig = FAeyerjiAbilityResolvedConfig();
    return false;
  }

  if (ASC)
  {
    if (UWorld* World = ASC->GetWorld())
    {
      if (UGameInstance* GI = World->GetGameInstance())
      {
        if (const UAeyerjiAbilityTuningSubsystem* Tuning = GI->GetSubsystem<UAeyerjiAbilityTuningSubsystem>())
        {
          if (Tuning->ResolveAbilityConfig(RowTag, AbilityRank, OutConfig))
          {
            return true;
          }
        }
      }
    }
  }

  const UDataTable* ConfiguredTable = UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable();
  if (!ConfiguredTable)
  {
    OutConfig = FAeyerjiAbilityResolvedConfig();
    return false;
  }

  const FAeyerjiAbilityTableRow* BaseRow = UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(ConfiguredTable, RowTag);
  if (!BaseRow || !UAeyerjiAbilityTuningSubsystem::MakeResolvedConfigFromBaseRow(*BaseRow, OutConfig))
  {
    OutConfig = FAeyerjiAbilityResolvedConfig();
    return false;
  }

  OutConfig.Rank = FMath::Max(1, AbilityRank);
  if (OutConfig.Rank > 1)
  {
    if (const UDataTable* RankTable = UAeyerjiAbilityTuningSubsystem::ResolveConfiguredRankTable())
    {
      if (const FAeyerjiAbilityRankTableRow* RankRow = UAeyerjiAbilityTuningSubsystem::FindAbilityRankRowInTable(RankTable, RowTag, OutConfig.Rank))
      {
        UAeyerjiAbilityTuningSubsystem::ApplyRankOverrides(*RankRow, OutConfig);
      }
    }
  }

  return true;
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

void UGA_AeyerjiBase::ApplyResolvedCooldownTagsToSpec(FGameplayEffectSpecHandle& SpecHandle) const
{
  if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  const FGameplayTagContainer* CooldownTags = GetCooldownTags();
  if (!CooldownTags || CooldownTags->IsEmpty())
  {
    return;
  }

  SpecHandle.Data->DynamicGrantedTags.AppendTags(*CooldownTags);
}

void UGA_AeyerjiBase::ApplyDamageTypeTagToSpec(FGameplayEffectSpecHandle& SpecHandle, const FGameplayTag& DamageTypeTag) const
{
  if (!DamageTypeTag.IsValid() || !SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
}

void UGA_AeyerjiBase::ApplyDefaultDamageRulesToSpec(FGameplayEffectSpecHandle& SpecHandle) const
{
  if (SpecHandle.IsValid() && SpecHandle.Data.IsValid())
  {
    DefaultDamageRules.ApplyToSpec(*SpecHandle.Data.Get());
  }
}

bool UGA_AeyerjiBase::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return false;
  }

  UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
  const int32 AbilityRank = ResolveAbilityRank(Handle, ActorInfo);

  float EvaluatedManaCost = 0.f;
  float EvaluatedCooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ASC, AbilityRank, EvaluatedManaCost, EvaluatedCooldownSeconds);

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
  if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
  {
    const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
    FGameplayTagContainer CooldownTags;

    FAeyerjiAbilityResolvedConfig Config;
    if (GetAbilityResolvedConfig(ASC, ResolveAbilityRank(Handle, ActorInfo), Config))
    {
      if (Config.CooldownTag.IsValid())
      {
        CooldownTags.AddTag(Config.CooldownTag);
      }
    }

    // Include subclass-exposed cooldown tags, such as Blink's local Cooldown.Blink tag.
    const FGameplayTagContainer* AbilityCooldownTags = GetCooldownTags();
    if (AbilityCooldownTags)
    {
      CooldownTags.AppendTags(*AbilityCooldownTags);
    }

    if (!CooldownTags.IsEmpty())
    {
      const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CooldownTags);
      const TArray<float> RemainingTimes = ASC->GetActiveEffectsTimeRemaining(Query);
      for (const float RemainingTime : RemainingTimes)
      {
        if (RemainingTime > KINDA_SMALL_NUMBER)
        {
          if (OptionalRelevantTags)
          {
            OptionalRelevantTags->AppendTags(CooldownTags);
          }
          return false;
        }
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
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), ResolveAbilityRank(Handle, ActorInfo), EvaluatedManaCost, EvaluatedCooldownSeconds);

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
  const int32 AbilityRank = ResolveAbilityRank(Handle, ActorInfo);
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), AbilityRank, EvaluatedManaCost, EvaluatedCooldownSeconds);

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
      FAeyerjiAbilityResolvedConfig Config;
      if (GetAbilityResolvedConfig(ActorInfo->AbilitySystemComponent.Get(), AbilityRank, Config))
      {
        if (Config.CooldownTag.IsValid() && SpecHandle.Data.IsValid())
        {
          SpecHandle.Data->DynamicGrantedTags.AddTag(Config.CooldownTag);
        }
      }
      ApplyResolvedCooldownTagsToSpec(SpecHandle);
      ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
    }
  }
}

const FGameplayTagContainer* UGA_AeyerjiBase::GetCooldownTags() const
{
  RuntimeCooldownTags.Reset();

  FAeyerjiAbilityResolvedConfig Config;
  if (GetAbilityResolvedConfig(nullptr, 1, Config))
  {
    if (Config.CooldownTag.IsValid())
    {
      RuntimeCooldownTags.AddTag(Config.CooldownTag);
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
