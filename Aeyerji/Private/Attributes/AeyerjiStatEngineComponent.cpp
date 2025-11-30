// File: Source/Aeyerji/Private/Attributes/AeyerjiStatEngineComponent.cpp
#include "Attributes/AeyerjiStatEngineComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/GE_SecondaryStatsFromPrimaries.h"
#include "Attributes/GE_Regen_Periodic.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "AeyerjiGameplayTags.h"

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

    SubscribeToPrimaries();
    ReapplyDerivedEffect();

    // Apply regen once if configured (infinite periodic GE with AttributeBased magnitudes)
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        if (ActiveRegenHandle.IsValid())
        {
            ASC->RemoveActiveGameplayEffect(ActiveRegenHandle);
            ActiveRegenHandle.Invalidate();
        }
        if (RegenEffectClass)
        {
            FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
            Ctx.AddSourceObject(GetOwner());
            FGameplayEffectSpecHandle SH = ASC->MakeOutgoingSpec(RegenEffectClass, /*Level*/1.f, Ctx);
            if (SH.IsValid())
            {
                ActiveRegenHandle = ASC->ApplyGameplayEffectSpecToSelf(*SH.Data.Get());
            }
        }
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
        ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetAilmentAttribute())
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
    const float Ailment   = FMath::Max(0.f, Attr->GetAilment());

    const float HpFromStr      = Strength  * Rules.StrengthToHP;
    const float ArmorFromStr   = Strength  * Rules.StrengthToArmor;
    const float DodgeFromAgi   = FMath::Clamp(Agility * Rules.AgilityToDodgeChance, 0.f, 1.f);
    const float ASFromAgi      = FMath::Max(0.f, Agility * Rules.AgilityToAttackSpeed);
    const float SpellFromInt   = FMath::Max(0.f, Intellect * Rules.IntellectToSpellPower);
    const float ManaFromInt    = FMath::Max(0.f, Intellect * Rules.IntellectToManaMax);
    const float ManaRegenFromInt = FMath::Max(0.f, Intellect * Rules.IntellectToManaRegen);
    const float HPRegenFromStr   = FMath::Max(0.f, Strength  * Rules.StrengthToHPRegen);
    const float AilmentDPS     = FMath::Max(0.f, Ailment   * Rules.AilmentToDPS);
    const float AilmentDur     = FMath::Max(0.f, Ailment   * Rules.AilmentToDuration);

    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_HPMax,           HpFromStr);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_Armor,           ArmorFromStr);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_DodgeChance,     DodgeFromAgi);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_AttackSpeed,     ASFromAgi);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_SpellPower,      SpellFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_ManaMax,         ManaFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_ManaRegen,       ManaRegenFromInt);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_HPRegen,         HPRegenFromStr);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_AilmentDPS,      AilmentDPS);
    SH.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_PrimaryDerived_AilmentDuration, AilmentDur);

    ActiveDerivedHandle = ASC->ApplyGameplayEffectSpecToSelf(*SH.Data.Get());
}

void UAeyerjiStatEngineComponent::StopRegeneration()
{
    if (!ActiveRegenHandle.IsValid())
    {
        return;
    }

    if (UAbilitySystemComponent* ASC = GetASC())
    {
        ASC->RemoveActiveGameplayEffect(ActiveRegenHandle);
    }

    ActiveRegenHandle.Invalidate();
}
