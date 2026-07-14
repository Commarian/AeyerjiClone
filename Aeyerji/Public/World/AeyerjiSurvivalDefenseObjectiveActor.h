// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Interaction/AeyerjiInteractable.h"
#include "GameFramework/Actor.h"
#include "AeyerjiSurvivalDefenseObjectiveActor.generated.h"

class UAbilitySystemComponent;
class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;
class UAeyerjiAttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAeyerjiDefenseObjectiveOutOfHealthSignature, AActor*, ObjectiveActor, AActor*, InstigatorActor, float, DamageTaken);

/** GAS-backed static target used as the defendable objective in survival rounds. */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiSurvivalDefenseObjectiveActor : public AActor, public IAbilitySystemInterface, public IGenericTeamAgentInterface, public IAeyerjiInteractable
{
	GENERATED_BODY()

public:
	AAeyerjiSurvivalDefenseObjectiveActor();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Replication for upgrade stats
	virtual void OnRep_UpgradeReflectFraction();
	virtual void OnRep_UpgradeRegenPerSecond();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual bool CanInteract_Implementation(AAeyerjiPlayerController* Controller) override;
	virtual FVector GetInteractionLocation_Implementation() override;
	virtual float GetInteractionRadius_Implementation() override;
	virtual void Interact_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Returns true after the objective has reached zero health on the server. */
	UFUNCTION(BlueprintPure, Category="Survival|Defense")
	bool IsObjectiveDestroyed() const { return bObjectiveDestroyed; }

	/** Returns the replicated/current health from the objective ASC. */
	UFUNCTION(BlueprintPure, Category="Survival|Defense")
	float GetObjectiveHealth() const;

	/** Returns the replicated/current max health from the objective ASC. */
	UFUNCTION(BlueprintPure, Category="Survival|Defense")
	float GetObjectiveMaxHealth() const;

	/** Broadcasts on the authority when this objective reaches zero health. */
	UPROPERTY(BlueprintAssignable, Category="Survival|Defense")
	FAeyerjiDefenseObjectiveOutOfHealthSignature OnObjectiveOutOfHealth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	TObjectPtr<UBoxComponent> TargetCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	TObjectPtr<UStaticMeshComponent> StaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense|Repair")
	TObjectPtr<USphereComponent> RepairInteractionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	TObjectPtr<UAeyerjiAttributeSet> AttributeSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(ClampMin="1.0"))
	float MaxHealth = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(ClampMin="0.0"))
	float Armor = 0.f;

	/** Team 0 matches the player side, making enemy team 1 hostile to the objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	uint8 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bDisableCollisionWhenDestroyed = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bHideWhenDestroyed = false;

	/** Enables click/overlap query interaction so players can open the repair menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Repair")
	bool bEnableRepairInteraction = true;

	/** Server-validated radius for opening the repair menu around this objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Repair", meta=(ClampMin="1.0", Units="cm"))
	float RepairInteractionRadius = 350.f;

	/** Runtime accumulated reflect fraction from survival upgrades (e.g. TreeReflectDamage). Replicated for clients. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense|Upgrades")
	float UpgradeReflectFraction = 0.f;

	/** Runtime regen rate per second from survival upgrades (e.g. TreeRegen). */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense|Upgrades")
	float UpgradeRegenPerSecond = 0.f;

public:
	/** Applies a reflect damage fraction upgrade (server only). */
	UFUNCTION(BlueprintAuthorityOnly, Category="Survival|Defense|Upgrades")
	void AddTreeReflectFraction(float Amount);

	/** Applies a regen per second upgrade (server only). */
	UFUNCTION(BlueprintAuthorityOnly, Category="Survival|Defense|Upgrades")
	void AddTreeRegenPerSecond(float Amount);

	/** Applies max HP upgrade directly to the ASC (server only). */
	UFUNCTION(BlueprintAuthorityOnly, Category="Survival|Defense|Upgrades")
	void ApplyTreeMaxHealthUpgrade(float DeltaHP);

	/** Performs one regen tick if applicable (called by director or timer). */
	UFUNCTION(BlueprintAuthorityOnly, Category="Survival|Defense|Upgrades")
	void ApplyRegenTick();

	/** Resets all runtime upgrade stats (called on run end / clear). */
	UFUNCTION(BlueprintAuthorityOnly, Category="Survival|Defense|Upgrades")
	void ResetSurvivalUpgrades();

protected:
	/** Blueprint hook for cosmetic destruction responses. Runs where HandleObjectiveOutOfHealth executes. */
	UFUNCTION(BlueprintImplementableEvent, Category="Survival|Defense")
	void BP_OnObjectiveDestroyed(AActor* InstigatorActor, float DamageTaken);

	UFUNCTION()
	void HandleObjectiveOutOfHealth(AActor* VictimActor, AActor* InstigatorActor, float DamageTaken);

	UFUNCTION()
	void OnRep_ObjectiveDestroyed();

	void InitializeObjectiveAttributes();
	void ApplyDestroyedPresentation();

	UPROPERTY(ReplicatedUsing=OnRep_ObjectiveDestroyed, VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bObjectiveDestroyed = false;
};
