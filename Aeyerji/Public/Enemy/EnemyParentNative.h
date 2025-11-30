// EnemyParentNative.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiCharacter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/OutlineHighlightComponent.h"

#include "EnemyParentNative.generated.h"

class UPrimitiveComponent;

class UGameplayAbility;
struct FPropertyChangedEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnemyDiedSignature, AActor*, Enemy);
/**
 * Native base class for AI-controlled "creep" / enemy pawns.
 * Blueprint children should inherit from this (NOT from ACharacter directly).
 */
UCLASS()
class AEYERJI_API AEnemyParentNative : public AAeyerjiCharacter, public IGenericTeamAgentInterface   
{
	GENERATED_BODY()

public:
	
	/* IGenericTeamAgentInterface */
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamId; }
	virtual void SetGenericTeamId(const FGenericTeamId& NewID) override { TeamId = NewID; }


	/* IAbilitySystemInterface */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
	{
		return AbilitySystemAeyerji;
	}

	/* ====== LIFECYCLE ====== */

	AEnemyParentNative();

protected:
	/* --- AActor overrides --- */
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void NotifyActorBeginCursorOver() override;
	virtual void NotifyActorEndCursorOver() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/* Initialise ASC owner/avatar pointers */
	void InitAbilityActorInfo();

	/* Grant abilities & effects defined below (runs once, server only) */
	void GiveStartupAbilitiesAndEffects();

	/* --------- Death hook (Blueprint-extendable) --------- */
	virtual void OnDeath_Implementation();

	/* ====== DESIGN-TIME LISTS ====== */

	/** Abilities given on spawn (level 1) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	/** Gameplay Effects applied on spawn (e.g. passive buffs) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Internal guard so we don't double-grant after seamless travel */
	bool bStartupGiven = false;

	UPROPERTY(EditDefaultsOnly, Category="AI")
	uint8 TeamId = 1;                            // 0 = Players by convention

public:
	/** Fired when this pawn dies so encounter directors can react immediately. */
	UPROPERTY(BlueprintAssignable, Category="Enemy|Events")
	FEnemyDiedSignature OnEnemyDied;

	/* ====== OUTLINE HIGHLIGHT ====== */
public:
	UFUNCTION(BlueprintCallable, Category="Enemy|Highlight")
	void SetEnemyHighlighted(bool bInHighlighted);
	
	bool IsHoverTargetComponent(const UPrimitiveComponent* Component) const;

protected:
	void RefreshEnemyHighlightTargets();
	void UpdateEnemyHighlightState();
	void ConfigureEnemyOutlineComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TObjectPtr<UOutlineHighlightComponent> OutlineHighlight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TArray<TObjectPtr<UPrimitiveComponent>> AdditionalHighlightPrimitives;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight", meta=(ClampMin="0", ClampMax="255", DisplayName="Highlight Channel"))
	int32 HighlightChannel = 20;

	UPROPERTY()
	int32 HighlightStencilValue_DEPRECATED = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	bool bHighlightOnSpawn = false;

	UPROPERTY(BlueprintReadOnly, Category="Enemy|Highlight")
	bool bEnemyHighlighted = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Enemy|Highlight")
	int32 HoverHighlightRefCount = 0;

	UFUNCTION()
	void HandleMeshBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleMeshEndCursorOver(UPrimitiveComponent* TouchedComponent);
};
