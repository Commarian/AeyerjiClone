// File: Source/Aeyerji/Private/Progression/AeyerjiRewardConfigComponent.cpp

#include "Progression/AeyerjiRewardConfigComponent.h"
#include "Progression/AeyerjiXPLibrary.h"

UAeyerjiRewardConfigComponent::UAeyerjiRewardConfigComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false);
}

void UAeyerjiRewardConfigComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!bApplyOnBeginPlay || !GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    if (RewardTuning)
    {
        UAeyerjiXPLibrary::ApplyRewardTuningToActor(GetOwner(), RewardTuning);
    }
}

