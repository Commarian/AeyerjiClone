#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_Stun.generated.h"

/**
 * Generic stun GameplayEffect.
 *
 * Duration is driven by SetByCaller.Stun.Duration.
 * While active, this GE grants State.CrowdControl.Stunned to the target.
 *
 * The actual enemy reaction must be handled by AI/character code:
 * - stop movement
 * - block attacks
 * - resume when the tag is removed
 */
UCLASS()
class AEYERJI_API UGE_Stun : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Stun(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};