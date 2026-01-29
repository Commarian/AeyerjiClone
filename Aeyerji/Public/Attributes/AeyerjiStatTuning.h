// File: Source/Aeyerji/Public/Attributes/AeyerjiStatTuning.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "AeyerjiStatTuning.generated.h"

USTRUCT(BlueprintType)
struct FAeyerjiPrimaryToDerivedTuning
{
    GENERATED_BODY()

    // Primary -> Derived multipliers (editable in a single data asset)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float StrengthToHP          = 2.0f;   // +2 HP per Strength
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float StrengthToArmor       = 1.0f;   // +1 Armor per Strength

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float AgilityToDodgeChance  = 0.1f;   // +0.1 dodge chance per Agility (0..1 range)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float AgilityToAttackSpeed  = 1.0f;   // +1 attacks/sec per Agility

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float IntellectToSpellPower = 1.0f;   // +1 spell power per Intellect
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float IntellectToManaMax    = 2.0f;   // +2 mana per Intellect
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float IntellectToManaRegen  = 0.1f;   // +0.1 mana/sec per Intellect

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float AilmentToDPS          = 0.1f;   // +0.1 DPS per Ailment
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float AilmentToDuration     = 0.05f;  // +0.05 seconds per Ailment

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Tuning") float StrengthToHPRegen     = 0.1f;   // +0.1 HP/sec per Strength
};

USTRUCT(BlueprintType)
struct FAeyerjiArmorMitigationTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Armor") float ArmorK         = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Armor") float ArmorSoftCap   = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Armor") float ArmorTailSlope = 0.00001f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Armor") float ArmorTailCap   = 0.52f;
};

/** Flat data object with multipliers: keep numbers in one place for easy tuning. */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiAttributeTuning : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Tuning")
    FAeyerjiPrimaryToDerivedTuning Rules;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Tuning")
    FAeyerjiArmorMitigationTuning ArmorMitigation;
};

/** Developer settings to pick the default tuning DataAsset */
UCLASS(Config=Game, defaultconfig)
class AEYERJI_API UAeyerjiStatSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UAeyerjiStatSettings();

    // Project Settings -> Aeyerji -> Stats
    virtual FName GetCategoryName() const override { return TEXT("Aeyerji"); }
    virtual FName GetSectionName() const override { return TEXT("Stats"); }

    // Soft reference so designers can swap without code
    UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Stats")
    TSoftObjectPtr<UAeyerjiAttributeTuning> DefaultTuning;

    /** Try to load the tuning asset (can return nullptr). */
    static const UAeyerjiAttributeTuning* Get();
};
