// ===============================
// File: AeyerjiTargetingManager.h
// ===============================
#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Engine/TimerHandle.h"
#include "Net/UnrealNetwork.h"
#include "AeyerjiTargetingManager.generated.h"

class AAeyerjiPlayerController;
class UAbilitySystemComponent;
struct FHitResult;

UENUM()
enum class EAeyerjiCastFlow : uint8
{
	Normal,
	AwaitingGround,
	AwaitingEnemy,
	AwaitingFriend
};

/** Tunable settings that shape how targeting previews behave. */
USTRUCT(BlueprintType)
struct FAeyerjiTargetingTunables
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Targeting")
	float RangePreviewTickRate = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Targeting")
	float RangePreviewDrawLife = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Targeting")
	float RangePreviewThickness = 2.5f;
};

/** Input data the manager needs to resolve the current click. */
USTRUCT()
struct FAeyerjiTargetingClickContext
{
	GENERATED_BODY()

	bool bHasGroundHit = false;
	FHitResult GroundHit;

	/** Optional: hovered enemy (pre-filtered) to prefer over raw hit results. */
	TWeakObjectPtr<AActor> HoveredEnemy;
};

/**
 * Functions bound by the owner that the manager can call to execute actions.
 * Kept as TFunctions so the manager stays agnostic of the owner implementation.
 */
USTRUCT()
struct FAeyerjiTargetingHooks
{
	GENERATED_BODY()

	/** Returns true when the cursor hit the ground, populating OutHit. */
	TFunction<bool(FHitResult& OutHit)> GroundTrace;

	/** Activates an ability that targets a ground point. */
	TFunction<void(const FAeyerjiAbilitySlot&, const FVector_NetQuantize&)> ActivateAtLocation;

	/** Activates an ability that targets an actor. */
	TFunction<void(const FAeyerjiAbilitySlot&, AActor*)> ActivateOnActor;
};

/**
 * Small manager that owns ability targeting state and preview drawing.
 * It does not perform traces itself; the owning controller provides those via hooks.
 */
UCLASS()
class AEYERJI_API UAeyerjiTargetingManager : public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;
	void Initialize(AAeyerjiPlayerController* InOwner, const FAeyerjiTargetingTunables& InTunables);
	void SetHooks(FAeyerjiTargetingHooks InHooks);

	void BeginTargeting(const FAeyerjiAbilitySlot& Slot);
	bool HandleClick(const FAeyerjiTargetingClickContext& Context);
	void ClearTargeting();

	bool IsTargeting() const { return CastFlow != EAeyerjiCastFlow::Normal; }
	void TickPreview();

private:
	void StartRangePreview(const FAeyerjiAbilitySlot& Slot);
	void StopRangePreview();
	float ResolveAbilityPreviewRange(const FAeyerjiAbilitySlot& Slot) const;
	void DrawAbilityRangePreview(float Range, EAeyerjiTargetMode Mode);
	UAbilitySystemComponent* GetControlledAbilitySystem() const;

	AAeyerjiPlayerController* OwnerPC = nullptr;
	FAeyerjiTargetingTunables Tunables;
	FAeyerjiTargetingHooks Hooks;

	EAeyerjiCastFlow CastFlow = EAeyerjiCastFlow::Normal;
	FAeyerjiAbilitySlot PendingSlot;

	struct FPreviewState
	{
		bool bActive = false;
		float Range = 0.f;
		EAeyerjiTargetMode Mode = EAeyerjiTargetMode::Instant;
	};
	FPreviewState Preview;

	FTimerHandle PreviewTimer;
};
