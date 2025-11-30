// CharacterStatsLibrary.h
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "OnlineSubsystemTypes.h"          // FUniqueNetIdRepl
#include "Aeyerji/AeyerjiSaveGame.h"
#include "GameFramework/PlayerState.h"     // APlayerState
#include "GameplayTagContainer.h"
#include "CharacterStatsLibrary.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class AActor;
class AController;

/**
	Works together with @GetAttributeForAeyerjiStat in CharacterStatsLibrary.cpp
	Make sure to update both when adding/removing stats.
 */
UENUM(BlueprintType)
enum class EAeyerjiStat : uint8
{
    None                                UMETA(DisplayName = "None"),
    Agility                             UMETA(DisplayName = "Agility"),
    Ailment                             UMETA(DisplayName = "Ailment"),
    AilmentDPS                          UMETA(DisplayName = "Ailment DPS"),
    AilmentDuration                     UMETA(DisplayName = "Ailment Duration"),
    Armor                               UMETA(DisplayName = "Armor"),
    AttackAngle                         UMETA(DisplayName = "Attack Angle"),
    AttackCooldown                      UMETA(DisplayName = "Attack Cooldown"),
    AttackDamage                        UMETA(DisplayName = "Attack Damage"),
    AttackRange                         UMETA(DisplayName = "Attack Range"),
    AttackSpeed                         UMETA(DisplayName = "Attack Speed"),
    CooldownReduction                   UMETA(DisplayName = "Cooldown Reduction"),
    CritChance                          UMETA(DisplayName = "Crit Chance"),
    DodgeChance                         UMETA(DisplayName = "Dodge Chance"),
    HearingRange                        UMETA(DisplayName = "Hearing Range"),
    HP                                  UMETA(DisplayName = "HP"),
    HPMax                               UMETA(DisplayName = "HP Max"),
    HPRegen                             UMETA(DisplayName = "HP Regen"),
    Intellect                           UMETA(DisplayName = "Intellect"),
    Level                               UMETA(DisplayName = "Level"),
    Mana                                UMETA(DisplayName = "Mana"),
    ManaMax                             UMETA(DisplayName = "Mana Max"),
    ManaRegen                           UMETA(DisplayName = "Mana Regen"),
    PatrolRadius                        UMETA(DisplayName = "Patrol Radius"),
    ProjectilePredictionAmount          UMETA(DisplayName = "Projectile Prediction Amount"),
    ProjectileSpeedRanged               UMETA(DisplayName = "Projectile Speed (Ranged)"),
    RunSpeed                            UMETA(DisplayName = "Run Speed"),
    SpellPower                          UMETA(DisplayName = "Spell Power"),
    Strength                            UMETA(DisplayName = "Strength"),
    VisionRange                         UMETA(DisplayName = "Vision Range"),
    WalkSpeed                           UMETA(DisplayName = "Walk Speed"),
    XP                                  UMETA(DisplayName = "XP"),
    XPMax                               UMETA(DisplayName = "XP Max")
};

UCLASS()
class AEYERJI_API UCharacterStatsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/* ───────────────────────── Existing API (unchanged) ───────────────────────── */

	UFUNCTION(BlueprintPure, Category="SaveGame")
	static FString MakeStableCharSlotName(const class APlayerState* PS);
	
	UFUNCTION(BlueprintCallable, Category="SaveGame")
	static void LoadAeyerjiChar(UAeyerjiSaveGame* Data,
	                            class AAeyerjiPlayerState* PS,
	                            UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category="SaveGame")
	static FString SanitizeSaveSlotName(const FString& RawSlotName);

	UFUNCTION(BlueprintCallable, Category="SaveGame")
	static UAeyerjiSaveGame* LoadOrCreateAeyerjiSave(const FString& Slot, UPARAM(ref) bool& bOutLoadedFromDisk);

	UFUNCTION(BlueprintCallable, Category="SaveGame")
	static void SaveAeyerjiChar(UAeyerjiSaveGame* Data,
	                            const class AAeyerjiPlayerState* PS,
	                            FString Slot);

	static int32 TagDepth(const FGameplayTag& Tag);

	UFUNCTION(BlueprintCallable, Category="GAS|Tags")
	static FGameplayTag GetLeafTagFromBranchTag(
	    const UAbilitySystemComponent* ASC, FGameplayTag BranchTag);
	
	static FGameplayTag GetLeafTagFromBranchTag_Container(
		const UAbilitySystemComponent* ASC, const FGameplayTagContainer& BranchTags);
	
	static FGameplayTagContainer MakeContainerFromLeaf(
		const UAbilitySystemComponent* ASC, FGameplayTag BranchTag);

	UFUNCTION(BlueprintPure, Category="GAS|Tags")
	static TSubclassOf<UGameplayAbility> GetAbilityClassForBranchTag(
		const UAbilitySystemComponent* ASC, FGameplayTag BranchTag);

	UFUNCTION(BlueprintPure, Category="GAS|Tags")
	static UGameplayAbility* GetAbilityCDOForBranchTag(
		const UAbilitySystemComponent* ASC, FGameplayTag BranchTag);
    /** Blueprint helper that looks up a numeric Aeyerji stat on any actor that exposes an ASC. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Stats", meta=(DefaultToSelf="Actor", ExpandBoolAsExecs="ReturnValue", DisplayName="Get Aeyerji Stat From Actor"))
    static bool GetAeyerjiStatFromActor(const AActor* Actor, EAeyerjiStat Stat, float& OutValue);


	/* ─────────────────────── New “Move To Attack Range” API ───────────────────── */

	/** Reads AttackRange from the pawn's ASC (UAeyerjiAttributeSet::AttackRange).
	 *  Returns >0 on success; otherwise returns FallbackRange if provided (>0), else 0.
	 */
	UFUNCTION(BlueprintPure, Category="Movement|AttackRange")
	static float GetAttackRangeFromActorASC(const AActor* Actor, float FallbackRange = 150.f);

	/** Computes a destination point that places Self within (StopAtPercentOfRange * AttackRange)
	 *  of Target, along the line from Target to Self (so we stop just inside range).
	 *  Returns false if inputs are invalid or range <= 0.
	 */
	UFUNCTION(BlueprintPure, Category="Movement|AttackRange")
	static bool ComputeAttackRangeDestination(
		const FVector& SelfLocation2D,
		const FVector& TargetLocation2D, float AttackRange, float StopAtPercentOfRange, FVector& OutDestination);

	/** Returns true if Self is already within AttackRange of Target (optionally scaled by StopAtPercentOfRange). */
	UFUNCTION(BlueprintPure, Category="Movement|AttackRange")
	static bool IsWithinAttackRange(const AActor* SelfActor,
	                                const AActor* TargetActor,
	                                float StopAtPercentOfRange = 1.0f,
	                                float FallbackRange = 150.f);
	
	/** Smoothly rotate Source toward Target. Returns the new rotation and whether we're within ToleranceDeg. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Rotation")
	static void SmoothFaceActorTowardTarget(
		AActor* Source,
		AActor* Target,
		float DeltaSeconds,
		float InterpSpeed,
		bool bYawOnly,
		float ToleranceDeg,
		FRotator& OutNewRotation,
		bool& bWithinTolerance);
};






