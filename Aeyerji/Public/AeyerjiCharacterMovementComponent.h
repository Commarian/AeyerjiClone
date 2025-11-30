// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "AeyerjiCharacterMovementComponent.generated.h"

class UAbilitySystemComponent;

/**
 * Custom character movement component with improved network smoothing
 */
UCLASS()
class AEYERJI_API UAeyerjiCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
    UAeyerjiCharacterMovementComponent();
	
	/** When true, character uses network smoothing when moving under AI control */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetworkSmoothing")
	bool bSmoothClientPosition_AIMovement = true;

	// Prediction settings
	/** Increase server movement update rate for better responsiveness */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetworkSmoothing")
	float MinNetUpdateFrequency = 100.0f;

	/** Maximum update rate for network updates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetworkSmoothing")
	float MaxNetUpdateFrequency = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NetworkSmoothing")
	float ClientPredictionFudgeFactor = 0.0f;

    // Get network smoothing distances with safety checks
    float GetNetworkMaxSmoothUpdateDistance() const { return FMath::Max(120.0f, NetworkMaxSmoothUpdateDistance); }

	/** Force immediate refresh of the cached rooted state */
	UFUNCTION(BlueprintCallable, Category="Movement")
	void ForceRootedStateRefresh();

	/** Override to prevent movement when rooted */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

	/** Override to prevent input processing when rooted */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/** Override to prevent input vector consumption when rooted */
	virtual FVector ConsumeInputVector() override;

    /** Override to clear velocity when rooted */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Optional: enable built-in RVO avoidance (settable in BP). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Avoidance|RVO")
    bool bEnableRVOAvoidance = true;

    /** RVO: how far we consider other agents for avoidance (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Avoidance|RVO", meta=(EditCondition="bEnableRVOAvoidance"))
    float RVOConsiderationRadius = 280.f;

    /** RVO: weight of avoidance steering. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movement|Avoidance|RVO", meta=(EditCondition="bEnableRVOAvoidance"))
    float RVOAvoidanceWeight = 0.55f;

    /** Apply RVO settings on startup. */
    virtual void BeginPlay() override;

protected:
	/** Gameplay tag that indicates the character is rooted */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	FGameplayTag RootedTag;

	/** Cached rooted state to avoid expensive ASC lookups every frame */
	mutable bool bCachedRootedState = false;

	/** Timestamp of last rooted state check */
	mutable float LastRootedCheckTime = 0.0f;

	/** How often to refresh the cached rooted state (in seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	float RootedStateCheckInterval = 0.1f;

	/** Get the Ability System Component from the character */
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	/** Network-safe method to check rooted state with caching */
	bool GetCachedRootedState() const;
};
