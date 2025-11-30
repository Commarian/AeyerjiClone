#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AeyerjiWeaponActor.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class UItemDefinition;

/**
 * Simple replicated actor that displays the visuals for an equipped weapon.
 * Uses either a static or skeletal mesh pulled from the item definition.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiWeaponActor : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiWeaponActor();

	/** Applies the meshes from the given item definition (static or skeletal). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void InitializeFromDefinition(const UItemDefinition* Definition);

	/** Directly assigns meshes for blueprint-driven weapons. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetWeaponMeshes(UStaticMesh* StaticMesh, USkeletalMesh* SkeletalMesh);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;
};
