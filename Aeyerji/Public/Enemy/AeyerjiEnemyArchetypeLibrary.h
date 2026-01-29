// AeyerjiEnemyArchetypeLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Enemy/AeyerjiEnemyArchetypeData.h"

#include "AeyerjiEnemyArchetypeLibrary.generated.h"

/**
 * Library entry that mirrors UAeyerjiEnemyArchetypeData fields for bulk editing.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyArchetypeEntry
{
	GENERATED_BODY()

	// Gameplay tag that represents the archetype (e.g., Enemy.Role.Assassin).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Archetype")
	FGameplayTag ArchetypeTag;

	// Gameplay tags applied to the ASC on spawn.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tags")
	FGameplayTagContainer GrantedTags;

	// Optional override for team ID (falls back to the enemy default if not set).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Team")
	bool bOverrideTeamId = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Team", meta=(EditCondition="bOverrideTeamId", ClampMin="0", ClampMax="255"))
	uint8 TeamIdOverride = 1;

	// Optional override for team gameplay tag (falls back to the enemy default if not set).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Team")
	bool bOverrideTeamTag = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Team", meta=(EditCondition="bOverrideTeamTag"))
	FGameplayTag TeamTagOverride = FGameplayTag::RequestGameplayTag(TEXT("Team.Enemy"));

	// Optional attack montage used by the basic attack flow.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat")
	TSoftObjectPtr<UAnimMontage> AttackMontage;

	// Ability level used when granting archetype abilities.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS", meta=(ClampMin="1"))
	int32 AbilityLevel = 1;

	// Effect level used when applying archetype gameplay effects.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS", meta=(ClampMin="0.01"))
	float EffectLevel = 1.f;

	// Abilities granted on spawn (in addition to any base startup abilities).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS")
	TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

	// Effects applied on spawn (in addition to any base startup effects).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS")
	TArray<TSubclassOf<UGameplayEffect>> InitGameplayEffects;

	// Optional reference used by the basic attack ability (not applied on spawn).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS")
	TSubclassOf<UGameplayEffect> BasicAttackEffect;

	// Optional attribute metadata table used to initialize base attribute values.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attributes")
	TSoftObjectPtr<UDataTable> AttributeDefaultsTable;

	// AttributeSet class to use with the defaults table (falls back to AeyerjiAttributeSet when unset).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attributes")
	TSubclassOf<UAttributeSet> AttributeSetClass;

	// Trait components attached to the pawn for modular behavior.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traits")
	TArray<TSubclassOf<UAeyerjiEnemyTraitComponent>> TraitComponents;

	// Optional aggro/spacing tuning for AI logic or traits to consume.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI")
	FAeyerjiEnemyAggroSettings AggroSettings;

	// Optional mesh/animation overrides for the pawn's skeletal mesh component.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visuals")
	FAeyerjiEnemyMeshOverrides MeshOverrides;

	// Stat multipliers applied after EnemyScalingTable values are computed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats")
	FAeyerjiEnemyStatMultipliers StatMultipliers;

	// Validation warnings for common setup issues.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnIfMissingAttackMontage = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnIfMissingInitEffects = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Validation")
	bool bWarnIfMissingGrantedTags = true;
};

/**
 * Single-asset library for editing multiple enemy archetypes in one place.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiEnemyArchetypeLibrary : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Returns the entry with the matching archetype tag if found.
	const FAeyerjiEnemyArchetypeEntry* FindEntryByTag(const FGameplayTag& Tag) const;

	// Copies the entry with the matching archetype tag into OutEntry.
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Archetype")
	bool TryGetEntryByTag(const FGameplayTag& Tag, FAeyerjiEnemyArchetypeEntry& OutEntry) const;

	// Archetypes stored in this library.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Archetype", meta=(TitleProperty="ArchetypeTag"))
	TArray<FAeyerjiEnemyArchetypeEntry> Entries;

#if WITH_EDITORONLY_DATA
	// Source archetype assets to migrate into Entries.
	UPROPERTY(EditAnywhere, Category="Archetype|Migration")
	TArray<TSoftObjectPtr<UAeyerjiEnemyArchetypeData>> MigrationSources;

	// Clears Entries before migrating when true.
	UPROPERTY(EditAnywhere, Category="Archetype|Migration")
	bool bMigrationClearExisting = false;

	// Overwrites entries with the same ArchetypeTag when true.
	UPROPERTY(EditAnywhere, Category="Archetype|Migration")
	bool bMigrationOverwriteExisting = true;
#endif

#if WITH_EDITOR
	// Builds or updates Entries using the MigrationSources list.
	UFUNCTION(CallInEditor, Category="Aeyerji|Archetype|Migration")
	void RunMigration();
#endif
};
