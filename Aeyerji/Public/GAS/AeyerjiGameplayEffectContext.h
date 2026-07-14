#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

#include "AeyerjiGameplayEffectContext.generated.h"

/** Authoritative result produced by a physical damage execution. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float BaseDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float PreMitigationDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float FinalDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float MitigatedDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float LifeStealFraction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	float StaggerDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	FGameplayTag DamageType;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	FGameplayTagContainer RuleTags;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	bool bWasCritical = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	bool bWasDodged = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	bool bWasFatal = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Combat")
	bool bTriggeredStagger = false;
};

/** Effect context used to replicate resolved combat information to GameplayCues. */
USTRUCT()
struct AEYERJI_API FAeyerjiGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FAeyerjiGameplayEffectContext* Duplicate() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	FAeyerjiDamageResult& GetMutableDamageResult() { return DamageResult; }
	const FAeyerjiDamageResult& GetDamageResult() const { return DamageResult; }

	static FAeyerjiGameplayEffectContext* ExtractMutable(FGameplayEffectContextHandle& Handle);
	static const FAeyerjiGameplayEffectContext* Extract(const FGameplayEffectContextHandle& Handle);

private:
	UPROPERTY()
	FAeyerjiDamageResult DamageResult;
};

template<>
struct TStructOpsTypeTraits<FAeyerjiGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FAeyerjiGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
