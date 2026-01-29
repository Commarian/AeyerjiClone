// EnemyParentNative.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiCharacter.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/OutlineHighlightComponent.h"

#include "EnemyParentNative.generated.h"

class UPrimitiveComponent;
class UCapsuleComponent;

class UGameplayAbility;
class UAeyerjiEnemyArchetypeComponent;
class UAeyerjiEnemyArchetypeData;
class UAeyerjiEnemyTraitComponent;
class UAeyerjiLevelingComponent;
class UAeyerjiRewardConfigComponent;
struct FPropertyChangedEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnemyDiedSignature, AActor*, Enemy);
/**
 * Native base class for AI-controlled "creep" / enemy pawns.
 * Blueprint children should inherit from this (NOT from ACharacter directly).
 */
UCLASS()
class AEYERJI_API AEnemyParentNative : public AAeyerjiCharacter, public IGenericTeamAgentInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	
	/* IGenericTeamAgentInterface */
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamId; }
	virtual void SetGenericTeamId(const FGenericTeamId& NewID) override { TeamId = NewID; }


	/* IAbilitySystemInterface */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
	{
		return AbilitySystemAeyerji;
	}

	/* ====== LIFECYCLE ====== */

	AEnemyParentNative();

protected:
	/* --- AActor overrides --- */
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void NotifyActorBeginCursorOver() override;
	virtual void NotifyActorEndCursorOver() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/* Initialise ASC owner/avatar pointers */
	void InitAbilityActorInfo();

	/* Grant abilities & effects defined below (runs once, server only) */
	void GiveStartupAbilitiesAndEffects();

	/* --------- Death hook (Blueprint-extendable) --------- */
	virtual void OnDeath_Implementation();

	/* ====== DESIGN-TIME LISTS ====== */

	/** Abilities given on spawn (level 1) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	/** Gameplay Effects applied on spawn (e.g. passive buffs) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Deprecated: prefer ArchetypeComponent.ArchetypeData. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Archetype", meta=(DeprecatedProperty, DeprecationMessage="Use ArchetypeComponent.ArchetypeData instead."))
	TObjectPtr<UAeyerjiEnemyArchetypeData> ArchetypeData;

	/** Deprecated: prefer ArchetypeComponent auto-apply setting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Archetype", meta=(DeprecatedProperty, DeprecationMessage="Use ArchetypeComponent auto-apply setting instead."))
	bool bApplyArchetypeOnBeginPlay = true;

	/** Internal guard so we don't double-grant after seamless travel */
	bool bStartupGiven = false;

	UPROPERTY(EditDefaultsOnly, Category="AI")
	uint8 TeamId = 1;                            // 0 = Players by convention

	/** Gameplay tag that marks this pawn as an enemy by default. */
	UPROPERTY(EditDefaultsOnly, Category="Enemy|Team")
	FGameplayTag DefaultTeamTag;

	/** Active team gameplay tag replicated for client-side queries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ActiveTeamTag, Category="Enemy|Team")
	FGameplayTag ActiveTeamTag;

	UPROPERTY(Transient)
	FGameplayTag LastAppliedTeamTag;

public:
	/** Fired when this pawn dies so encounter directors can react immediately. */
	UPROPERTY(BlueprintAssignable, Category="Enemy|Events")
	FEnemyDiedSignature OnEnemyDied;

	/** Apply archetype tags, traits, abilities, and effects (server only). */
	UFUNCTION(BlueprintCallable, Category="Enemy|Archetype")
	void ApplyArchetypeData();

	/** Returns true if Candidate is hostile to this enemy's AI controller and does not carry InvalidTag (GAS/ASC). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enemy|AI")
	bool IsAliveAndHostile(const AActor* Candidate, FGameplayTag InvalidTag = FGameplayTag()) const;

	/** Returns true if tag is valid and NOT found on the EnemyParentNative object*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enemy|AI")
	bool IsAlive(FGameplayTag DeathTag = FGameplayTag()) const;

	/** Updates the active team tag and pushes it to the ASC (server only). */
	void SetActiveTeamTag(const FGameplayTag& NewTag);

	/** Assigns a new archetype asset and optionally applies it immediately (server only). */
	UFUNCTION(BlueprintCallable, Category="Enemy|Archetype")
	void SetArchetypeAndApply(UAeyerjiEnemyArchetypeData* NewArchetypeData, bool bApplyImmediately = true);

	// Applies archetype stat multipliers to a scaling value if configured.
	bool ApplyArchetypeStatMultipliers(const FName& AttributeName, float& InOutValue) const;

	// Returns the default team tag used when no archetype override is provided.
	FGameplayTag GetDefaultTeamTag() const { return DefaultTeamTag; }

	/** IGameplayTagAssetInterface */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	/* ====== OUTLINE HIGHLIGHT ====== */
