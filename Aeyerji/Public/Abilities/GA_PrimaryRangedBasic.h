// GA_PrimaryRangedBasic.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameplayTagContainer.h"
#include "GA_PrimaryRangedBasic.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
class AAeyerjiProjectile_RangedBasic;
class UGameplayEffect;
struct FGameplayAbilityTargetDataHandle;
struct FGameplayAbilityEventData;
struct FGameplayEventData;

/**
 * Primary ranged basic attack (projectile based).
 * - Plays an optional montage using AttackSpeed as the play-rate reference.
 * - Spawns a projectile actor that reports impacts back to the ability for damage handling.
 * - Applies damage through a configurable GameplayEffect using SetByCaller magnitude.
 */
UCLASS()
class AEYERJI_API UGA_PrimaryRangedBasic : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_PrimaryRangedBasic();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

	virtual void CancelAbility(const FGameplayAbilitySpecHandle Handle,
	                           const FGameplayAbilityActorInfo* ActorInfo,
	                           const FGameplayAbilityActivationInfo ActivationInfo,
	                           bool bReplicateCancelAbility) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
	                        const FGameplayAbilityActorInfo* ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool bReplicateEndAbility,
	                        bool bWasCancelled) override;

	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	                           const FGameplayAbilityActorInfo* ActorInfo,
	                           const FGameplayAbilityActivationInfo ActivationInfo) const override;

	/** Projectile actor spawned when the ability fires. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	TSubclassOf<AAeyerjiProjectile_RangedBasic> ProjectileClass;

	/** Optional socket to use as spawn location (falls back to actor location). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	FName MuzzleSocketName = TEXT("Muzzle");

	/** Forward offset (cm) applied when no socket is available. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	float ForwardSpawnOffset = 50.f;

	/** Vertical offset (cm) applied when no socket is available. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	float VerticalSpawnOffset = 50.f;

	/** Baseline attacks-per-second that maps to montage play-rate 1.0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged|Montage", meta=(ClampMin="0.01"))
	float BaselineAttackSpeed = 1.f;

	/** Optional montage to play while firing (set per-activation via SelectAttackMontage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged|Montage")
	TSoftObjectPtr<UAnimMontage> AttackMontage;

	/** Gameplay Effect used to apply damage on impact. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
	TSoftClassPtr<UGameplayEffect> DamageEffectClass;

	/** SetByCaller tag filled with AttackDamage * DamageScalar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
	FGameplayTag DamageSetByCallerTag;

	/** Scalar applied to the AttackDamage attribute before writing SetByCaller. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
	float DamageScalar = 1.f;

	/** Minimum cooldown duration when computing SetByCaller duration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cooldown")
	float MinCooldownDuration = 0.05f;

	/** Broadcasts Event.PrimaryAttack.Completed when the shot resolves (server only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Events")
	bool bSendCompletionGameplayEvent = true;

	/** Optional delay (seconds) before the projectile is spawned after activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	float SpawnDelaySeconds = 0.f;

	/** Fallback projectile speed if the attribute is missing / zero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	float ProjectileSpeedFallback = 1200.f;

	/** Attribute sample tolerance for stationary targets when predicting lead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ranged")
	float StationaryTargetSpeedTolerance = 50.f;

	/** Blueprint hook: customize montage per activation. */
	UFUNCTION(BlueprintNativeEvent, Category="Ranged|Montage")
	UAnimMontage* SelectAttackMontage(const FGameplayAbilityActorInfo& ActorInfo) const;
	virtual UAnimMontage* SelectAttackMontage_Implementation(const FGameplayAbilityActorInfo& ActorInfo) const;

	/** Blueprint hook triggered on the server after damage is applied. */
	UFUNCTION(BlueprintImplementableEvent, Category="Ranged|Server", DisplayName="HandleRangedDamage")
	void BP_HandleRangedDamage(AActor* HitActor, const FGameplayAbilityTargetDataHandle& TargetData);

	/** Cosmetic hook fired on autonomous proxies/predicting clients. */
	UFUNCTION(BlueprintImplementableEvent, Category="Ranged|Cosmetic", DisplayName="HandlePredictedRangedImpact")
	void BP_HandlePredictedImpact(AActor* HitActor, const FGameplayAbilityTargetDataHandle& TargetData);

private:
	/** Active montage task for the fire animation. */
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** True once we have dispatched the completion gameplay event. */
	bool bCompletionBroadcasted;

	/** Cached pointer to the currently spawned projectile (authority only). */
	TWeakObjectPtr<AAeyerjiProjectile_RangedBasic> ActiveProjectile;

	/** Handle to defer spawn when SpawnDelaySeconds > 0. */
	FTimerHandle SpawnDelayHandle;

	/** Whether CachedTriggerEventData contains data from activation. */
	bool bHasCachedTriggerEventData;

	/** Cached trigger payload (used when spawning after a delay). */
	FGameplayEventData CachedTriggerEventData;

	/* --- Activation helpers --- */
	void StartMontage(float AttackSpeed);
	void ClearMontageTask();
	void ScheduleProjectileSpawn(float ProjectileSpeed);
	void SpawnProjectileNow(float ProjectileSpeed);

	/* --- Projectile callbacks --- */
	void HandleProjectileImpact(AActor* HitActor, const FHitResult& Hit);
	void HandleProjectileExpired();

	/* --- Damage helpers --- */
	void ApplyDamageToTarget(const FGameplayAbilityTargetDataHandle& TargetData);
	bool BuildDamageSpec(FGameplayEffectSpecHandle& OutSpecHandle) const;

	/* --- Utility --- */
	FTransform ComputeMuzzleTransform(const FGameplayAbilityActorInfo* ActorInfo) const;
	float ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const;
	float ResolveProjectileSpeed(const FGameplayAbilityActorInfo* ActorInfo) const;
	AActor* ResolveTargetActor(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* TriggerEventData) const;

	bool ShouldProcessServerLogic() const;
	bool IsLocallyPredicting() const;
	void BroadcastPrimaryAttackComplete();

	UFUNCTION()
	void OnMontageCompleted();

	UFUNCTION()
	void OnMontageInterrupted();

	UFUNCTION()
	void OnMontageCancelled();

	void HandleMontageFinished(bool bWasCancelled);
};
