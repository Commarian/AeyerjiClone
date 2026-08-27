#include "AbilityAsync_WaitForAbilityFailed.h"
#include "AbilitySystemComponent.h"

UAbilityAsync_WaitForAbilityFailed*
UAbilityAsync_WaitForAbilityFailed::WaitForAbilityFailed(
	UObject* WorldContextObject,
	UAbilitySystemComponent* AbilitySystem,
	FGameplayTag FilterTag)
{
	auto* Task = NewObject<UAbilityAsync_WaitForAbilityFailed>();
	Task->ASC = AbilitySystem;
	Task->SingleFilter = FilterTag;
	Task->RegisterWithGameInstance(WorldContextObject);
	return Task;
}

void UAbilityAsync_WaitForAbilityFailed::Activate()
{
	UAbilitySystemComponent* AbilitySystem = ASC.Get();
	if (!AbilitySystem)
	{
		SetReadyToDestroy();
		return;
	}

	Handle = AbilitySystem->AbilityFailedCallbacks.AddUObject(
				this, &UAbilityAsync_WaitForAbilityFailed::HandleFailure);
}

void UAbilityAsync_WaitForAbilityFailed::SetReadyToDestroy()
{
	if (UAbilitySystemComponent* AbilitySystem = ASC.Get(); AbilitySystem && Handle.IsValid())
	{
		AbilitySystem->AbilityFailedCallbacks.Remove(Handle);
	}

	Handle.Reset();
	ASC.Reset();
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
