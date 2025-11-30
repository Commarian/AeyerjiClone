// ItemPickup.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ItemPickup.generated.h"

class UAeyerjiItemInstance;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Replicated world pickup that grants its stored item to overlapping inventory components.
 */
UCLASS()
class AEYERJI_API AItemPickup : public AActor
{
	GENERATED_BODY()

public:
	AItemPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Pickup")
	void SetItem(UAeyerjiItemInstance* InItem);

	UFUNCTION(BlueprintCallable, Category = "Pickup")
	UAeyerjiItemInstance* GetItem() const { return Item; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<USphereComponent> SphereComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Pickup")
	TObjectPtr<UAeyerjiItemInstance> Item;

	UFUNCTION()
	void HandleSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                         UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	                         const FHitResult& SweepResult);
};
