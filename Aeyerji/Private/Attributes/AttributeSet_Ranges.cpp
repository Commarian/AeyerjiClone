// Fill out your copyright notice in the Description page of Project Settings.


#include "Attributes/AttributeSet_Ranges.h"
//  AttributeSet_Ranges.cpp
#include "Net/UnrealNetwork.h"          //  DOREPLIFETIME_…
#include "GameplayEffectExtension.h"    //  GAMEPLAYATTRIBUTE_REPNOTIFY

/* ---------- RepNotify ---------- */
void UAttributeSet_Ranges::OnRep_BlinkRange(const FGameplayAttributeData& OldBlinkRange)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAttributeSet_Ranges, BlinkRange, OldBlinkRange);
}

/* ---------- Replication ---------- */
void UAttributeSet_Ranges::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	//  Always replicate, always call OnRep even if the value didn’t change locally (prediction rollback)
	DOREPLIFETIME_CONDITION_NOTIFY(UAttributeSet_Ranges, BlinkRange, COND_None, REPNOTIFY_Always);
}
