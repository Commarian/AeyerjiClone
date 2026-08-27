#include "Abilities/GA_AeyerjiBase.h"

#include "AbilitySystemComponent.h"

#include "Animation/AnimMontage.h"

#include "AeyerjiCharacter.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiGameplayTags.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"

#include "Engine/World.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"
#include "GAS/GE_AeyerjiAbilityCostMana.h"
#include "MouseNavBlueprintLibrary.h"
#include "TimerManager.h"

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

void UGA_AeyerjiBase::ActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData* TriggerEventData)
{
  if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || IsOwnerDead(ActorInfo))
  {
    EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
    return;
  }

  const UClass* RuntimeClass = GetClass();
  const bool bIsBlueprintGenerated = RuntimeClass
      && RuntimeClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
  const UClass* FirstNativeParent = RuntimeClass;
  while (FirstNativeParent
      && FirstNativeParent->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
  {
    FirstNativeParent = FirstNativeParent->GetSuperClass();
  }

  // Only classes whose first native parent is this base use the generic Blueprint
  // deferral. Native subclasses retain their own activation timing even when the
  // granted class is a Blueprint child and their implementation calls Super.
  if (!bIsBlueprintGenerated || FirstNativeParent != StaticClass())
  {
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    return;
  }

  FAeyerjiAbilityResolvedConfig Config;
  if (!GetAbilityResolvedConfig(
          ActorInfo->AbilitySystemComponent.Get(),
          ResolveAbilityRank(Handle, ActorInfo),
          Config)
      || Config.Visuals.Montage.IsNull())
  {
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    return;
  }

  const float ImpactDelaySeconds = CalculateAbilityImpactDelay(Config);
  BeginAbilityCastPresentation(*ActorInfo, Config, ImpactDelaySeconds);
  if (ImpactDelaySeconds <= KINDA_SMALL_NUMBER)
  {
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    return;
  }

  UWorld* World = ActorInfo->AvatarActor->GetWorld();
  if (!World)
  {
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
    return;
  }

  const bool bHasTriggerEventData = TriggerEventData != nullptr;
  const FGameplayEventData TriggerEventDataCopy = bHasTriggerEventData
      ? *TriggerEventData
      : FGameplayEventData();

  FTimerDelegate ActivationDelegate;
  ActivationDelegate.BindWeakLambda(
      this,
      [this, Handle, ActivationInfo, TriggerEventDataCopy, bHasTriggerEventData]()
      {
        ContinueDeferredBlueprintActivation(
            Handle,
            GetCurrentActorInfo(),
            ActivationInfo,
            TriggerEventDataCopy,
            bHasTriggerEventData);
      });
  World->GetTimerManager().SetTimer(
      DeferredBlueprintActivationTimerHandle,
      ActivationDelegate,
      ImpactDelaySeconds,
      false);
}

