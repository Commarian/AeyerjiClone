// AeyerjiEnemyArchetypeData.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AeyerjiEnemyArchetypeData.generated.h"

class UAnimMontage;
class UAnimInstance;
class UAttributeSet;
class UDataTable;
class UGameplayAbility;
class UGameplayEffect;
class UMaterialInterface;
class USkeletalMesh;
class UAeyerjiEnemyTraitComponent;

/**
 * Lightweight aggro/spacing tuning that can be read by AI logic or traits.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyAggroSettings
{
	GENERATED_BODY()

	// Preferred combat distance; 0 means "use the current attack range".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aggro", meta=(ClampMin="0.0"))
	float PreferredRange = 0.f;

	// Maximum distance before the AI should leash back to its home location.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aggro", meta=(ClampMin="0.0"))
	float LeashDistance = 2000.f;

	// 0..1 bias toward flanking behavior (higher means stronger flank preference).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aggro", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FlankBias = 0.f;
};

/**
 * High-level stat multipliers applied after scaling table evaluation.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyStatMultipliers
{
	GENERATED_BODY()

	// Scales HP and HPMax.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0.0"))
	float HealthMultiplier = 1.0f;

	// Scales AttackDamage.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0.0"))
	float DamageMultiplier = 1.0f;

	// Scales WalkSpeed and RunSpeed.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0.0"))
	float MoveSpeedMultiplier = 1.0f;

	// Scales AttackSpeed and inversely scales AttackCooldown.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Stats", meta=(ClampMin="0.01"))
	float AttackRateMultiplier = 1.0f;
};

/**
 * Optional mesh/animation overrides applied before gameplay begins.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyMeshOverrides
{
	GENERATED_BODY()

	// Optional skeletal mesh to assign.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	// Optional animation blueprint class to use.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TSubclassOf<UAnimInstance> AnimClass;

	// Optional material overrides applied by slot index.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	// Optional relative transform for the mesh component.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
	bool bOverrideRelativeTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh", meta=(EditCondition="bOverrideRelativeTransform"))
	FTransform RelativeTransform = FTransform::Identity;
};

/**
 * Data-driven enemy archetype definition used by enemy pawns on spawn.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiEnemyArchetypeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
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
