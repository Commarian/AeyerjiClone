// File: Source/Aeyerji/Private/Attributes/AeyerjiRewardAttributeSet.cpp

#include "Attributes/AeyerjiRewardAttributeSet.h"
#include "Net/UnrealNetwork.h"

UAeyerjiRewardAttributeSet::UAeyerjiRewardAttributeSet()
{
    InitXPRewardBase(0.f);
}

void UAeyerjiRewardAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiRewardAttributeSet, XPRewardBase, COND_None, REPNOTIFY_Always);
}