void UGA_AeyerjiBase::EndAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const bool bReplicateEndAbility,
    const bool bWasCancelled)
{
  const FGameplayAbilityActorInfo* EffectiveActorInfo = ActorInfo
      ? ActorInfo
      : GetCurrentActorInfo();
  if (EffectiveActorInfo && EffectiveActorInfo->AvatarActor.IsValid())
  {
    if (UWorld* World = EffectiveActorInfo->AvatarActor->GetWorld())
    {
      World->GetTimerManager().ClearTimer(DeferredBlueprintActivationTimerHandle);
    }
  }

  RemoveOwnedAbilityCastLock(EffectiveActorInfo);
  Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGA_AeyerjiBase::CanActivateAbility(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,
    const FGameplayTagContainer* TargetTags,
    FGameplayTagContainer* OptionalRelevantTags) const
{
  const UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(ActorInfo);
  if (ASC && ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting))
  {
    if (OptionalRelevantTags)
    {
      OptionalRelevantTags->AddTag(AeyerjiTags::State_Ability_Casting);
    }
    return false;
  }

  return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

float UGA_AeyerjiBase::CalculateAbilityImpactDelay(const FAeyerjiAbilityResolvedConfig& Config) const
{
  if (FMath::IsFinite(Config.Visuals.ImpactDelaySeconds)
      && Config.Visuals.ImpactDelaySeconds >= 0.f)
  {
    return Config.Visuals.ImpactDelaySeconds;
  }

  if (Config.Visuals.Montage.IsNull())
  {
    return 0.f;
  }

  UAnimMontage* Montage = Config.Visuals.Montage.Get();
  if (!Montage)
  {
    Montage = Config.Visuals.Montage.LoadSynchronous();
  }
  if (!Montage)
  {
    return 0.f;
  }

  const float PlayRate = FMath::IsFinite(Config.Visuals.MontagePlayRate)
      ? FMath::Max(0.01f, Config.Visuals.MontagePlayRate)
      : 1.f;
  return FMath::Max(0.f, Montage->GetPlayLength() * 0.5f / PlayRate);
}

void UGA_AeyerjiBase::BeginAbilityCastPresentation(
    const FGameplayAbilityActorInfo& ActorInfo,
    const FAeyerjiAbilityResolvedConfig& Config,
    const float ImpactDelaySeconds)
{
  AAeyerjiCharacter* Character = Cast<AAeyerjiCharacter>(ActorInfo.AvatarActor.Get());
  if (!Character || !Character->HasAuthority())
  {
    return;
  }

  if (ImpactDelaySeconds > KINDA_SMALL_NUMBER)
  {
    if (UAbilitySystemComponent* ASC = ActorInfo.AbilitySystemComponent.Get())
    {
      FGameplayTagContainer PrimaryAttackTags;
      PrimaryAttackTags.AddTag(AeyerjiTags::Ability_Primary);
      ASC->CancelAbilities(&PrimaryAttackTags);

      if (AeyerjiTags::State_Ability_Casting.GetTag().IsValid())
      {
        ASC->AddLooseGameplayTag(
            AeyerjiTags::State_Ability_Casting,
            1,
            EGameplayTagReplicationState::TagOnly);
        bOwnsAbilityCastLock = true;
      }
    }

    if (AController* Controller = Character->GetController())
    {
      Controller->StopMovement();
    }
    if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
    {
      Movement->StopMovementImmediately();
    }
  }

  if (!Config.Visuals.Montage.IsNull())
  {
    UE_LOG(
        LogTemp,
        Display,
        TEXT("AbilityCast: playing %s for %s; impact in %.3fs."),
        *Config.Visuals.Montage.ToSoftObjectPath().ToString(),
        *Config.AbilityTag.ToString(),
        ImpactDelaySeconds);
    Character->Multicast_PlayAbilityMontageByPath(
        Config.Visuals.Montage.ToSoftObjectPath(),
        Config.Visuals.MontagePlayRate);
  }
}

void UGA_AeyerjiBase::ContinueDeferredBlueprintActivation(
    const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,
    const FGameplayEventData TriggerEventData,
    const bool bHasTriggerEventData)
{
  if (!IsActive())
  {
    return;
  }
  if (!ActorInfo || !ActorInfo->AvatarActor.IsValid() || IsOwnerDead(ActorInfo))
  {
    EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
    return;
  }

  Super::ActivateAbility(
      Handle,
      ActorInfo,
      ActivationInfo,
      bHasTriggerEventData ? &TriggerEventData : nullptr);
}

void UGA_AeyerjiBase::RemoveOwnedAbilityCastLock(const FGameplayAbilityActorInfo* ActorInfo)
{
  if (!bOwnsAbilityCastLock)
  {
    return;
  }

  if (ActorInfo)
  {
    if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
    {
      if (ASC->GetTagCount(AeyerjiTags::State_Ability_Casting) > 0)
      {
        ASC->RemoveLooseGameplayTag(
            AeyerjiTags::State_Ability_Casting,
            1,
            EGameplayTagReplicationState::TagOnly);
      }
    }
  }
  bOwnsAbilityCastLock = false;
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
    OutManaCost = FMath::IsFinite(Config.Cost.ManaCost) ? FMath::Max(0.f, Config.Cost.ManaCost) : 0.f;
    OutCooldown = FMath::IsFinite(Config.Cost.Cooldown) ? FMath::Max(0.f, Config.Cost.Cooldown) : 0.f;
    if (ASC && OutCooldown > 0.f)
    {
      const float RawCooldownReduction = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetCooldownReductionAttribute());
      const float CooldownReduction = FMath::Clamp(
          FMath::IsFinite(RawCooldownReduction) ? RawCooldownReduction : 0.f,
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
  const float SafeBaseCooldown = FMath::IsFinite(BaseCooldown) ? FMath::Max(0.f, BaseCooldown) : 0.f;
  const float SafeReduction = FMath::IsFinite(CooldownReduction)
      ? FMath::Clamp(CooldownReduction, 0.f, 0.40f)
      : 0.f;
  return SafeBaseCooldown * (1.f - SafeReduction);
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

    if (Depth > BestDepth
        || (Depth == BestDepth && (!BestTag.IsValid() || TagString < BestTag.ToString())))
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
    const float SafeManaCost = FMath::IsFinite(InManaCost) ? FMath::Abs(InManaCost) : 0.f;
    Spec->SetSetByCallerMagnitude(AeyerjiTags::SBC_Cost_Mana, -SafeManaCost);
  }

  if (AeyerjiTags::SBC_CooldownSeconds.GetTag().IsValid())
  {
    const float SafeCooldown = FMath::IsFinite(InCooldown) ? FMath::Max(0.f, InCooldown) : 0.f;
    Spec->SetSetByCallerMagnitude(AeyerjiTags::SBC_CooldownSeconds, SafeCooldown);
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
    if (!ASC->HasAttributeSetForAttribute(ManaAttr))
    {
      return false;
    }

    const float CurrentMana = ASC->GetNumericAttribute(ManaAttr);
    if (!FMath::IsFinite(CurrentMana) || CurrentMana + KINDA_SMALL_NUMBER < EvaluatedManaCost)
    {
      return false;
    }
  }

  // If a cost GE exists, skip Super::CheckCost to avoid evaluating it without SetByCaller.
  if (GetCostGameplayEffect() != nullptr && EvaluatedManaCost > 0.f)
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

  if (!Character || !Character->HasAuthority()
      || DesiredLocation.ContainsNaN() || DesiredRotation.ContainsNaN())

  {

    OutFinalLocation = DesiredLocation;

    return false;
  }
  FVector TargetLocation = DesiredLocation;
  const float DefaultTraceHeight = 200.f;
  const float DefaultTraceDepth = 300.f;
  const float SafeGroundTraceDistance = FMath::IsFinite(GroundTraceDistance)
      ? FMath::Max(0.f, GroundTraceDistance)
      : 0.f;
  const float TraceHeight =
	  SafeGroundTraceDistance > 0.f ? SafeGroundTraceDistance : DefaultTraceHeight;
  const float TraceDepth =
	  SafeGroundTraceDistance > 0.f ? SafeGroundTraceDistance : DefaultTraceDepth;
  const float SafeCapsuleInflation = FMath::IsFinite(CapsuleInflation) ? FMath::Max(0.f, CapsuleInflation) : 0.f;
  const float AdditionalOffset = FMath::Max(2.f, SafeCapsuleInflation);

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
          SafeCapsuleInflation))

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
