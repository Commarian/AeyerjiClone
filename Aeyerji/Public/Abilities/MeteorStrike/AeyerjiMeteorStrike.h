#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiMeteorStrike.generated.h"

UENUM(BlueprintType)
enum class EAeyerjiMeteorPhase : uint8
{
	Inactive,
	Falling,
	Impacted,
	Cancelled
};

/** Single replicated snapshot. Clients derive the fall position from server time, so no movement replicates. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiMeteorState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EAeyerjiMeteorPhase Phase = EAeyerjiMeteorPhase::Inactive;

	UPROPERTY(BlueprintReadOnly)
	FVector ImpactLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float SpawnHeight = 1200.f;

	UPROPERTY(BlueprintReadOnly)
	float FallDuration = 1.f;

	UPROPERTY(BlueprintReadOnly)
	double FallStartServerTime = 0.0;
};

DECLARE_MULTICAST_DELEGATE(FOnAeyerjiMeteorResolved);

/**
 * Server-authoritative falling meteor visual.
 *
 * This actor never applies damage. The owning ability (UGA_AGMeteorStrike) applies GAS damage
 * at impact through the shared targeted-effect base, so impact cannot damage twice.
 * At impact the server spawns FractureActorClass (for example a Blueprint holding the
 * Chaos Geometry Collection fracture) and replicates the Impacted phase for presentation.
 */
UCLASS(Blueprintable, NotPlaceable)
class AEYERJI_API AAeyerjiMeteorStrike : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiMeteorStrike();

	/** Supported fall window in seconds. Falls outside it are clamped so visuals stay near the ability impact delay. */
	static constexpr float MinFallDuration = 0.2f;
	static constexpr float MaxFallDuration = 3.0f;
	/** Fallback spawn height when the ability row has no valid tunable. */
	static constexpr float DefaultSpawnHeight = 1200.f;
	static constexpr float MinSpawnHeight = 200.f;
	static constexpr float MaxSpawnHeight = 4000.f;
	/** Server failsafe slack after the fall ends, in case the owning ability never resolves the impact. */
	static constexpr float ImpactFailsafeSlack = 0.25f;

	static float ClampFallDuration(float FallDuration);
	static float ClampSpawnHeight(float SpawnHeight);
	static FVector ComputeSpawnLocation(const FVector& ImpactLocation, float SpawnHeight);
	static float ComputeFallProgress(double FallStartServerTime, double NowServerTime, float FallDuration);
	static FVector ComputeFallLocation(const FVector& ImpactLocation, float SpawnHeight, float FallProgress);

	/** Server-only: begin the fall above ImpactLocation. Snaps the impact point to the ground. */
	bool InitializeMeteor(const FVector& ImpactLocation, float FallDuration, float SpawnHeight);
	/** Server-only: land now and present the fracture. Called by the ability at damage time. */
	void ResolveImpact();
	/** Server-only: dismiss without impact because the owning ability ended before damage. */
	void CancelMeteor();
	bool IsFalling() const { return State.Phase == EAeyerjiMeteorPhase::Falling; }

	FOnAeyerjiMeteorResolved OnResolved;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Actor spawned by the server at impact to present destruction.
	// Point this at a Blueprint holding the Chaos fracture/VFX from Content/Abilities/MeteorStrike.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Meteor")
	TSubclassOf<AActor> FractureActorClass;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Replicated fall/impact snapshot. Clients present; only the server mutates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_State, Category="Meteor")
	FAeyerjiMeteorState State;

	/** Cosmetic hook on all peers, including late relevance. Never apply damage here. */
	UFUNCTION(BlueprintImplementableEvent, Category="Meteor|Visuals")
	void BP_OnMeteorStateChanged(const FAeyerjiMeteorState& NewState);

	/** Cosmetic impact hook on all peers. The Chaos fracture/VFX belongs here or in FractureActorClass. */
	UFUNCTION(BlueprintImplementableEvent, Category="Meteor|Visuals")
	void BP_OnMeteorImpacted(const FAeyerjiMeteorState& NewState);

	/** Cosmetic fall progress, 0 at spawn and 1 at landing. */
	UFUNCTION(BlueprintImplementableEvent, Category="Meteor|Visuals")
	void BP_UpdateFall(float FallProgress);

private:
	void FinishMeteor(bool bImpacted);
	void RefreshPresentation();
	UFUNCTION()
	void OnRep_State();

	// Fallback falling mesh, hidden on impact. Leave it unmodified when a Blueprint child provides its own visual.
	UPROPERTY(VisibleAnywhere, Category="Meteor|Visuals")
	TObjectPtr<UStaticMeshComponent> FallMesh;

	FTimerHandle ImpactTimer;
	bool bResolvingImpact = false;
	EAeyerjiMeteorPhase LastPresentedPhase = EAeyerjiMeteorPhase::Inactive;
};
