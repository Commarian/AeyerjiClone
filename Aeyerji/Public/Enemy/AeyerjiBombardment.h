#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "AeyerjiBombardment.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EAeyerjiBombardmentPhase : uint8
{
	Inactive,
	Warning,
	Impacted,
	Cancelled
};

/** A single replicated snapshot prevents radius, deadline and phase from arriving separately. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiBombardmentState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EAeyerjiBombardmentPhase Phase = EAeyerjiBombardmentPhase::Inactive;
	UPROPERTY(BlueprintReadOnly)
	FVector Center = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	float Radius = 250.f;
	UPROPERTY(BlueprintReadOnly)
	float WindupSeconds = 1.4f;
	UPROPERTY(BlueprintReadOnly)
	double ImpactServerTime = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAeyerjiBombardmentResolved, bool /*bCancelled*/);

/** Fixed ground warning; authority alone resolves occupants and submits GAS damage at impact. */
UCLASS(Blueprintable, NotPlaceable)
class AEYERJI_API AAeyerjiBombardment : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiBombardment();

	/** Initialize once on authority. The center is fixed for the entire warning. Returns false if the world cap is full. */
	bool InitializeBombardment(AActor* Source, AActor* AimTarget, const FVector& Center,
		float Radius, float WindupSeconds, const FGameplayEffectSpecHandle& DamageSpec);
	void CancelBombardment();
	bool IsPending() const { return State.Phase == EAeyerjiBombardmentPhase::Warning; }
	static bool CanStartBombardment(UWorld* World);
	static bool IsInsideBlast(const FVector& Center, const FVector& Point, float Radius, float HalfHeight);
	static bool IsDamageTarget(const AActor* Source, AActor* Candidate);
	FOnAeyerjiBombardmentResolved OnResolved;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** World-wide warning cap is two in this first version, across all Bombardier ability subclasses. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_State, Category="Bombardment")
	FAeyerjiBombardmentState State;

	/** Vertical half-height in cm. Actor centers inside this cylinder can be damaged; upper/lower floors are excluded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardment", meta=(ClampMin="50", ClampMax="500", Units="cm"))
	float BlastHalfHeight = 180.f;

	/** Enable the native white segmented warning and shrinking countdown ring. Works without authored effects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardment|Visuals")
	bool bShowNativeWarning = true;

	/** Optional material for both native rings; an emissive warning material makes them easier to read. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Bombardment|Visuals")
	TObjectPtr<UMaterialInterface> WarningMaterial;

	/** Cosmetic hook on listen server and clients, including late relevance. Never apply damage here. */
	UFUNCTION(BlueprintImplementableEvent, Category="Bombardment|Visuals")
	void BP_OnBombardmentStateChanged(const FAeyerjiBombardmentState& NewState);

	/** Cosmetic countdown, 1 at start and 0 at impact, derived from replicated server time. */
	UFUNCTION(BlueprintImplementableEvent, Category="Bombardment|Visuals")
	void BP_UpdateWarning(float RemainingFraction);

private:
	friend class FAeyerjiBombardmentGameplayTest;
	void ResolveImpact();
	void FinishBombardment(bool bCancelled, int32 Candidates, int32 Applications);
	bool IsSourceReady() const;
	void RefreshPresentation();
	UFUNCTION()
	void OnRep_State();
	UFUNCTION()
	void HandleSourceDestroyed(AActor* DestroyedSource);

	UPROPERTY(VisibleAnywhere, Category="Bombardment|Visuals")
	TObjectPtr<UInstancedStaticMeshComponent> BoundaryRing;
	UPROPERTY(VisibleAnywhere, Category="Bombardment|Visuals")
	TObjectPtr<UInstancedStaticMeshComponent> CountdownRing;
	UPROPERTY(Transient)
	FGameplayEffectSpecHandle PendingDamageSpec;
	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<AActor> AimActor;
	FTimerHandle ImpactTimer;
	bool bResolvingImpact = false;
	EAeyerjiBombardmentPhase LastPresentedPhase = EAeyerjiBombardmentPhase::Inactive;
};
