// Copyright (c) 2026 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "AeyerjiLinkedTeleporter.generated.h"

class AAeyerjiPlayerController;
class APawn;
class USceneComponent;
class USphereComponent;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;
class APlayerState;

/**
 * Replicated two-point teleporter that only activates from an explicit player click request.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiLinkedTeleporter : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiLinkedTeleporter();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Resolves which teleporter endpoint owns the hit component. */
	bool ResolveEndpointFromComponent(const UPrimitiveComponent* Component, uint8& OutEndpointIndex) const;

	/** Returns the world-space center for the requested endpoint. */
	FVector GetEndpointLocation(uint8 EndpointIndex) const;

	/** Moves endpoint B by converting a world-space destination into this actor's relative endpoint transform. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Teleporter")
	void SetEndpointBWorldTransform(const FTransform& WorldTransform);

	/** Returns the usable click interaction radius for either endpoint. */
	float GetEndpointInteractionRadius() const;

	/** Returns true when the pawn is close enough for the server to allow endpoint use. */
	bool IsPawnInInteractionRange(const APawn* Pawn, uint8 EndpointIndex) const;

	/** Returns true when the controller is still waiting for its per-player cooldown to end. */
	bool IsControllerOnCooldown(const AAeyerjiPlayerController* Controller) const;

	/** Returns true when this endpoint is currently allowed to teleport to its linked destination. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Teleporter")
	bool IsEndpointEnabledForUse(uint8 EndpointIndex) const;

	/** Server-authoritative teleport request used by player controllers and Blueprints. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Teleporter")
	bool TryTeleport(AAeyerjiPlayerController* Controller, uint8 EndpointIndex);

	/** Authority-owned direction configuration replicated for client interaction presentation. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Teleporter|Direction")
	void SetAllowedDirections(bool bAllowAToB, bool bAllowBToA);

	/** Server-side boss-entry fact; endpoint A remains available to participants who have not entered yet. */
	bool HasPlayerEnteredFromEndpointA(const APlayerState* PlayerState) const;

protected:
	/** Updates endpoint transforms, mesh assignments, and interaction radii from editor properties. */
	void ApplyEndpointConfiguration();

	/** Applies replicated endpoint layout changes to client-side visual and interaction components. */
	UFUNCTION()
	void OnRep_EndpointBRelativeTransform();

	UFUNCTION()
	void OnRep_AllowedDirections();

	/** Returns the scene component for an endpoint index, or null for an invalid index. */
	USceneComponent* GetEndpointScene(uint8 EndpointIndex) const;

	/** Returns the interaction sphere for an endpoint index, or null for an invalid index. */
	USphereComponent* GetEndpointSphere(uint8 EndpointIndex) const;

	/** Returns the world transform for an endpoint index, or this actor's transform if invalid. */
	FTransform GetEndpointTransform(uint8 EndpointIndex) const;

	/** Converts one endpoint index into its linked destination endpoint index. */
	uint8 GetLinkedEndpointIndex(uint8 EndpointIndex) const;

	/** Returns true when the endpoint index is one of the two supported endpoints. */
	bool IsValidEndpointIndex(uint8 EndpointIndex) const;

	/** Starts or refreshes the cooldown timer for the controller that just teleported. */
	void StartCooldownForController(AAeyerjiPlayerController* Controller);

	/** Clears a completed cooldown entry. */
	void ClearCooldownForController(FObjectKey ControllerKey);

	/** Blueprint hook fired after a server-approved teleport succeeds. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Teleporter")
	void BP_OnTeleported(APawn* TeleportedPawn, uint8 FromEndpointIndex, uint8 ToEndpointIndex);

	/** Blueprint hook fired when the server rejects an attempted endpoint use. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Teleporter")
	void BP_OnTeleportRejected(APawn* RequestingPawn, uint8 EndpointIndex);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<USceneComponent> EndpointA = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<USceneComponent> EndpointB = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<UStaticMeshComponent> EndpointAMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<UStaticMeshComponent> EndpointBMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<USphereComponent> EndpointAInteraction = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Teleporter")
	TObjectPtr<USphereComponent> EndpointBInteraction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Teleporter")
	TObjectPtr<UStaticMesh> PortalMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_EndpointBRelativeTransform, Category="Aeyerji|Teleporter")
	FTransform EndpointBRelativeTransform = FTransform(FRotator::ZeroRotator, FVector(600.f, 0.f, 0.f), FVector::OneVector);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Teleporter", meta=(ClampMin="1.0", Units="cm"))
	float InteractionRadius = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_AllowedDirections, Category="Aeyerji|Teleporter|Direction")
	bool bAllowEndpointAToB = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_AllowedDirections, Category="Aeyerji|Teleporter|Direction")
	bool bAllowEndpointBToA = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Teleporter", meta=(ClampMin="0.0", Units="s"))
	float CooldownSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Teleporter")
	FVector ExitOffsetLocal = FVector(140.f, 0.f, 0.f);

private:
	TMap<FObjectKey, FTimerHandle> ControllerCooldownTimers;
	TSet<TWeakObjectPtr<APlayerState>> PlayersEnteredFromEndpointA;
};
