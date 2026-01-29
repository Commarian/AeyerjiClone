// AeyerjiViewDistanceCullComponent.cpp

#include "Components/AeyerjiViewDistanceCullComponent.h"

#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

// Configure default tick behavior for the culling updater.
UAeyerjiViewDistanceCullComponent::UAeyerjiViewDistanceCullComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Initialize the culling timer and actor tracking for local clients.
void UAeyerjiViewDistanceCullComponent::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
	if (!OwnerPC)
	{
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			OwnerPC = Cast<APlayerController>(OwnerPawn->GetController());
		}
	}

	if (ShouldRunLocal(OwnerPC))
	{
		if (UWorld* World = GetWorld())
		{
			OnActorSpawnedHandle = World->AddOnActorSpawnedHandler(
				FOnActorSpawned::FDelegate::CreateUObject(this, &UAeyerjiViewDistanceCullComponent::HandleActorSpawned));

			if (CullInterval > 0.f)
			{
				World->GetTimerManager().SetTimer(
					CullTimer,
					this,
					&UAeyerjiViewDistanceCullComponent::EvaluateCulling,
					CullInterval,
					true);
			}
		}

		RefreshTrackedActors();
		EvaluateCulling();
	}
}

// Restore visibility and unregister delegates on shutdown.
void UAeyerjiViewDistanceCullComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CullTimer);
		if (OnActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(OnActorSpawnedHandle);
		}
	}

	RestoreAllCulledActors();
	TrackedActors.Empty();

	Super::EndPlay(EndPlayReason);
}

// Tick to support per-frame evaluation and to recover timer registration if needed.
void UAeyerjiViewDistanceCullComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);

	FVector ViewLoc = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	if (!ResolveViewContext(ViewLoc, Pawn, PC))
	{
		RestoreAllCulledActors();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (CullInterval > 0.f)
		{
			if (!World->GetTimerManager().IsTimerActive(CullTimer))
			{
				World->GetTimerManager().SetTimer(
					CullTimer,
					this,
					&UAeyerjiViewDistanceCullComponent::EvaluateCulling,
					CullInterval,
					true);
			}
		}
		else
		{
			World->GetTimerManager().ClearTimer(CullTimer);
		}
	}

	if (CullInterval <= 0.f)
	{
		EvaluateCulling();
	}
}

// Core evaluation pass that hides or restores actors based on distance.
void UAeyerjiViewDistanceCullComponent::EvaluateCulling()
{
	FVector ViewLoc = FVector::ZeroVector;
	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	if (!ResolveViewContext(ViewLoc, Pawn, PC))
	{
		RestoreAllCulledActors();
		return;
	}

	if (!bEnableCulling || CullRadius <= 0.f)
	{
		RestoreAllCulledActors();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bDrawDebugSphere)
	{
		DrawDebugSphere(World, ViewLoc, CullRadius, 24, FColor::Emerald, false, DebugDrawDuration, 0, 2.0f);
	}

	const float Buffer = FMath::Max(0.f, CullHysteresis);
	const float ExitRadius = CullRadius + Buffer;
	const float EnterRadius = FMath::Max(0.f, CullRadius - Buffer);
	const float ExitRadiusSq = ExitRadius * ExitRadius;
	const float EnterRadiusSq = EnterRadius * EnterRadius;

	TArray<TWeakObjectPtr<AActor>> ToRemove;
	ToRemove.Reserve(TrackedActors.Num());

	for (auto& Pair : TrackedActors)
	{
		AActor* Actor = Pair.Key.Get();
		if (!IsValid(Actor))
		{
			ToRemove.Add(Pair.Key);
			continue;
		}

		const float DistSq = FVector::DistSquared(ViewLoc, Actor->GetActorLocation());
		const bool bCurrentlyCulled = Pair.Value.bCulled;
		const bool bShouldCull = bCurrentlyCulled ? (DistSq > EnterRadiusSq) : (DistSq > ExitRadiusSq);
		ApplyCullState(Actor, Pair.Value, bShouldCull);
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : ToRemove)
	{
		TrackedActors.Remove(WeakActor);
	}

	CleanupInvalidActors();
}

