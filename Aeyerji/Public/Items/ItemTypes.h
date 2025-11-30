// ItemTypes.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"

#include "ItemTypes.generated.h"

class UGameplayEffect;
class UGameplayAbility;
class UAnimInstance;
class AAeyerjiWeaponActor;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EItemRarity : uint8
{
	Common            UMETA(DisplayName = "Common"),
	Uncommon          UMETA(DisplayName = "Uncommon"),
	Rare              UMETA(DisplayName = "Rare"),
	Epic              UMETA(DisplayName = "Epic"),
	Pure              UMETA(DisplayName = "Pure"),
	Legendary         UMETA(DisplayName = "Legendary"),
	PerfectLegendary  UMETA(DisplayName = "Perfect Legendary"),
	Celestial         UMETA(DisplayName = "Celestial")
};

UENUM(BlueprintType)
enum class EEquipmentSlot : uint8
{
	Offense,
	Defense,
	Magic
};

UENUM(BlueprintType)
enum class EItemCategory : uint8
{
	Offense,
	Defense,
	Magic
};

UENUM(BlueprintType)
enum class EItemModOp : uint8
{
	Additive,
	Multiplicative,
	Override
};

USTRUCT(BlueprintType)
struct AEYERJI_API FItemStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FGameplayAttribute Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EItemModOp Op = EItemModOp::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float Magnitude = 0.f;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FItemGrantedEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSubclassOf<UGameplayEffect> EffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	float EffectLevel = 1.f;

	/** Optional set of tags to add to the gameplay effect spec when applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FGameplayTagContainer ApplicationTags;

	bool IsValid() const { return EffectClass != nullptr; }
};

USTRUCT(BlueprintType)
struct AEYERJI_API FItemGrantedAbility
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSubclassOf<UGameplayAbility> AbilityClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 AbilityLevel = 1;

	/** Optional input binding id to assign when granting the ability (-1 leaves input untouched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 InputID = INDEX_NONE;

	/** Optional gameplay tags to grant to the ability owner while this ability is equipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FGameplayTagContainer OwnedTags;

	bool IsValid() const { return AbilityClass != nullptr; }
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAffixTier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	float MinRoll = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	float MaxRoll = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	int32 MinItemLevel = 1;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAttributeRoll
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	FGameplayAttribute Attribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	float Scale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	EItemModOp Op = EItemModOp::Additive;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FRolledAffix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	FName AffixId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	TArray<FItemStatModifier> FinalModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	TArray<FItemGrantedAbility> GrantedAbilities;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FWeaponMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bOverrideMaxWalkSpeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (EditCondition = "bOverrideMaxWalkSpeed", ClampMin = "0.0"))
	float MaxWalkSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	bool bOverrideRotationSettings = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (EditCondition = "bOverrideRotationSettings"))
	bool bOrientRotationToMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (EditCondition = "bOverrideRotationSettings"))
	bool bUseControllerDesiredRotation = false;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FWeaponEquipmentConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName EquippedSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FWeaponMovementSettings Movement;

	bool HasValidSocket() const { return EquippedSocket != NAME_None; }
};

USTRUCT(BlueprintType)
struct AEYERJI_API FEquipmentSlotFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affix")
	TArray<EEquipmentSlot> AllowedSlots;
};

/**
 * Cosmetic parameters used when a pickup grants this item to a player.
 * Applied on the character-side via UAeyerjiPickupFXComponent instead of spawning world meshes.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiPickupVisualConfig
{
	GENERATED_BODY()

	/** Non-looping Niagara system to play on the character at the moment the pickup is granted from the world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	TObjectPtr<UNiagaraSystem> PickupGrantedSystem = nullptr;

	/** Optional Niagara burst played when the item becomes equipped (leave null for items with no equip FX). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	TObjectPtr<UNiagaraSystem> InventoryGrantedSystem = nullptr;

	/** Socket used for both systems when no override is provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	FName AttachSocket = NAME_None;

	/** Override socket for InventoryGrantedSystem (falls back to AttachSocket when unset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	FName SecondaryAttachSocket = NAME_None;

	/** Local offset applied when spawning Niagara FX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	FVector SpawnOffset = FVector::ZeroVector;

	/** Optional color parameter pushed into the spawned systems. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	FName ColorParameter = TEXT("PickupColor");

	/** Color value written to ColorParameter when valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|FX")
	FLinearColor FXColor = FLinearColor::White;

	/** When true we temporarily force the owner's outline highlight on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Outline")
	bool bPulseOutline = false;

	/** Duration (seconds) of the forced outline pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Outline", meta = (EditCondition = "bPulseOutline", ClampMin = "0.0"))
	float OutlinePulseDuration = 0.35f;

	/** Optional fade delay before disabling the highlight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Outline", meta = (EditCondition = "bPulseOutline", ClampMin = "0.0"))
	float OutlinePulseFadeTime = 0.15f;

	/** Optional stencil override while the pulse is active (-1 keeps the current stencil). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup|Outline", meta = (EditCondition = "bPulseOutline", ClampMin = "-1", ClampMax = "255"))
	int32 OutlineStencilOverride = -1;

	/** Helper: true when any pickup FX (visual burst or outline pulse) is configured. */
	bool HasPickupVisuals() const
	{
		return PickupGrantedSystem != nullptr
			|| (bPulseOutline && OutlinePulseDuration > 0.f);
	}

	/** Helper: true when any equip FX (visual burst or outline pulse) is configured. */
	bool HasEquipVisuals() const
	{
		return InventoryGrantedSystem != nullptr
			|| (bPulseOutline && OutlinePulseDuration > 0.f);
	}

	/** Helper to detect if any FX entries were configured. */
	bool HasAnyVisuals() const { return HasPickupVisuals() || HasEquipVisuals(); }
};

/**
 * Configurable equip synergy color per required stack count.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FItemEquipSynergyColor
{
	GENERATED_BODY()

	/** How many copies of this item must be equipped to use this color (2-5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Synergy", meta = (ClampMin = "1", ClampMax = "5"))
	int32 StackCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Synergy")
	FLinearColor Color = FLinearColor::White;
};
