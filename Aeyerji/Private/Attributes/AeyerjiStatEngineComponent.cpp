// File: Source/Aeyerji/Private/Attributes/AeyerjiStatEngineComponent.cpp
#include "Attributes/AeyerjiStatEngineComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/GE_SecondaryStatsFromPrimaries.h"
#include "Attributes/GE_Regen_Periodic.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyParentNative.h"

UAeyerjiStatEngineComponent::UAeyerjiStatEngineComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
    DerivedEffectClass = UGE_SecondaryStatsFromPrimaries::StaticClass();
    RegenEffectClass   = UGE_Regen_Periodic::StaticClass();
}

void UAeyerjiStatEngineComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return; // server-only; attributes replicate down to clients
    }

    if (!Cast<AEnemyParentNative>(GetOwner()))
    {
        SubscribeToPrimaries();
        ReapplyDerivedEffect();
    }

    // Enemies now use authored JSON values directly; keep regen support but skip runtime derived-stat mutation.
    StartRegenTimer();
}

void UAeyerjiStatEngineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RegenTickHandle);
    }

    Super::EndPlay(EndPlayReason);
}

void UAeyerjiStatEngineComponent::StartRegenTimer()
{
    bRegenRetryQueued = false;

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    UAbilitySystemComponent* ASC = GetASC();
    if (!ASC
        || !ASC->GetAvatarActor()
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPRegenAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetManaAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetManaRegenAttribute()))
    {
        QueueRegenRetry();
        return;
    }

    if (UWorld* World = GetWorld())
    {
        const float SafeInterval = FMath::Max(0.01f, RegenTickInterval);
        World->GetTimerManager().SetTimer(RegenTickHandle, this, &UAeyerjiStatEngineComponent::TickRegeneration, SafeInterval, true);
    }
}

void UAeyerjiStatEngineComponent::TickRegeneration()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    UAbilitySystemComponent* ASC = GetASC();
    if (!ASC)
    {
        return;
    }

    const float HP = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
    const float HPMax = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
    const float HPRegen = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPRegenAttribute());
    if (HP > 0.f && HP < HPMax && HPRegen > 0.f)
    {
        const float HPDelta = FMath::Min(HPRegen * FMath::Max(0.01f, RegenTickInterval), HPMax - HP);
        if (HPDelta > KINDA_SMALL_NUMBER)
        {
            ASC->ApplyModToAttributeUnsafe(UAeyerjiAttributeSet::GetHPAttribute(), EGameplayModOp::Additive, HPDelta);
        }
    }

    const float Mana = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaAttribute());
    const float ManaMax = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute());
    const float ManaRegen = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaRegenAttribute());
    if (Mana < ManaMax && ManaRegen > 0.f)
    {
        const float ManaDelta = FMath::Min(ManaRegen * FMath::Max(0.01f, RegenTickInterval), ManaMax - Mana);
        if (ManaDelta > KINDA_SMALL_NUMBER)
        {
            ASC->ApplyModToAttributeUnsafe(UAeyerjiAttributeSet::GetManaAttribute(), EGameplayModOp::Additive, ManaDelta);
        }
    }
}

void UAeyerjiStatEngineComponent::TryApplyRegen()
{
    bRegenRetryQueued = false;

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    if (!RegenEffectClass || ActiveRegenHandle.IsValid())
    {
        return;
    }

    UAbilitySystemComponent* ASC = GetASC();
    if (!ASC)
    {
        QueueRegenRetry();
        return;
    }

    if (!ASC->GetAvatarActor()
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetHPRegenAttribute())
        || !ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetManaRegenAttribute()))
    {
        QueueRegenRetry();
        return;
    }

    FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
    Ctx.AddSourceObject(GetOwner());
    FGameplayEffectSpecHandle SH = ASC->MakeOutgoingSpec(RegenEffectClass, /*Level*/1.f, Ctx);
    if (SH.IsValid())
    {
        ActiveRegenHandle = ASC->ApplyGameplayEffectSpecToSelf(*SH.Data.Get());
    }
}

void UAeyerjiStatEngineComponent::QueueRegenRetry()
{
    if (bRegenRetryQueued)
    {
        return;
    }

    if (UWorld* World = GetWorld())
    {
        bRegenRetryQueued = true;
        FTimerHandle LocalHandle;
        World->GetTimerManager().SetTimer(LocalHandle, this, &UAeyerjiStatEngineComponent::StartRegenTimer, 0.1f, false);
    }
}

