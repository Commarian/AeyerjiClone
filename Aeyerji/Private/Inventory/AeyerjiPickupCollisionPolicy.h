#pragma once

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace Aeyerji::PickupCollision
{
	/**
	 * Prevents pickup presentation components from becoming walkable Pawn obstacles.
	 * Explicit pickup volumes retain Pawn overlap so authoritative auto-pickup still works.
	 */
	inline void EnforceNonBlockingPawnPolicy(
		AActor& PickupActor,
		const UPrimitiveComponent* PawnOverlapComponentA = nullptr,
		const UPrimitiveComponent* PawnOverlapComponentB = nullptr,
		const UPrimitiveComponent* PawnOverlapComponentC = nullptr)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(&PickupActor);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive))
			{
				continue;
			}

			const bool bIsPawnOverlapVolume = Primitive == PawnOverlapComponentA
				|| Primitive == PawnOverlapComponentB
				|| Primitive == PawnOverlapComponentC;
			Primitive->SetCollisionResponseToChannel(ECC_Pawn, bIsPawnOverlapVolume ? ECR_Overlap : ECR_Ignore);
			Primitive->SetCanEverAffectNavigation(false);
		}
	}
}
