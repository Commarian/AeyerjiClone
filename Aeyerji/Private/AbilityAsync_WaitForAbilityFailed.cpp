#include "AbilityAsync_WaitForAbilityFailed.h"
#include "AbilitySystemComponent.h"

UAbilityAsync_WaitForAbilityFailed*
UAbilityAsync_WaitForAbilityFailed::WaitForAbilityFailed(
	UObject* WorldContextObject,
	UAbilitySystemComponent* AbilitySystem,
	FGameplayTag FilterTag)
{
	auto* Task = NewObject<UAbilityAsync_WaitForAbilityFailed>();
	Task->ASC         = AbilitySystem;
	Task->SingleFilter = FilterTag;
	return Task;
}

void UAbilityAsync_WaitForAbilityFailed::Activate()
{
	if (!ASC) { SetReadyToDestroy(); return; }

	Handle = ASC->AbilityFailedCallbacks.AddUObject(
				this, &UAbilityAsync_WaitForAbilityFailed::HandleFailure);
}

void UAbilityAsync_WaitForAbilityFailed::SetReadyToDestroy()
{
    if (ASC && Handle.IsValid())
    {
        ASC->AbilityFailedCallbacks.Remove(Handle);
    }
    Super::SetReadyToDestroy();
}

void UAbilityAsync_WaitForAbilityFailed::HandleFailure(
		const UGameplayAbility* GA,
		const FGameplayTagContainer& Tags)
{
	if (!SingleFilter.IsValid() || Tags.HasTagExact(SingleFilter))
	{
		OnFailed.Broadcast(const_cast<UGameplayAbility*>(GA), Tags);
	}
}
