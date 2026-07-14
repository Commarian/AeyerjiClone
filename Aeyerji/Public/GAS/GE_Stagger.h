#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"

#include "GE_Stagger.generated.h"

/** Short crowd-control effect applied when a target's poise is broken. */
UCLASS()
class AEYERJI_API UGE_Stagger : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_Stagger(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
