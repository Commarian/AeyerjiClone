// AnimNotifyMeleeWindow.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "AnimNotifyMeleeWindow.generated.h"

// Forward declare to use TWeakObjectPtr<UAbilitySystemComponent> in the header
class UAbilitySystemComponent;

/** Opens a melee trace window and sends a Gameplay Event each tick with the current hits. */
UCLASS(meta=(DisplayName="Aeyerji: Melee Trace Window"))
class AEYERJI_API UAnimNotifyMeleeWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
    UAnimNotifyMeleeWindow();

	/** Event tag sent each tick while active. Your Gameplay Ability should listen for this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS")
	FGameplayTag TickEventTag;

	/** Radius of sphere sweep between sockets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace")
	float Radius = 20.f;

	/** Socket at the handle/hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace")
	FName StartSocket = FName("WeaponRHandSocket");

	/** Socket near the striking tip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace")
	FName EndSocket = FName("WeaponTip");

	/** Object channels to hit (use WorldDynamic/Pawn, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace")
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;

	/** Draw debug line/spheres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebug = false;

	/** Ignore repeated hits on the same actor while the window is open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace")
	bool bPreventRepeatedHitsDuringWindow = true;

	/** If true, only send events on the server (recommended for multiplayer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GAS")
	bool bServerOnly = true;

#if WITH_EDITOR
	// NOTE: UAnimNotifyState::ShouldFireInEditor() is NON-const in UE5.6.
	virtual bool ShouldFireInEditor() override { return false; } // don't spam in Persona
#endif

	// UAnimNotifyState interface
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
	/** Per-component state so multiple characters can swing simultaneously. */
	struct FWindowState
	{
		TSet<TWeakObjectPtr<AActor>> AlreadyHit;
		TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
	};

	/** Weak map keyed by SkeletalMeshComponent. */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FWindowState> ActiveWindows;

	void SendTickEvent(AActor* OwnerActor, const TArray<FHitResult>& Hits) const;
};

