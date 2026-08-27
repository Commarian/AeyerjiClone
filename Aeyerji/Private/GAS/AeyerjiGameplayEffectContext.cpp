#include "GAS/AeyerjiGameplayEffectContext.h"

UScriptStruct* FAeyerjiGameplayEffectContext::GetScriptStruct() const
{
	return StaticStruct();
}

FAeyerjiGameplayEffectContext* FAeyerjiGameplayEffectContext::Duplicate() const
{
	FAeyerjiGameplayEffectContext* NewContext = new FAeyerjiGameplayEffectContext();
	*NewContext = *this;
	if (const FHitResult* Hit = GetHitResult())
	{
		NewContext->AddHitResult(*Hit, true);
	}
	return NewContext;
}

bool FAeyerjiGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bParentSuccess = true;
	FGameplayEffectContext::NetSerialize(Ar, Map, bParentSuccess);

	Ar << DamageResult.BaseDamage;
	Ar << DamageResult.PreMitigationDamage;
	Ar << DamageResult.FinalDamage;
	Ar << DamageResult.MitigatedDamage;
	Ar << DamageResult.LifeStealFraction;
	Ar << DamageResult.StaggerDamage;

	bool bDamageTypeSuccess = true;
	DamageResult.DamageType.NetSerialize(Ar, Map, bDamageTypeSuccess);
	bool bRuleTagsSuccess = true;
	DamageResult.RuleTags.NetSerialize(Ar, Map, bRuleTagsSuccess);

	uint8 ResultFlags = 0;
	if (Ar.IsSaving())
	{
		ResultFlags |= DamageResult.bWasCritical ? 1 << 0 : 0;
		ResultFlags |= DamageResult.bWasDodged ? 1 << 1 : 0;
		ResultFlags |= DamageResult.bWasFatal ? 1 << 2 : 0;
		ResultFlags |= DamageResult.bTriggeredStagger ? 1 << 3 : 0;
	}
	Ar.SerializeBits(&ResultFlags, 4);
	if (Ar.IsLoading())
	{
		DamageResult.bWasCritical = (ResultFlags & (1 << 0)) != 0;
		DamageResult.bWasDodged = (ResultFlags & (1 << 1)) != 0;
		DamageResult.bWasFatal = (ResultFlags & (1 << 2)) != 0;
		DamageResult.bTriggeredStagger = (ResultFlags & (1 << 3)) != 0;

		auto SanitizeNonNegative = [](float& Value)
		{
			Value = FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
		};
		SanitizeNonNegative(DamageResult.BaseDamage);
		SanitizeNonNegative(DamageResult.PreMitigationDamage);
		SanitizeNonNegative(DamageResult.FinalDamage);
		SanitizeNonNegative(DamageResult.MitigatedDamage);
		SanitizeNonNegative(DamageResult.LifeStealFraction);
		SanitizeNonNegative(DamageResult.StaggerDamage);
	}

	bOutSuccess = bParentSuccess && bDamageTypeSuccess && bRuleTagsSuccess && !Ar.IsError();
	return true;
}

FAeyerjiGameplayEffectContext* FAeyerjiGameplayEffectContext::ExtractMutable(FGameplayEffectContextHandle& Handle)
{
	FGameplayEffectContext* Context = Handle.Get();
	const UScriptStruct* ContextStruct = Context ? Context->GetScriptStruct() : nullptr;
	return ContextStruct && ContextStruct->IsChildOf(StaticStruct())
		? static_cast<FAeyerjiGameplayEffectContext*>(Context)
		: nullptr;
}

const FAeyerjiGameplayEffectContext* FAeyerjiGameplayEffectContext::Extract(const FGameplayEffectContextHandle& Handle)
{
	const FGameplayEffectContext* Context = Handle.Get();
	const UScriptStruct* ContextStruct = Context ? Context->GetScriptStruct() : nullptr;
	return ContextStruct && ContextStruct->IsChildOf(StaticStruct())
		? static_cast<const FAeyerjiGameplayEffectContext*>(Context)
		: nullptr;
}
