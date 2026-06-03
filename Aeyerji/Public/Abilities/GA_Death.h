#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GA_Death.generated.h"

/**
 * Passive finalization ability triggered by State.Dead.
 * Native death shutdown happens in ApplyDeathState; this ability only handles delayed cleanup.
 */
UCLASS()
class AEYERJI_API UGA_Death : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Death();

	/** Legacy montage reference retained for Blueprint-driven death presentation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	TObjectPtr<UAnimMontage> DeathMontage;

	/** Delay (seconds) before the dead pawn is respawned or destroyed. */
	UPROPERTY(EditDefaultsOnly, Category = "Death")
	float RespawnDelay = 2.f;      

	/** If true, bypass GameMode restart and manually spawn/possess a pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Respawn")
	bool bUseCustomRespawn = false;

	/** Pawn class to spawn when using custom respawn. Defaults to the class of the dead pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Respawn", meta = (EditCondition = "bUseCustomRespawn"))
	TSubclassOf<APawn> RespawnPawnClassOverride;

	/** Optional PlayerStart tag for custom respawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death|Respawn", meta = (EditCondition = "bUseCustomRespawn"))
	FName RespawnPlayerStartTag = NAME_None;

protected:
	//~UGameplayAbility interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Server-only callback fired by timer. */
	UFUNCTION()
	void Server_FinishDeath();
private:
	FTimerHandle RespawnHandle;
	// Per-activation guard to avoid double finalization from multiple timers.
	bool bDeathFinalized = false;
};
