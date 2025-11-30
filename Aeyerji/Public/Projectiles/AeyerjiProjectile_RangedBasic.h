// AeyerjiProjectile_RangedBasic.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiProjectile_RangedBasic.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class USceneComponent;
class UStaticMeshComponent;
class UGA_PrimaryRangedBasic;

DECLARE_MULTICAST_DELEGATE_TwoParams(FRangedProjectileImpactSignature, AActor* /*HitActor*/, const FHitResult& /*Hit*/);
DECLARE_MULTICAST_DELEGATE(FRangedProjectileExpiredSignature);

/**
 * Lightweight projectile actor used by GA_PrimaryRangedBasic.
 * Spawns a sphere collider + projectile movement and reports impacts back to the ability.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiProjectile_RangedBasic : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiProjectile_RangedBasic();

	/** Initializes velocity / owner data straight after spawning. */
	void InitializeProjectile(UGA_PrimaryRangedBasic* InAbility, AActor* InIntendedTarget, float InitialSpeed, float StationaryTolerance);

	/** Fired when the projectile overlaps a valid actor (server authoritative). */
	FRangedProjectileImpactSignature OnProjectileImpact;

	/** Fired when the projectile times out or is destroyed for any reason. */
	FRangedProjectileExpiredSignature OnProjectileExpired;

protected:
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneComponent> SpinRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Lifetime in seconds before auto-destroy (0 = infinite). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
	float MaxLifetime = 6.f;

	/** Optional delay prior to allowing the projectile to hit the instigator (prevents immediate self-overlap). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Projectile")
	float SelfCollisionGraceDistance = 120.f;

private:
	/** Ability responsible for spawning this projectile (server only). */
	TWeakObjectPtr<UGA_PrimaryRangedBasic> OwningAbility;

	/** Cached intended target used for aim prediction. */
	TWeakObjectPtr<AActor> IntendedTarget;

	/** Actor that launched the projectile (ignored for collision). */
	TWeakObjectPtr<AActor> SourceActor;

	/** Spawn time for debug logging. */
	double SpawnTimeSeconds = 0.0;

	/** Flag so we only process one impact. */
	bool bImpactProcessed;
	FVector SpawnLocation;
	TArray<FName> CachedSourceTags;

	/** Suppresses world collision for a short grace window when spawning inside geometry. */
	bool bWorldCollisionSuppressed = false;
	FTimerHandle WorldCollisionRestoreHandle;

	void HandleImpact(AActor* OtherActor, const FHitResult& Hit);
	void IgnoreActorAndResumeFlight(AActor* ActorToIgnore, const FHitResult& Hit);
	const AActor* GetSourceActorForTeamCheck() const;
	bool SharesTagWithSource(const AActor* OtherActor) const;
	void ApplyCollisionResponseOverrides() const;
	void SuppressWorldCollisionForGrace(float OverrideDelaySeconds = -1.f);
	void RestoreWorldCollision();

	UFUNCTION()
	void OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	                             AActor* OtherActor,
	                             UPrimitiveComponent* OtherComp,
	                             int32 OtherBodyIndex,
	                             bool bFromSweep,
	                             const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollisionHit(UPrimitiveComponent* HitComponent,
	                    AActor* OtherActor,
	                    UPrimitiveComponent* OtherComp,
	                    FVector NormalImpulse,
	                    const FHitResult& Hit);
};
