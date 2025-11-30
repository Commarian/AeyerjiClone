// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AeyerjiAbilityTypes.generated.h"

/*-------------------------- click-flow categories ---------------------------*/
/**
 * @enum EAeyerjiTargetMode
 *
 * Represents the targeting mode for abilities in the system. Each value
 * defines a specific interaction or targeting mechanism for the ability.
 * Enum values are designed to remain stable over time for compatibility.
 *
 * @note Additional target modes may be added in the future without affecting
 *       the existing enum values.
 *
 * @var Instant
 * Represents an ability that fires immediately upon activation or targets the
 * user themselves.
 *
 * @var GroundLocation
 * Represents an ability that requires a ground point to be selected on the map.
 *
 * @var EnemyActor
 * Represents an ability that targets an enemy actor through hover selection and click.
 *
 * @var FriendlyActor
 * Represents an ability that targets a friendly actor, often for healing or buffing purposes.
 */
UENUM(BlueprintType)
enum class EAeyerjiTargetMode : uint8
{
	// fires immediately
	Instant         UMETA(DisplayName = "Instant / Self"),
	// 2nd click on map
	GroundLocation  UMETA(DisplayName = "Ground Point"),
	// hover-select & click
	EnemyActor      UMETA(DisplayName = "Enemy Actor"),
	// e.g. heals / buffs same as EnemyActor
	FriendlyActor   UMETA(DisplayName = "Friendly Actor"),          
	/* add more later (Cone, Area, Etc) → old enum values stay stable */
};
