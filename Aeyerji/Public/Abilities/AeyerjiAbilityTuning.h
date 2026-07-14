#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "CharacterStatsLibrary.h"
#include "Engine/DataTable.h"
#include "GAS/AeyerjiDamageRules.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AeyerjiAbilityTuning.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UTexture2D;
class UAnimMontage;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EAeyerjiAbilityTargetShape : uint8
{
	SingleActor UMETA(DisplayName="Single Actor"),
	OwnerCone UMETA(DisplayName="Owner Cone"),
	OwnerRadius UMETA(DisplayName="Owner Radius"),
	GroundRadius UMETA(DisplayName="Ground Radius")
};

UENUM(BlueprintType)
enum class EAeyerjiAbilityTargetTeam : uint8
{
	Enemy UMETA(DisplayName="Enemy"),
	Friendly UMETA(DisplayName="Friendly"),
	Self UMETA(DisplayName="Self"),
	Any UMETA(DisplayName="Any")
};

/** Magnitude definition for damage/effects authored in the global ability table. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityMagnitude
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float FlatValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiStat SourceStat = EAeyerjiStat::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float SourceStatScalar = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftClassPtr<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag SetByCallerTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag DamageTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bUseDamageVariance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bCanCrit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bCanBeDodged = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bCanLifeSteal = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bCanTriggerOnHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage")
	bool bCanStagger = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage", meta=(ClampMin="-1.0"))
	float VarianceOverride = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage", meta=(ClampMin="0.0"))
	float CriticalMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage", meta=(ClampMin="0.0"))
	float ArmorShred = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ArmorPenetration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Damage", meta=(ClampMin="0.0"))
	float StaggerMultiplier = 1.f;

	/** Converts the serialized flat fields into the shared runtime rule configuration. */
	FAeyerjiDamageRuleConfig MakeDamageRuleConfig() const
	{
		FAeyerjiDamageRuleConfig Rules;
		Rules.bUseVariance = bUseDamageVariance;
		Rules.bCanCrit = bCanCrit;
		Rules.bCanBeDodged = bCanBeDodged;
		Rules.bCanLifeSteal = bCanLifeSteal;
		Rules.bCanTriggerOnHit = bCanTriggerOnHit;
		Rules.bCanStagger = bCanStagger;
		Rules.VarianceOverride = VarianceOverride;
		Rules.CriticalMultiplierOverride = CriticalMultiplierOverride;
		Rules.ArmorShred = ArmorShred;
		Rules.ArmorPenetration = ArmorPenetration;
		Rules.StaggerMultiplier = StaggerMultiplier;
		return Rules;
	}
};

