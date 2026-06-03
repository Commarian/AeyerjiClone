#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "CharacterStatsLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AeyerjiAbilityTuning.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UTexture2D;

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
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Aeyerji Ability Tuning"))
class AEYERJI_API UAeyerjiAbilityTuningSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Abilities")
	TSoftObjectPtr<UDataTable> AbilityTuningTable;
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

	const FAeyerjiAbilityTableRow* FindAbilityRow(FGameplayTag AbilityTag) const;
	bool BuildAbilitySlot(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const;
	void GetAllAbilityRows(TArray<const FAeyerjiAbilityTableRow*>& OutRows) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void GetAllAbilitySlots(TArray<FAeyerjiAbilitySlot>& OutSlots) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	void GetAllAbilitySlotsSorted(TArray<FAeyerjiAbilitySlot>& OutSlots) const;

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Abilities")
	bool BuildAbilitySlotByTag(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const;

	static UDataTable* ResolveConfiguredTable();
	static FGameplayTag NormalizeAbilityTag(FGameplayTag AbilityTag);
	static FGameplayTag NormalizeCooldownTag(FGameplayTag CooldownTag);
	static const FAeyerjiAbilityTableRow* FindAbilityRowInTable(const UDataTable* Table, FGameplayTag AbilityTag);
	static bool BuildAbilitySlotFromRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilitySlot& OutSlot);

private:
	void RebuildCache();
	void ValidateRow(FName RowName, const FAeyerjiAbilityTableRow& Row) const;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> RuntimeAbilityTuningTable = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CachedAbilityTuningTable = nullptr;

	TMap<FGameplayTag, const FAeyerjiAbilityTableRow*> RowsByTag;
};
