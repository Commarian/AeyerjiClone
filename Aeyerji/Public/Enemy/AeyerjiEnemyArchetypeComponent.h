// AeyerjiEnemyArchetypeComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "AeyerjiEnemyArchetypeComponent.generated.h"

class UAeyerjiEnemyArchetypeData;
class UAeyerjiEnemyArchetypeLibrary;
class UAeyerjiEnemyTraitComponent;
class UAbilitySystemComponent;
class UAnimMontage;
class UGameplayAbility;
class UGameplayEffect;
struct FAeyerjiEnemyArchetypeEntry;
struct FAeyerjiEnemyStatMultipliers;

/**
 * Runtime applicator for enemy archetype data assets.
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiEnemyArchetypeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiEnemyArchetypeComponent();

	// Applies the current archetype to the owning enemy (server only).
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Archetype")
	void ApplyArchetype();

	// Sets the archetype asset and optionally applies it immediately.
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Archetype")
	void SetArchetypeData(UAeyerjiEnemyArchetypeData* NewData, bool bApplyImmediately = true);

	// Sets the archetype library + tag and optionally applies it immediately.
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Archetype")
	void SetArchetypeFromLibrary(UAeyerjiEnemyArchetypeLibrary* NewLibrary, const FGameplayTag& NewTag, bool bApplyImmediately = true);

	// Applies mesh/animation overrides from the archetype (runs on all roles).
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Archetype")
	void ApplyArchetypeVisuals(bool bAllowInEditor = false, bool bForce = false);

	// Returns the resolved archetype asset without forcing a load.
	UFUNCTION(BlueprintPure, Category="Aeyerji|Archetype")
	const UAeyerjiEnemyArchetypeData* GetArchetypeData() const;

	// Returns the archetype tag without forcing a load.
	UFUNCTION(BlueprintPure, Category="Aeyerji|Archetype")
	FGameplayTag GetArchetypeTag() const;

	// Returns the archetype attack montage (loads the asset if needed).
	UFUNCTION(BlueprintPure, Category="Aeyerji|Archetype")
	UAnimMontage* GetAttackMontage() const;

	// Returns the optional basic attack effect reference.
	UFUNCTION(BlueprintPure, Category="Aeyerji|Archetype")
	TSubclassOf<UGameplayEffect> GetBasicAttackEffect() const;

	// Copies the archetype's granted tags into the output container.
	UFUNCTION(BlueprintPure, Category="Aeyerji|Archetype")
	void GetGrantedTags(FGameplayTagContainer& OutTags) const;

	// Returns the archetype stat multipliers.
	const FAeyerjiEnemyStatMultipliers* GetStatMultipliers() const;

	// True if an archetype asset or library entry is assigned.
	bool HasArchetypeData() const;

	// Controls whether ApplyArchetype runs automatically on BeginPlay.
	void SetAutoApplyOnBeginPlay(bool bInAutoApply) { bAutoApplyOnBeginPlay = bInAutoApply; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Re-applies visuals when the archetype source changes on clients.
	UFUNCTION()
	void OnRep_ArchetypeSource();

	const UAeyerjiEnemyArchetypeData* ResolveArchetypeData(bool bLoadIfNeeded) const;
	// Returns the resolved archetype entry from the library.
	const FAeyerjiEnemyArchetypeEntry* ResolveArchetypeEntry(bool bLoadIfNeeded) const;
	void ValidateArchetype(const UAeyerjiEnemyArchetypeData& Data);
	void ValidateArchetype(const FAeyerjiEnemyArchetypeEntry& Data);
	// Initializes base attribute values from the archetype's defaults table.
	void ApplyAttributeDefaults(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC);
	void ApplyAttributeDefaults(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC);
	void ApplyGrantedTags(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC);
	void ApplyGrantedTags(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC);
	void ApplyTeamOverrides(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC);
	void ApplyTeamOverrides(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC);
	void GrantAbilities(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC);
	void GrantAbilities(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC);
	void ApplyInitEffects(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC);
	void ApplyInitEffects(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC);
	void AddTraitComponents(const UAeyerjiEnemyArchetypeData& Data);
	void AddTraitComponents(const FAeyerjiEnemyArchetypeEntry& Data);
	void ApplyMeshOverrides(const UAeyerjiEnemyArchetypeData& Data);
	void ApplyMeshOverrides(const FAeyerjiEnemyArchetypeEntry& Data);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ArchetypeSource, Category="Archetype", meta=(AllowPrivateAccess="true"))
	TSoftObjectPtr<UAeyerjiEnemyArchetypeData> ArchetypeData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ArchetypeSource, Category="Archetype", meta=(AllowPrivateAccess="true"))
	TSoftObjectPtr<UAeyerjiEnemyArchetypeLibrary> ArchetypeLibrary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ArchetypeSource, Category="Archetype", meta=(AllowPrivateAccess="true"))
	FGameplayTag ArchetypeTag;

	// Prevents repeating runtime warnings for archetype source setup issues.
	mutable bool bLoggedInvalidLibraryTag = false;
	mutable bool bLoggedMissingLibraryEntry = false;
	mutable bool bLoggedBothSources = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Archetype", meta=(AllowPrivateAccess="true"))
	bool bAutoApplyOnBeginPlay = true;

	bool bApplied = false;
	bool bValidated = false;
	bool bVisualsApplied = false;
};
