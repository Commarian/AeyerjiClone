#pragma once

#include "CoreMinimal.h"

#include "AeyerjiCombatTextTypes.generated.h"

/** Local presentation setting for floating combat text. */
UENUM(BlueprintType)
enum class EAeyerjiCombatTextMode : uint8
{
	Off,
	ImportantOnly,
	All
};

/** High-level combat result shown by floating combat text. */
UENUM(BlueprintType)
enum class EAeyerjiCombatTextResultType : uint8
{
	Damage,
	Critical,
	Dodged,
	Staggered,
	Killing
};