/** Additional gameplay effect entry applied after the primary damage payload. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityAppliedEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftClassPtr<UGameplayEffect> GameplayEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag SetByCallerTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float Magnitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float EffectLevel = 1.f;
};

/** One-shot cosmetic payload authored in the global ability table. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityVisuals
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals", meta=(ClampMin="0.01"))
	float MontagePlayRate = 1.f;

	/** Seconds from commit/cast start to gameplay impact. Negative uses half of Montage length; zero impacts immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals", meta=(ClampMin="-1.0"))
	float ImpactDelaySeconds = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals")
	TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals")
	bool bAttachNiagaraToOwnerMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals", meta=(EditCondition="bAttachNiagaraToOwnerMesh"))
	FName NiagaraAttachSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals")
	FVector NiagaraOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Visuals")
	FVector NiagaraScale = FVector::OneVector;
};

/** Named float parameter authored per ability row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityFloatTunable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	float Value = 0.f;
};

/** Named bool parameter authored per ability row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityBoolTunable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	bool Value = false;
};

/** Named int parameter authored per ability row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityIntTunable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	int32 Value = 0;
};

/** Named gameplay tag parameter authored per ability row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityTagTunable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Value;
};

/** Named asset reference authored per ability row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityAssetTunable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FGameplayTag Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	FSoftObjectPath Value;
};

/** One global ability row. RowName should equal AbilityTag, e.g. Ability.AG.GravitonPull. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftClassPtr<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiTargetMode TargetMode = EAeyerjiTargetMode::Instant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|UI")
	int32 UIOrder = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="1"))
	int32 RequiredLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	bool bUnlockedByDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityCost Cost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag CooldownTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="0.0"))
	float PreviewRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="0.0"))
	float MaxRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiAbilityTargetShape Shape = EAeyerjiAbilityTargetShape::SingleActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiAbilityTargetTeam TargetTeam = EAeyerjiAbilityTargetTeam::Enemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="0.0"))
	float Radius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="0.0", ClampMax="180.0"))
	float ArcAngleDegrees = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="0"))
	int32 MaxTargets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityMagnitude Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TArray<FAeyerjiAbilityAppliedEffect> AdditionalEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityVisuals Visuals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityFloatTunable> FloatTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityBoolTunable> BoolTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityIntTunable> IntTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityTagTunable> TagTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityAssetTunable> AssetTunables;

	bool TryGetFloatTunable(FGameplayTag Key, float& OutValue) const;
	bool TryGetBoolTunable(FGameplayTag Key, bool& OutValue) const;
	bool TryGetIntTunable(FGameplayTag Key, int32& OutValue) const;
	bool TryGetTagTunable(FGameplayTag Key, FGameplayTag& OutValue) const;
	bool TryGetAssetTunable(FGameplayTag Key, FSoftObjectPath& OutValue) const;
};

/** One authored rank-up row keyed by (AbilityTag, Rank). Rank 1 remains the implicit base row. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityRankTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(ClampMin="2"))
	int32 Rank = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Progression", meta=(ClampMin="1"))
	int32 RequiredPlayerLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Progression", meta=(ClampMin="1"))
	int32 PointCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Progression", meta=(ClampMin="0"))
	int32 RequiredOtherPointSpendsSinceLastUpgrade = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideCost = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideCost"))
	FAeyerjiAbilityCost Cost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverridePreviewRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverridePreviewRange", ClampMin="0.0"))
	float PreviewRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideMaxRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideMaxRange", ClampMin="0.0"))
	float MaxRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideRadius = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideRadius", ClampMin="0.0"))
	float Radius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideArcAngleDegrees = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideArcAngleDegrees", ClampMin="0.0", ClampMax="180.0"))
	float ArcAngleDegrees = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideMaxTargets = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideMaxTargets", ClampMin="0"))
	int32 MaxTargets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideDamage = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideDamage"))
	FAeyerjiAbilityMagnitude Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides")
	bool bOverrideAdditionalEffects = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Overrides", meta=(EditCondition="bOverrideAdditionalEffects"))
	TArray<FAeyerjiAbilityAppliedEffect> AdditionalEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityFloatTunable> FloatTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityBoolTunable> BoolTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityIntTunable> IntTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityTagTunable> TagTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityAssetTunable> AssetTunables;
};

/** Merged base-row plus rank-row config resolved at runtime for a specific ability rank. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityResolvedConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftClassPtr<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag AbilityTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	int32 Rank = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiTargetMode TargetMode = EAeyerjiTargetMode::Instant;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability", meta=(MultiLine="true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|UI")
	int32 UIOrder = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	int32 RequiredLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	bool bUnlockedByDefault = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityCost Cost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FGameplayTag CooldownTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float PreviewRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float MaxRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiAbilityTargetShape Shape = EAeyerjiAbilityTargetShape::SingleActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	EAeyerjiAbilityTargetTeam TargetTeam = EAeyerjiAbilityTargetTeam::Enemy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float Radius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	float ArcAngleDegrees = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	int32 MaxTargets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityMagnitude Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	TArray<FAeyerjiAbilityAppliedEffect> AdditionalEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityVisuals Visuals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityFloatTunable> FloatTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityBoolTunable> BoolTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityIntTunable> IntTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityTagTunable> TagTunables;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Ability|Tunables")
	TArray<FAeyerjiAbilityAssetTunable> AssetTunables;

	bool TryGetFloatTunable(FGameplayTag Key, float& OutValue) const;
	bool TryGetBoolTunable(FGameplayTag Key, bool& OutValue) const;
	bool TryGetIntTunable(FGameplayTag Key, int32& OutValue) const;
	bool TryGetTagTunable(FGameplayTag Key, FGameplayTag& OutValue) const;
	bool TryGetAssetTunable(FGameplayTag Key, FSoftObjectPath& OutValue) const;
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Aeyerji Ability Tuning"))
class AEYERJI_API UAeyerjiAbilityTuningSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Abilities")
	TSoftObjectPtr<UDataTable> AbilityTuningTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Abilities")
	TSoftObjectPtr<UDataTable> AbilityRankTuningTable;
};

/** Loads and validates the global ability tuning table. */
UCLASS()
class AEYERJI_API UAeyerjiAbilityTuningSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void ReloadAbilityTuningTable();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void SetRuntimeAbilityTuningTable(UDataTable* InTable);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void SetRuntimeAbilityRankTuningTable(UDataTable* InTable);

	const FAeyerjiAbilityTableRow* FindAbilityRow(FGameplayTag AbilityTag) const;
	const FAeyerjiAbilityRankTableRow* FindAbilityRankRow(FGameplayTag AbilityTag, int32 Rank) const;
	bool ResolveAbilityConfig(FGameplayTag AbilityTag, int32 Rank, FAeyerjiAbilityResolvedConfig& OutConfig) const;
	bool BuildAbilitySlot(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const;
	void GetAllAbilityRows(TArray<const FAeyerjiAbilityTableRow*>& OutRows) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void GetAllAbilitySlots(TArray<FAeyerjiAbilitySlot>& OutSlots) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void GetAllAbilitySlotsSorted(TArray<FAeyerjiAbilitySlot>& OutSlots) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	bool BuildAbilitySlotByTag(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	int32 GetMaxAbilityRank(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	bool HasAuthoredAbilityRank(FGameplayTag AbilityTag, int32 Rank) const;

	static UDataTable* ResolveConfiguredTable();
	static UDataTable* ResolveConfiguredRankTable();
	static const FAeyerjiAbilityTableRow* FindAbilityRowInTable(const UDataTable* Table, FGameplayTag AbilityTag);
	static const FAeyerjiAbilityRankTableRow* FindAbilityRankRowInTable(const UDataTable* Table, FGameplayTag AbilityTag, int32 Rank);
	static bool BuildAbilitySlotFromRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilitySlot& OutSlot);
	static bool BuildAbilitySlotFromConfig(const FAeyerjiAbilityResolvedConfig& Config, FAeyerjiAbilitySlot& OutSlot);
	static bool MakeResolvedConfigFromBaseRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilityResolvedConfig& OutConfig);
	static void ApplyRankOverrides(const FAeyerjiAbilityRankTableRow& RankRow, FAeyerjiAbilityResolvedConfig& InOutConfig);

private:
	void RebuildCache();
	void ValidateRow(FName RowName, const FAeyerjiAbilityTableRow& Row) const;
	void ValidateRankRow(FName RowName, const FAeyerjiAbilityRankTableRow& Row) const;
	void LogLookupMiss(FGameplayTag AbilityTag) const;
	void LogRankLookupMiss(FGameplayTag AbilityTag, int32 Rank) const;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RuntimeAbilityTuningTable = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CachedAbilityTuningTable = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RuntimeAbilityRankTuningTable = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CachedAbilityRankTuningTable = nullptr;

	TMap<FGameplayTag, const FAeyerjiAbilityTableRow*> RowsByTag;
	TMap<FGameplayTag, TMap<int32, const FAeyerjiAbilityRankTableRow*>> RankRowsByTag;
	mutable TSet<FGameplayTag> LoggedLookupMisses;
	mutable TSet<FString> LoggedRankLookupMisses;
};
