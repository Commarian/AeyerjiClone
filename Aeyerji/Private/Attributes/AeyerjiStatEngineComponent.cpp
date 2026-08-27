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

namespace
{
	float StatEngineFiniteOrDefault(const float Value, const float DefaultValue = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}

	float ResolveRegenInterval(const float ConfiguredInterval)
	{
		return FMath::Max(0.01f, StatEngineFiniteOrDefault(ConfiguredInterval, 0.1f));
	}

	float ResolveDerivedMagnitude(const float AttributeValue, const float Multiplier)
	{
		const float Result = FMath::Max(0.f, StatEngineFiniteOrDefault(AttributeValue)) * FMath::Max(0.f, StatEngineFiniteOrDefault(Multiplier));
		return StatEngineFiniteOrDefault(Result);
	}
}

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
		World->GetTimerManager().ClearTimer(RegenRetryHandle);
    }

	if (UAbilitySystemComponent* ASC = GetASC())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetStrengthAttribute()).RemoveAll(this);
		ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetAgilityAttribute()).RemoveAll(this);
		ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetIntellectAttribute()).RemoveAll(this);
		if (ActiveDerivedHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(ActiveDerivedHandle);
			ActiveDerivedHandle.Invalidate();
		}
	}
	StopRegeneration();

    Super::EndPlay(EndPlayReason);
}

void UAeyerjiStatEngineComponent::EnsureRegenerationActive()
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    if (UAbilitySystemComponent* ASC = GetASC())
    {
		if (ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
        {
            return;
        }
    }

    StartRegenTimer();
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
		const float SafeInterval = ResolveRegenInterval(RegenTickInterval);
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

	const float SafeInterval = ResolveRegenInterval(RegenTickInterval);
	const float HP = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute())));
	const float HPMax = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute())));
	const float HPRegen = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPRegenAttribute())));
    if (HP > 0.f && HP < HPMax && HPRegen > 0.f)
    {
		const float HPDelta = FMath::Min(StatEngineFiniteOrDefault(HPRegen * SafeInterval), HPMax - HP);
        if (HPDelta > KINDA_SMALL_NUMBER)
        {
            ASC->ApplyModToAttributeUnsafe(UAeyerjiAttributeSet::GetHPAttribute(), EGameplayModOp::Additive, HPDelta);
        }
    }

	const float Mana = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaAttribute())));
	const float ManaMax = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute())));
	const float ManaRegen = FMath::Max(0.f, StatEngineFiniteOrDefault(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaRegenAttribute())));
    if (Mana < ManaMax && ManaRegen > 0.f)
    {
		const float ManaDelta = FMath::Min(StatEngineFiniteOrDefault(ManaRegen * SafeInterval), ManaMax - Mana);
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
		World->GetTimerManager().SetTimer(RegenRetryHandle, this, &UAeyerjiStatEngineComponent::StartRegenTimer, 0.1f, false);
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
    FAeyerjiCombatLimitsTuning CombatLimits; // defaults
    if (Tuning)
    {
        Rules = Tuning->Rules;
        CombatLimits = Tuning->CombatLimits;
    }

	const float Strength = FMath::Max(0.f, StatEngineFiniteOrDefault(Attr->GetStrength()));
	const float Agility = FMath::Max(0.f, StatEngineFiniteOrDefault(Attr->GetAgility()));
	const float Intellect = FMath::Max(0.f, StatEngineFiniteOrDefault(Attr->GetIntellect()));

	const float HpFromStr = ResolveDerivedMagnitude(Strength, Rules.StrengthToHP);
	const float ArmorFromStr = ResolveDerivedMagnitude(Strength, Rules.StrengthToArmor);
	const float DodgeFromAgi = FMath::Clamp(
		ResolveDerivedMagnitude(Agility, Rules.AgilityToDodgeChance),
		0.f,
		CombatLimits.GetSafeMaxDodgeChance());
	const float ASFromAgi = ResolveDerivedMagnitude(Agility, Rules.AgilityToAttackSpeed);
	const float SpellFromInt = ResolveDerivedMagnitude(Intellect, Rules.IntellectToSpellPower);
	const float ManaFromInt = ResolveDerivedMagnitude(Intellect, Rules.IntellectToManaMax);
	const float ManaRegenFromInt = ResolveDerivedMagnitude(Intellect, Rules.IntellectToManaRegen);
	const float HPRegenFromStr = ResolveDerivedMagnitude(Strength, Rules.StrengthToHPRegen);

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
		World->GetTimerManager().ClearTimer(RegenRetryHandle);
    }
	bRegenRetryQueued = false;

    if (ActiveRegenHandle.IsValid())
    {
        if (UAbilitySystemComponent* ASC = GetASC())
        {
            ASC->RemoveActiveGameplayEffect(ActiveRegenHandle);
        }

        ActiveRegenHandle.Invalidate();
    }
}
