#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "AeyerjiNavSafetyComponent.generated.h"

class APawn;

/**
 * Server-authoritative guard that keeps live characters recoverable from non-navigable space.
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiNavSafetyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiNavSafetyComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Ensures the owning pawn is on usable nav, recovering immediately when requested. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation")
	bool EnsureOwnerOnSafeNav(bool bImmediateRecover = true);

	/** Forces a recovery attempt using the last known safe location as fallback. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation")
	bool RecoverOwnerToSafeNav();

	/** Returns the last safe grounded location tracked by this component. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Navigation")
	FVector GetLastSafeNavLocation() const { return LastSafeNavLocation; }

protected:
	/** Runtime recovery is authority-only and only applies to live pawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation")
	bool bEnableRuntimeRecovery = true;

	/** Interval between off-nav checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.01", Units="s"))
	float CheckInterval = 0.20f;

	/** Time allowed off nav before normal recovery runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="s"))
	float OffNavGraceSeconds = 0.35f;

	/** Extra height above WorldSettings.KillZ that triggers immediate recovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(Units="cm"))
	float KillZRecoveryMargin = 500.f;

	/** Absolute fallback Z used when a world has no useful KillZ configured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(Units="cm"))
	float AbsoluteCriticalZ = -10000.f;

	/** Shared nav projection and grounding settings for this pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation")
	FAeyerjiNavSafetyResolveParams ResolveParams;

	/** Most recent grounded location that passed nav safety checks. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FVector LastSafeNavLocation = FVector::ZeroVector;

	/** Rotation paired with the most recent safe location. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FRotator LastSafeNavRotation = FRotator::ZeroRotator;

	/** World time when LastSafeNavLocation was updated. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Navigation")
	double LastSafeNavTime = -1.0;

	/** World time when this pawn first failed the current off-nav check. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Navigation")
	double OffNavStartTime = -1.0;

private:
	APawn* GetOwnerPawn() const;
	bool IsOwnerRecoverable() const;
	bool IsOwnerBelowCriticalZ(const APawn* Pawn) const;
	void UpdateLastSafeLocation(const FAeyerjiNavSafetyResult& Result);
};
