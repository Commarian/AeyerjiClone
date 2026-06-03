#include "Components/AeyerjiWorldStateRegistrationComponent.h"

#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "GameFramework/Actor.h"

UAeyerjiWorldStateRegistrationComponent::UAeyerjiWorldStateRegistrationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAeyerjiWorldStateRegistrationComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(Owner);
	if (!WorldStateSubsystem)
	{
		return;
	}

	const FAeyerjiWorldStateKey Key = MakeRegistrationKey();
	if (!Key.IsValid())
	{
		return;
	}

	if (bRegisterObjectReference)
	{
		WorldStateSubsystem->RegisterLiveObject(Key, Owner, Persistence, Replication, Scope);
	}
	else
	{
		WorldStateSubsystem->SetValue(Key, FAeyerjiWorldStateValue::FromBool(bPresenceValue), Persistence, Replication, Scope);
	}
}

void UAeyerjiWorldStateRegistrationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(Owner))
		{
			WorldStateSubsystem->UnregisterLiveObject(MakeRegistrationKey(), Owner);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FAeyerjiWorldStateKey UAeyerjiWorldStateRegistrationComponent::MakeRegistrationKey() const
{
	FName ResolvedInstanceId = InstanceId;
	const AActor* Owner = GetOwner();
	if (ResolvedInstanceId.IsNone() && bUseOwnerNameWhenInstanceIdEmpty && Owner)
	{
		ResolvedInstanceId = Owner->GetFName();
	}

	return FAeyerjiWorldStateKey(StateTag, ResolvedInstanceId, OwnerId);
}
