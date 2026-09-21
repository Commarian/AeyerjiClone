#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GA_EnemyBombardment.generated.h"

class AAeyerjiBombardment;
class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;

/** Enemy primary attack that locks a ground target, warns all clients, then resolves a physical blast. */
UCLASS()
class AEYERJI_API UGA_EnemyBombardment : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_EnemyBombardment();

protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo) const override;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	/** Optional Blueprint child supplies warning/impact VFX. Native actor provides a visible fallback ring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier")
	TSubclassOf<AAeyerjiBombardment> BombardmentClass;

	/** Warning duration in seconds, independent of attack speed. Maximum 2s stays below the shared AI's 2.5s watchdog. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier", meta=(ClampMin="0.75", ClampMax="2.0", Units="s"))
	float WarningSeconds = 1.4f;

	/** Ground radius in cm; targets are tested again at impact so leaving the warning avoids the blast. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier", meta=(ClampMin="50", ClampMax="600", Units="cm"))
	float BlastRadius = 250.f;

	/** Minimum time between warning starts. Ignores attack-speed scaling to preserve recovery at higher levels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier", meta=(ClampMin="3", ClampMax="30", Units="s"))
	float AttackIntervalSeconds = 5.f;

	/** Multiplier on the authoritative AttackDamage captured at warning start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier", meta=(ClampMin="0.0", ClampMax="10.0"))
	float DamageScalar = 1.f;

	/** Optional casting montage, played through GAS on the server and replicated to clients. Damage uses the warning timer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardier|Animation")
	TObjectPtr<UAnimMontage> CastMontage;

private:
	UFUNCTION()
	void HandleMontageInterrupted();
	void HandleBombardmentResolved(bool bCancelled);
	TWeakObjectPtr<AAeyerjiBombardment> ActiveBombardment;
	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;
	FGameplayTagContainer BombardmentCooldownTags;
	bool bEndingBombardment = false;
};