public:
	UFUNCTION(BlueprintCallable, Category="Enemy|Highlight")
	void SetEnemyHighlighted(bool bInHighlighted);

	/** Cached scaling info applied on spawn (for loot/stat reads). */
	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	int32 GetScaledLevel() const { return CachedScaledLevel; }

	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	float GetScaledDifficulty() const { return CachedDifficultyScale; }

	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	FGameplayTag GetScalingSourceTag() const { return CachedScalingSourceTag; }

	void SetScalingSnapshot(int32 InLevel, float InDifficultyScale, const FGameplayTag& InSourceTag);
	
	bool IsHoverTargetComponent(const UPrimitiveComponent* Component) const;

protected:
	/** Applies optional crowd-focused animation/rendering throttles for performance testing. */
	void ApplyCrowdPerformanceSettings();

	/** Sizes the click target capsule from the main capsule (if enabled). */
	void RefreshClickTargetCapsule();

	void RefreshEnemyHighlightTargets();
	void UpdateEnemyHighlightState();
	void ConfigureEnemyOutlineComponent();
	void GrantAbilityList(const TArray<TSubclassOf<UGameplayAbility>>& Abilities, int32 AbilityLevel);
	void ApplyEffectList(const TArray<TSubclassOf<UGameplayEffect>>& Effects, float EffectLevel);
	void AddTraitComponents(const TArray<TSubclassOf<UAeyerjiEnemyTraitComponent>>& TraitComponents);
	void ApplyDefaultTeamTags();
	void ApplyActiveTeamTagToASC(const FGameplayTag& OldTag);

	UFUNCTION()
	void OnRep_ActiveTeamTag();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TObjectPtr<UOutlineHighlightComponent> OutlineHighlight;

	/** Extra capsule used for cursor click traces to make enemies easier to select. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Targeting")
	TObjectPtr<UCapsuleComponent> ClickTargetCapsule;

	UPROPERTY(EditDefaultsOnly, Category="Enemy|Targeting")
	bool bAutoSizeClickTargetCapsule = true;

	UPROPERTY(EditDefaultsOnly, Category="Enemy|Targeting", meta=(ClampMin="1.0"))
	float ClickTargetRadiusScale = 1.35f;

	UPROPERTY(EditDefaultsOnly, Category="Enemy|Targeting", meta=(ClampMin="1.0"))
	float ClickTargetHalfHeightScale = 1.20f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Archetype")
	TObjectPtr<UAeyerjiEnemyArchetypeComponent> ArchetypeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Progression")
	TObjectPtr<UAeyerjiLevelingComponent> LevelingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Progression")
	TObjectPtr<UAeyerjiRewardConfigComponent> RewardConfigComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TArray<TObjectPtr<UPrimitiveComponent>> AdditionalHighlightPrimitives;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight", meta=(ClampMin="0", ClampMax="255", DisplayName="Highlight Channel"))
	int32 HighlightChannel = 20;

	UPROPERTY()
	int32 HighlightStencilValue_DEPRECATED = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	bool bHighlightOnSpawn = false;

	UPROPERTY(BlueprintReadOnly, Category="Enemy|Highlight")
	bool bEnemyHighlighted = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Enemy|Highlight")
	int32 HoverHighlightRefCount = 0;

	/** Enables crowd performance overrides on this enemy (opt-in). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance")
	bool bEnableCrowdPerformanceSettings = false;

	/** Skip crowd performance overrides on this enemy (bosses, hero units, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bIgnoreCrowdPerformanceSettings = false;

	/** Enable skeletal mesh update rate optimizations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bEnableUpdateRateOptimizations = true;

	/** Only tick animation pose when the mesh is rendered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bOnlyTickPoseWhenRendered = true;

	/** Minimum LOD to render when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0"))
	int32 CrowdMinLOD = 0;

	/** Force a specific LOD when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0"))
	int32 CrowdForcedLOD = 0;

	/** Max draw distance for the enemy mesh when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0.0", Units="cm"))
	float CrowdMaxDrawDistance = 0.f;

	/** Disable dynamic shadows for the enemy mesh when crowd settings are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bDisableDynamicShadows = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	int32 CachedScaledLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	float CachedDifficultyScale = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	FGameplayTag CachedScalingSourceTag;

	UFUNCTION()
	void HandleMeshBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleMeshEndCursorOver(UPrimitiveComponent* TouchedComponent);
};
