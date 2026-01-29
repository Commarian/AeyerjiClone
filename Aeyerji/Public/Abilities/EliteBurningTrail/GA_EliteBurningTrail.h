// Passive elite affix ability that leaves damaging ground patches behind the owner.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GameplayTagContainer.h"
#include "GA_EliteBurningTrail.generated.h"

class AEliteBurningTrailPatch;
class UDA_EliteBurningTrail;
class UNiagaraSystem;
class UGameplayEffect;

/**
 * Grants elites a burning footprint trail that spawns ground patches as they move.
 * Server-only, instanced per actor, activates automatically when granted.
 */
UCLASS()
class AEYERJI_API UGA_EliteBurningTrail : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_EliteBurningTrail();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
	                        const FGameplayAbilityActorInfo* ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool bReplicateEndAbility,
	                        bool bWasCancelled) override;

private:
	void HandleFootstepTick();

	bool CanSpawnPatch(const APawn* Pawn) const;

	bool TrySpawnPatch(APawn* Pawn, const FGameplayAbilityActorInfo* ActorInfo);

	void ClearFootstepTimer();

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Config", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UDA_EliteBurningTrail> BurningTrailConfig = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Patch", meta=(AllowPrivateAccess="true"))
	TSubclassOf<AEliteBurningTrailPatch> PatchClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Patch", meta=(AllowPrivateAccess="true"))
	TSubclassOf<UGameplayEffect> DotEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Patch", meta=(AllowPrivateAccess="true"))
	FGameplayTag DamageSetByCallerTag;

	UPROPERTY()
	TArray<TWeakObjectPtr<AEliteBurningTrailPatch>> ActivePatches;

	UPROPERTY()
	FTimerHandle FootstepTimerHandle;

	UPROPERTY()
	FVector LastPatchLocation = FVector::ZeroVector;
};