// Resolve the local player controller and pawn to use as the view origin.
bool UAeyerjiViewDistanceCullComponent::ResolveViewContext(
	FVector& OutViewLocation, APawn*& OutPawn, APlayerController*& OutPC) const
{
	OutPawn = nullptr;
	OutPC = nullptr;
	OutViewLocation = FVector::ZeroVector;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return false;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (APlayerController* OwnerPC = Cast<APlayerController>(GetOwner()))
	{
		OutPC = OwnerPC;
		OutPawn = OwnerPC->GetPawn();
	}
	else if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OutPawn = OwnerPawn;
		OutPC = Cast<APlayerController>(OwnerPawn->GetController());
	}

	if (!OutPC || !OutPawn)
	{
		return false;
	}

	if (!ShouldRunLocal(OutPC))
	{
		return false;
	}

	OutViewLocation = OutPawn->GetActorLocation();
	return true;
}

// Gate local-only execution and avoid split-screen conflicts if desired.
bool UAeyerjiViewDistanceCullComponent::ShouldRunLocal(const APlayerController* PC) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return false;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	if (bPrimaryPlayerOnly)
	{
		const ULocalPlayer* LP = PC->GetLocalPlayer();
		if (LP && LP->GetControllerId() != 0)
		{
			return false;
		}
	}

	return true;
}

// Gather initial actors for culling on startup.
void UAeyerjiViewDistanceCullComponent::RefreshTrackedActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		RegisterActor(*It);
	}
}

// Track a new actor if it matches the culling rules.
void UAeyerjiViewDistanceCullComponent::RegisterActor(AActor* Actor)
{
	if (!IsCullableActor(Actor))
	{
		return;
	}

	TrackedActors.FindOrAdd(Actor);
}

// Untrack an actor and restore its visibility state.
void UAeyerjiViewDistanceCullComponent::UnregisterActor(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (FActorCullState* State = TrackedActors.Find(Actor))
	{
		ApplyCullState(Actor, *State, false);
		TrackedActors.Remove(Actor);
	}
}

// Determine if the actor should be managed by the culling system.
bool UAeyerjiViewDistanceCullComponent::IsCullableActor(const AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	if (Actor == GetOwner())
	{
		return false;
	}

	if (const APlayerController* OwnerPC = Cast<APlayerController>(GetOwner()))
	{
		if (Actor == OwnerPC->GetPawn())
		{
			return false;
		}
	}
	else if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (Actor == OwnerPawn)
		{
			return false;
		}
	}

	if (IsActorIgnored(Actor))
	{
		return false;
	}

	if (CullMode == EAeyerjiViewDistanceCullMode::CullOnlyMarkedActors && !IsActorMarkedForCulling(Actor))
	{
		return false;
	}

	bool bHasPrimitive = false;
	const bool bStaticOnly = IsActorStaticOnly(Actor, bHasPrimitive);
	if (!bHasPrimitive)
	{
		return false;
	}

	if (!bCullStaticActors && bStaticOnly)
	{
		return false;
	}

	return true;
}

// Check if an actor only contains static primitive components.
bool UAeyerjiViewDistanceCullComponent::IsActorStaticOnly(const AActor* Actor, bool& bOutHasPrimitive) const
{
	bOutHasPrimitive = false;
	if (!Actor)
	{
		return false;
	}

	TArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);

	for (const UPrimitiveComponent* Comp : Components)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		bOutHasPrimitive = true;
		if (Comp->Mobility != EComponentMobility::Static)
		{
			return false;
		}
	}

	return bOutHasPrimitive;
}

// Check whether the actor is explicitly included for culling.
bool UAeyerjiViewDistanceCullComponent::IsActorMarkedForCulling(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (!CullIncludeActorTag.IsNone() && Actor->ActorHasTag(CullIncludeActorTag))
	{
		return true;
	}

	return MatchesClassList(Actor, CullIncludedClasses);
}

