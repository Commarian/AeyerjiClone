// Ground patch actor spawned by the elite Burning Trail affix.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/EliteBurningTrail/DA_EliteBurningTrail.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "EliteBurningTrailPatch.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class UGameplayAbility;
class UNiagaraComponent;
class USceneComponent;
class USphereComponent;

UCLASS()
class AEYERJI_API AEliteBurningTrailPatch : public AActor
{
	GENERATED_BODY()

public:
	AEliteBurningTrailPatch();

	virtual void BeginPlay() override;

	/** Runtime setup from the spawning ability. */
	void InitializePatch(const FAGEliteBurningTrailTuning& InTuning,
	                     UAbilitySystemComponent* InInstigatorASC,
	                     const FGameplayTag& InDamageSetByCallerTag,
	                     const FGameplayTag& InDamageTypeTag,
	                     TSubclassOf<UGameplayEffect> InDotEffectClass,
	                     UGameplayAbility* InSourceAbility);

	/** Damage hook called on overlap; native code applies a GAS effect unless BP overrides. */
	UFUNCTION(BlueprintNativeEvent, Category="BurningTrail|Damage")
	bool ApplyPatchDamage(AActor* Target, float InDamagePerSecond, UGameplayAbility* InSourceAbility);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USphereComponent> DamageArea;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UNiagaraComponent> VisualFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float LifetimeSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float DamagePerSecond = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float PatchRadius = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float GroundTraceDistance = 5000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="BurningTrail")
	float GroundOffsetZ = 425.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Effects")
	TSubclassOf<UGameplayEffect> DotEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Effects")
	FGameplayTag DamageSetByCallerTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail|Effects")
	FGameplayTag DamageTypeTag;

	/** Source ASC used for team checks and BP-driven damage. */
	UPROPERTY()
	TWeakObjectPtr<UAbilitySystemComponent> InstigatorASC;

	/** Owning ability that spawned this patch. */
	UPROPERTY()
	TWeakObjectPtr<UGameplayAbility> SourceAbility;

protected:
	UFUNCTION()
	void HandleDamageAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	                                  AActor* OtherActor,
	                                  UPrimitiveComponent* OtherComp,
	                                  int32 OtherBodyIndex,
	                                  bool bFromSweep,
	                                  const FHitResult& SweepResult);

	void RefreshCollisionRadius();

	/** Single trace downwards to snap the patch onto a static mesh ground surface. */
	void SnapPatchToGround();

	/** Override to customize how much the patch should be lifted above the ground hit location. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintPure, Category="BurningTrail|Ground")
	float GetGroundOffsetZ(const FHitResult& GroundHit) const;

	/** Returns the ground hit used for snapping (useful for BP-driven placement). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="BurningTrail|Ground")
	bool GetGroundSnapHitResult(FHitResult& OutGroundHit) const;

	/** Activates the Niagara FX after the patch has snapped to the ground. */
	void TryActivateVFX();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ActivateVFX(FVector ReplicatedLocation);

	bool bVFXActivated = false;
};
