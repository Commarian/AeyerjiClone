#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiWorldDirector.generated.h"

/**
 * Thin persistent-map bootstrap actor that triggers startup zone streaming on begin play.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiWorldDirector : public AActor
{
	GENERATED_BODY()

public:
	/** Creates a non-ticking bootstrap actor used only for startup orchestration. */
	AAeyerjiWorldDirector();

protected:
	/** Boots startup flow for the persistent root world. */
	virtual void BeginPlay() override;

	/** Executes startup flow on the next tick so GameState/world-flow systems are initialized first. */
	void ExecuteDeferredStartupFlow();

public:
	/** Explicit startup zone id used when entering gameplay from this persistent map. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName StartZoneId = NAME_None;

	/** If true, startup flow prefers subsystem saved LastZoneId over StartZoneId. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bPreferSavedZone = true;

	/** If true, BeginPlay will call EnterStartupZone(). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bEnterZoneOnBeginPlay = true;

	/** If true, startup goes through GameState world-flow transitions instead of direct EnterStartupZone(). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bUseServerWorldFlow = true;

	/** If true, only authority/standalone instances execute BeginPlay bootstrap logic. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bOnlyRunOnAuthority = true;
};