// Check whether the actor is explicitly excluded from culling.
bool UAeyerjiViewDistanceCullComponent::IsActorIgnored(const AActor* Actor) const
{
	if (!Actor)
	{
		return true;
	}

	if (!CullIgnoreActorTag.IsNone() && Actor->ActorHasTag(CullIgnoreActorTag))
	{
		return true;
	}

	return MatchesClassList(Actor, CullIgnoredClasses);
}

// Utility to test actor inheritance against a class list.
bool UAeyerjiViewDistanceCullComponent::MatchesClassList(
	const AActor* Actor, const TArray<TSubclassOf<AActor>>& Classes) const
{
	if (!Actor)
	{
		return false;
	}

	for (const TSubclassOf<AActor>& Class : Classes)
	{
		if (Class && Actor->IsA(Class))
		{
			return true;
		}
	}

	return false;
}

// Apply or remove visibility overrides on the actor's primitive components.
void UAeyerjiViewDistanceCullComponent::ApplyCullState(
	AActor* Actor, FActorCullState& State, bool bShouldCull)
{
	if (!Actor)
	{
		return;
	}

	if (State.bCulled == bShouldCull)
	{
		return;
	}

	TArray<UPrimitiveComponent*> Components;
	Actor->GetComponents(Components);

	if (bShouldCull)
	{
		for (UPrimitiveComponent* Comp : Components)
		{
			if (!IsValid(Comp))
			{
				continue;
			}

			FComponentVisibilityState& CompState = State.Components.FindOrAdd(Comp);
			if (!CompState.bCachedVisibility)
			{
				CompState.bOriginalVisible = Comp->GetVisibleFlag();
				CompState.bOriginalHiddenInGame = Comp->bHiddenInGame;
				CompState.bCachedVisibility = true;
			}

			if (!CompState.bHiddenBySystem)
			{
				Comp->SetHiddenInGame(true, false);
				CompState.bHiddenBySystem = true;
			}
		}
	}
	else
	{
		for (auto& Pair : State.Components)
		{
			if (UPrimitiveComponent* Comp = Pair.Key.Get())
			{
				const FComponentVisibilityState& CompState = Pair.Value;
				if (CompState.bHiddenBySystem)
				{
					Comp->SetHiddenInGame(CompState.bOriginalHiddenInGame, false);
					if (Comp->GetVisibleFlag() != CompState.bOriginalVisible)
					{
						Comp->SetVisibility(CompState.bOriginalVisible, false);
					}
				}
			}
		}

		State.Components.Reset();
	}

	State.bCulled = bShouldCull;
}

// Restore visibility for any actors currently culled by this system.
void UAeyerjiViewDistanceCullComponent::RestoreAllCulledActors()
{
	for (auto& Pair : TrackedActors)
	{
		if (AActor* Actor = Pair.Key.Get())
		{
			ApplyCullState(Actor, Pair.Value, false);
		}
	}
}

// Remove any invalid actors or component state entries.
void UAeyerjiViewDistanceCullComponent::CleanupInvalidActors()
{
	TArray<TWeakObjectPtr<AActor>> ToRemove;
	ToRemove.Reserve(TrackedActors.Num());

	for (auto& Pair : TrackedActors)
	{
		if (!Pair.Key.IsValid())
		{
			ToRemove.Add(Pair.Key);
			continue;
		}

		TArray<TWeakObjectPtr<UPrimitiveComponent>> DeadComponents;
		for (auto& CompPair : Pair.Value.Components)
		{
			if (!CompPair.Key.IsValid())
			{
				DeadComponents.Add(CompPair.Key);
			}
		}

		for (const TWeakObjectPtr<UPrimitiveComponent>& Dead : DeadComponents)
		{
			Pair.Value.Components.Remove(Dead);
		}
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : ToRemove)
	{
		TrackedActors.Remove(WeakActor);
	}
}

// Callback for tracking actors spawned after BeginPlay.
void UAeyerjiViewDistanceCullComponent::HandleActorSpawned(AActor* Actor)
{
	RegisterActor(Actor);
}