UAbilitySystemComponent* UAeyerjiStatEngineComponent::GetASC() const
{
    if (CachedASC.IsValid()) return CachedASC.Get();
    AActor* Owner = GetOwner(); if (!Owner) return nullptr;
    if (Owner->GetClass()->ImplementsInterface(UAbilitySystemInterface::StaticClass()))
    {
        if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner))
        {
            CachedASC = ASI->GetAbilitySystemComponent();
        }
    }
    else
    {
        CachedASC = Owner->FindComponentByClass<UAbilitySystemComponent>();
    }
    return CachedASC.Get();
}

const UAeyerjiAttributeSet* UAeyerjiStatEngineComponent::GetAttr() const
{
    if (CachedAttr.IsValid()) return CachedAttr.Get();
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        CachedAttr = ASC->GetSet<UAeyerjiAttributeSet>();
    }
    return CachedAttr.Get();
}

void UAeyerjiStatEngineComponent::SubscribeToPrimaries()
{
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetStrengthAttribute())
            .AddUObject(this, &UAeyerjiStatEngineComponent::OnPrimaryChanged);
        ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetAgilityAttribute())
            .AddUObject(this, &UAeyerjiStatEngineComponent::OnPrimaryChanged);
        ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetIntellectAttribute())
            .AddUObject(this, &UAeyerjiStatEngineComponent::OnPrimaryChanged);
    }
}

void UAeyerjiStatEngineComponent::OnPrimaryChanged(const FOnAttributeChangeData& /*Data*/)
{
    ReapplyDerivedEffect();
}

void UAeyerjiStatEngineComponent::ReapplyDerivedEffect()
{
    UAbilitySystemComponent* ASC = GetASC();
    const UAeyerjiAttributeSet* Attr = GetAttr();
    if (!ASC || !Attr || !DerivedEffectClass)
    {
        return;
    }

    if (ActiveDerivedHandle.IsValid())
    {
        ASC->RemoveActiveGameplayEffect(ActiveDerivedHandle);
        ActiveDerivedHandle.Invalidate();
    }

    FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
    Ctx.AddSourceObject(GetOwner());
    FGameplayEffectSpecHandle SH = ASC->MakeOutgoingSpec(DerivedEffectClass, /*Level*/1.f, Ctx);
    if (!SH.IsValid()) return;

    const UAeyerjiAttributeTuning* Tuning = UAeyerjiStatSettings::Get();
    FAeyerjiPrimaryToDerivedTuning Rules; // defaults
    if (Tuning) { Rules = Tuning->Rules; }

    const float Strength  = FMath::Max(0.f, Attr->GetStrength());
    const float Agility   = FMath::Max(0.f, Attr->GetAgility());
    const float Intellect = FMath::Max(0.f, Attr->GetIntellect());

    const float HpFromStr      = Strength  * Rules.StrengthToHP;
    const float ArmorFromStr   = Strength  * Rules.StrengthToArmor;
    const float DodgeFromAgi   = FMath::Clamp(Agility * Rules.AgilityToDodgeChance, 0.f, 1.f);
    const float ASFromAgi      = FMath::Max(0.f, Agility * Rules.AgilityToAttackSpeed);
    const float SpellFromInt   = FMath::Max(0.f, Intellect * Rules.IntellectToSpellPower);
    const float ManaFromInt    = FMath::Max(0.f, Intellect * Rules.IntellectToManaMax);
    const float ManaRegenFromInt = FMath::Max(0.f, Intellect * Rules.IntellectToManaRegen);
    const float HPRegenFromStr   = FMath::Max(0.f, Strength  * Rules.StrengthToHPRegen);

    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_HPMax,           HpFromStr);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_Armor,           ArmorFromStr);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_DodgeChance,     DodgeFromAgi);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_AttackSpeed,     ASFromAgi);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_SpellPower,      SpellFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_ManaMax,         ManaFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_ManaRegen,       ManaRegenFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_HPRegen,         HPRegenFromStr);

    ActiveDerivedHandle = ASC->ApplyGameplayEffectSpecToSelf(*SH.Data.Get());
}

void UAeyerjiStatEngineComponent::StopRegeneration()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RegenTickHandle);
    }

    if (ActiveRegenHandle.IsValid())
    {
        if (UAbilitySystemComponent* ASC = GetASC())
        {
            ASC->RemoveActiveGameplayEffect(ActiveRegenHandle);
        }

        ActiveRegenHandle.Invalidate();
    }
}
