// AeyerjiViewDistanceCullComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Delegates/Delegate.h"
#include "TimerManager.h"
#include "AeyerjiViewDistanceCullComponent.generated.h"

class AActor;
class APawn;
class APlayerController;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EAeyerjiViewDistanceCullMode : uint8
{
	/** Cull everything except actors explicitly ignored. */
	CullAllActors UMETA(DisplayName="Cull All (Except Ignored)"),

	/** Cull only actors explicitly included via tag or class list. */
	CullOnlyMarkedActors UMETA(DisplayName="Cull Only Marked Actors")
};

/**
 * Local-only view distance culling around the controlled pawn.
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent), Config=Game, DefaultConfig)
class AEYERJI_API UAeyerjiViewDistanceCullComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiViewDistanceCullComponent();

	/** Master toggle for view-distance culling. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	bool bEnableCulling = true;

	/** Radius around the pawn that stays visible (cm). 0 = no cull. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling", meta=(ClampMin="0.0", Units="cm"))
	float CullRadius = 12000.f;

	/** Hysteresis buffer to reduce pop (cm). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling", meta=(ClampMin="0.0", Units="cm"))
	float CullHysteresis = 400.f;

	/** How often to evaluate culling (seconds). 0 = every tick. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling", meta=(ClampMin="0.0"))
	float CullInterval = 0.2f;

	/** Policy for which actors are eligible. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	EAeyerjiViewDistanceCullMode CullMode = EAeyerjiViewDistanceCullMode::CullAllActors;

	/** When false, actors with only static primitives are ignored. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	bool bCullStaticActors = false;

	/** When true, only the primary local player runs culling (avoids split-screen fights). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	bool bPrimaryPlayerOnly = true;

	/** Actor tag to exclude from culling. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	FName CullIgnoreActorTag = FName("ViewCull.Ignore");

	/** Actor tag to include when in CullOnlyMarkedActors mode. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling", meta=(EditCondition="CullMode==EAeyerjiViewDistanceCullMode::CullOnlyMarkedActors"))
	FName CullIncludeActorTag = FName("ViewCull.Include");

	/** Class list to exclude from culling. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling")
	TArray<TSubclassOf<AActor>> CullIgnoredClasses;

	/** Class list to include when in CullOnlyMarkedActors mode. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling", meta=(EditCondition="CullMode==EAeyerjiViewDistanceCullMode::CullOnlyMarkedActors"))
	TArray<TSubclassOf<AActor>> CullIncludedClasses;

	/** Debug: draw the cull radius around the pawn. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling|Debug")
	bool bDrawDebugSphere = false;

	/** Debug draw duration (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|View Culling|Debug", meta=(ClampMin="0.0"))
	float DebugDrawDuration = 0.1f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	struct FComponentVisibilityState
	{
		bool bCachedVisibility = false;
		bool bOriginalVisible = true;
		bool bOriginalHiddenInGame = false;
		bool bHiddenBySystem = false;
	};

	struct FActorCullState
	{
		bool bCulled = false;
		TMap<TWeakObjectPtr<UPrimitiveComponent>, FComponentVisibilityState> Components;
	};

	void EvaluateCulling();
	bool ResolveViewContext(FVector& OutViewLocation, APawn*& OutPawn, APlayerController*& OutPC) const;
	bool ShouldRunLocal(const APlayerController* PC) const;
	void RefreshTrackedActors();
	void RegisterActor(AActor* Actor);
	void UnregisterActor(AActor* Actor);
	bool IsCullableActor(const AActor* Actor) const;
	bool IsActorStaticOnly(const AActor* Actor, bool& bOutHasPrimitive) const;
	bool IsActorMarkedForCulling(const AActor* Actor) const;
	bool IsActorIgnored(const AActor* Actor) const;
	bool MatchesClassList(const AActor* Actor, const TArray<TSubclassOf<AActor>>& Classes) const;
	void ApplyCullState(AActor* Actor, FActorCullState& State, bool bShouldCull);
	void RestoreAllCulledActors();
	void CleanupInvalidActors();
	void HandleActorSpawned(AActor* Actor);

	FTimerHandle CullTimer;
	FDelegateHandle OnActorSpawnedHandle;
	TMap<TWeakObjectPtr<AActor>, FActorCullState> TrackedActors;
};
