#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "AeyerjiWorldStateRegistrationComponent.generated.h"

/**
 * Optional placed-actor helper that registers its owner with the world-state registry on authority.
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiWorldStateRegistrationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiWorldStateRegistrationComponent();

	/** Registers the owning actor with the world-state subsystem when authority begins play. */
	virtual void BeginPlay() override;

	/** Clears transient live-object pointers when the owning actor leaves play. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Returns the key this component will register. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State")
	FAeyerjiWorldStateKey MakeRegistrationKey() const;

protected:
	/** Gameplay tag that identifies this registered actor or fact family. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FGameplayTag StateTag;

	/** Optional designer-authored id. If empty and enabled, the owner actor name is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FName InstanceId = NAME_None;

	/** Optional owner id for character/profile-scoped registrations. Leave empty for global/run/session registrations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FName OwnerId = NAME_None;

	/** Uses the owner name as the instance id when InstanceId is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	bool bUseOwnerNameWhenInstanceIdEmpty = true;

	/** Registers the owner as a live object instead of a simple bool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	bool bRegisterObjectReference = true;

	/** Bool value written when object-reference registration is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State", meta=(EditCondition="!bRegisterObjectReference"))
	bool bPresenceValue = true;

	/** Persistence policy for the registered entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::RuntimeOnly;

	/** Replication policy for the registered entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly;

	/** Lifetime and owner lane for the registered entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global;
};
