
#include "Attributes/Abdominoplasty.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"     //  GAMEPLAYATTRIBUTE_REPNOTIFY

/* ---------------- RepNotify helpers ----------------------------------------- */
#define IMPLEMENT_REPNOTIFY(AttrName)                                                \
void UAbdominoplasty::OnRep_##AttrName(                             \
const FGameplayAttributeData& Old)                                           \
{                                                                                \
GAMEPLAYATTRIBUTE_REPNOTIFY(UAbdominoplasty, AttrName, Old);    \
}

IMPLEMENT_REPNOTIFY(MaxRange)
IMPLEMENT_REPNOTIFY(InstantDamage)
IMPLEMENT_REPNOTIFY(NoStacks)
IMPLEMENT_REPNOTIFY(LandingRadius)
IMPLEMENT_REPNOTIFY(ChaosChance)

#undef IMPLEMENT_REPNOTIFY

/* ---------------- Replication list ------------------------------------------ */
void UAbdominoplasty::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(
		UAbdominoplasty, MaxRange,      COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UAbdominoplasty, InstantDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UAbdominoplasty, NoStacks,      COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UAbdominoplasty, LandingRadius, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(
		UAbdominoplasty, ChaosChance,   COND_None, REPNOTIFY_Always);
}
