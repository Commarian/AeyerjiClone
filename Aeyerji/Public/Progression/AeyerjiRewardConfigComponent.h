// File: Source/Aeyerji/Public/Progression/AeyerjiRewardConfigComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Progression/AeyerjiRewardTuning.h"
#include "AeyerjiRewardConfigComponent.generated.h"

/**
 * Optional drop-in component to bind a RewardTuning asset to an actor and
 * set its Base Reward XP on BeginPlay (server-side).
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiRewardConfigComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAeyerjiRewardConfigComponent();

    /** Asset providing reward values. Supports hierarchy via Parent pointer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Rewards")
    TObjectPtr<const UAeyerjiRewardTuning> RewardTuning = nullptr;

    /** If true, applies BaseRewardXP from tuning at BeginPlay (server only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Rewards")
    bool bApplyOnBeginPlay = true;

protected:
    virtual void BeginPlay() override;
};

