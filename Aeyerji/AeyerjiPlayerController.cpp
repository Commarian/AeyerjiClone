// ===============================
// File: AeyerjiPlayerController.cpp
// ===============================
// ReSharper disable CppTooWideScopeInitStatement
// ReSharper disable CppTooWideScope
#include "AeyerjiPlayerController.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiCharacterMovementComponent.h"
#include "Enemy/EnemyParentNative.h"
#include "CharacterStatsLibrary.h"
#include "GameplayTagContainer.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GenericTeamAgentInterface.h"
#include "NavigationSystem.h"
#include "Engine/LocalPlayer.h"
#include "NiagaraFunctionLibrary.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Components/CapsuleComponent.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "AeyerjiGameplayTags.h"
#include "Logging/AeyerjiLog.h"
#include "Items/InventoryComponent.h"
#include "MouseNavBlueprintLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "GUI/W_InventoryBag_Native.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/TargetPoint.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AeyerjiCameraOcclusionFadeComponent.h"
#include "Components/AeyerjiViewDistanceCullComponent.h"
#include "Engine/StaticMesh.h"
#include "GUI/W_EquipmentSlot.h"
#include "GUI/W_ItemTile.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"

template <class TAsset>
static void LoadIfNull(TObjectPtr<TAsset>& Dest, const TCHAR* AssetPath)
{
	if (!Dest)
	{
		static ConstructorHelpers::FObjectFinder<TAsset> Finder(AssetPath);
		if (Finder.Succeeded())
		{
			Dest = Finder.Object;
		}
	}
}

AAeyerjiPlayerController::AAeyerjiPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	CameraOcclusionFade = CreateDefaultSubobject<UAeyerjiCameraOcclusionFadeComponent>(TEXT("CameraOcclusionFade"));
	ViewDistanceCull = CreateDefaultSubobject<UAeyerjiViewDistanceCullComponent>(TEXT("ViewDistanceCull"));

	LoadIfNull(IMC_Default, TEXT("/Game/Player/Input/IMC_Default.IMC_Default"));
	LoadIfNull(IA_Attack_Click,    TEXT("/Game/Player/Input/Actions/IA_Attack_Click.IA_Attack_Click"));
	LoadIfNull(IA_Move_Click,    TEXT("/Game/Player/Input/Actions/IA_Move_Click.IA_Move_Click"));
	LoadIfNull(FX_Cursor,   TEXT("/Game/Cursor/FX_Cursor.FX_Cursor"));
	LoadIfNull(IA_ShowLoot,   TEXT("/Game/Player/Input/Actions/IA_ShowLoot.IA_ShowLoot"));
	LoadIfNull(IA_DropItem, TEXT("/Game/Player/Input/Actions/IA_DropItem.IA_DropItem"));

	bAutoManageActiveCameraTarget = false;

	if (CursorFollowTurnRateBuckets.Num() == 0)
	{
		FCursorFollowTurnRateBucket Bucket;

		Bucket.MaxAngleDeg = 15.f;
		Bucket.TurnRateScalar = 0.2f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 30.f;
		Bucket.TurnRateScalar = 0.3f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 50.f;
		Bucket.TurnRateScalar = 0.45f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 75.f;
		Bucket.TurnRateScalar = 0.6f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 130.f;
		Bucket.TurnRateScalar = 0.75f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 190.f;
		Bucket.TurnRateScalar = 0.9f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 270.f;
		Bucket.TurnRateScalar = 1.0f;
		CursorFollowTurnRateBuckets.Add(Bucket);

		Bucket.MaxAngleDeg = 360.f;
		Bucket.TurnRateScalar = 1.15f;
		CursorFollowTurnRateBuckets.Add(Bucket);
	}
}
void AAeyerjiPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdatePathFollowingForPawnState();

	if (!IsLocalController())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const bool bNeedsRefresh = MouseNavCacheRefreshInterval <= 0.f
		|| LastMouseNavCacheUpdateTime < 0.0
		|| (Now - LastMouseNavCacheUpdateTime) >= MouseNavCacheRefreshInterval;

	if (bNeedsRefresh)
	{
		RefreshMouseNavContextCache();
		LastMouseNavCacheUpdateTime = Now;
	}


}

void AAeyerjiPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	UpdatePathFollowingForPawnState();
}

void AAeyerjiPlayerController::OnUnPossess()
{
	AbortMovement_Both();
	StopPendingPickup();
	bAttackClickHeld = false;
	CancelFaceActor();

	Super::OnUnPossess();

	UpdatePathFollowingForPawnState();
}

void AAeyerjiPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();

	AbortMovement_Local();
	StopMoveToActorLoop();
	StopPendingPickup();
	bAttackClickHeld = false;
	CancelFaceActor();

	if (UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>())
	{
		PFC->UpdateCachedComponents();
	}

	UpdatePathFollowingForPawnState();
}

void AAeyerjiPlayerController::UpdatePathFollowingForPawnState()
{
	if (UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>())
	{
		APawn* ControlledPawn = GetPawn();
		const bool bHasMoveComp = ControlledPawn && ControlledPawn->GetMovementComponent() != nullptr;
		const bool bShouldDisable = (!ControlledPawn) || !bHasMoveComp || IsControlledPawnDead();

		if (bShouldDisable)
		{
			PFC->AbortMove(*this, FPathFollowingResultFlags::ForcedScript);
			PFC->SetActive(false);
			PFC->PrimaryComponentTick.SetTickFunctionEnable(false);
			bPathFollowingTickSuppressed = true;
			return;
		}

		if (bPathFollowingTickSuppressed)
		{
			PFC->SetActive(true);
			PFC->PrimaryComponentTick.SetTickFunctionEnable(true);
			bPathFollowingTickSuppressed = false;
		}
	}
}


void AAeyerjiPlayerController::AbortMovement_Local() const
{
	if (UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>())
	{
		PFC->AbortMove(*this, FPathFollowingResultFlags::ForcedScript);
	}
	if (APawn* P = GetPawn())
	{
		if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(P->GetMovementComponent()))
		{
			CMC->StopActiveMovement();
			CMC->StopMovementImmediately();
		}
	}
}

void AAeyerjiPlayerController::Server_AbortMovement_Implementation()
{
	AbortMovement_Local();
}

void AAeyerjiPlayerController::AbortMovement_Both()
{
	StopMoveToActorLoop();
	AbortMovement_Local();
	if (!HasAuthority())
	{
		Server_AbortMovement();
	}
}


void AAeyerjiPlayerController::ReportMouseNavContextToServer(EMouseNavResult Result, const FVector& NavLocation, const FVector& CursorLocation, APawn* ClickedPawn)
{
	if (!IsLocalController())
	{
		return;
	}

	if (Result == EMouseNavResult::None)
	{
		MouseNavServerCache.Invalidate();
		return;
	}

	SetMouseNavContextInternal(Result, NavLocation, CursorLocation, ClickedPawn);

	if (!HasAuthority())
	{
		Server_SetMouseNavContext(Result, NavLocation, CursorLocation, ClickedPawn);
	}
}

void AAeyerjiPlayerController::Server_SetMouseNavContext_Implementation(EMouseNavResult Result, FVector NavLocation, FVector CursorLocation, APawn* ClickedPawn)
{
	SetMouseNavContextInternal(Result, NavLocation, CursorLocation, ClickedPawn);
}

bool AAeyerjiPlayerController::GetCachedMouseNavContext(EMouseNavResult& OutResult, FVector& OutNavLocation, FVector& OutCursorLocation, APawn*& OutPawn, float MaxAgeSeconds) const
{
	if (IsLocalController())
	{
		return false;
	}

	if (MouseNavServerCache.Result == EMouseNavResult::None)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (MouseNavServerCache.Timestamp < 0.0)
	{
		return false;
	}

	if (MaxAgeSeconds > 0.f && World && (Now - MouseNavServerCache.Timestamp) > MaxAgeSeconds)
	{
		return false;
	}

	OutResult = MouseNavServerCache.Result;
	OutNavLocation = MouseNavServerCache.NavLocation;
	OutCursorLocation = MouseNavServerCache.CursorLocation;
	OutPawn = MouseNavServerCache.Pawn.Get();
	return OutResult != EMouseNavResult::None;
}

bool AAeyerjiPlayerController::HasShowLootMapping(const UInputMappingContext* Context) const
{
	if (!Context || !IA_ShowLoot)
	{
		return false;
	}

	const FKey AltKeys[] = { EKeys::LeftAlt, EKeys::RightAlt };

	for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
	{
		if (Mapping.Action == IA_ShowLoot)
		{
			for (const FKey& Key : AltKeys)
			{
				if (Mapping.Key == Key)
				{
					return true;
				}
			}
		}
	}

	return false;
}

void AAeyerjiPlayerController::EnsureShowLootBinding()
{
	if (!IsLocalController() || !IA_ShowLoot)
	{
		return;
	}

	if (bShowLootFallbackAdded)
	{
		return;
	}

	if (HasShowLootMapping(IMC_Default))
	{
		return;
	}

	if (!IMC_ShowLootFallback)
	{
		IMC_ShowLootFallback = NewObject<UInputMappingContext>(this, TEXT("IMC_ShowLoot_Fallback"));
		IMC_ShowLootFallback->MapKey(IA_ShowLoot, EKeys::LeftAlt);
		IMC_ShowLootFallback->MapKey(IA_ShowLoot, EKeys::RightAlt);
	}

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
		{
			if (!Sub->HasMappingContext(IMC_ShowLootFallback))
			{
				Sub->AddMappingContext(IMC_ShowLootFallback, 1);
				bShowLootFallbackAdded = true;
				AJ_LOG(this, TEXT("[PC] Added fallback ShowLoot mapping to LeftAlt (missing from IMC_Default)"));
			}
		}
	}
}

void AAeyerjiPlayerController::SetMouseNavContextInternal(EMouseNavResult Result, const FVector& NavLocation, const FVector& CursorLocation, APawn* ClickedPawn)
{
	if (Result == EMouseNavResult::None)
	{
		MouseNavServerCache.Invalidate();
		return;
	}

	MouseNavServerCache.Result = Result;
	MouseNavServerCache.NavLocation = NavLocation;
	MouseNavServerCache.CursorLocation = CursorLocation;
	MouseNavServerCache.Pawn = ClickedPawn;

	if (UWorld* World = GetWorld())
	{
		MouseNavServerCache.Timestamp = World->GetTimeSeconds();
	}
	else
	{
		MouseNavServerCache.Timestamp = 0.0;
	}
}

void AAeyerjiPlayerController::RefreshMouseNavContextCache()
{
	FVector NavLocation;
	FVector CursorLocation;
	APawn* HitPawn = nullptr;

	const EMouseNavResult Result = UMouseNavBlueprintLibrary::GetMouseNavContext(this, this, NavLocation, CursorLocation, HitPawn);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (Result != EMouseNavResult::None)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MouseNav] Refresh cached result %s Nav=%s Cursor=%s"),
			*StaticEnum<EMouseNavResult>()->GetNameStringByValue(static_cast<int64>(Result)),
			*NavLocation.ToCompactString(), *CursorLocation.ToCompactString());
	}
#endif
}



void AAeyerjiPlayerController::EnsureLocomotionRotationMode()
{
	// If we were in a temporary “face target” mode, ensure we’re fully restored.
	PopFacingRotationMode(); // no-op if not active

	if (ACharacter* C = Cast<ACharacter>(GetPawn()))
	{
		if (UCharacterMovementComponent* CMC = C->GetCharacterMovement())
		{
			// Canonical locomotion defaults for top-down click-move:
			C->bUseControllerRotationYaw           = false; // character does NOT follow control yaw
			CMC->bOrientRotationToMovement         = true;  // face move direction
			CMC->bUseControllerDesiredRotation     = false; // CM owns rotation during locomotion
			// RotationRate.Yaw stays at your normal locomotion value (Pop restored it already)
		}
	}
}

bool AAeyerjiPlayerController::ComputePickupGoal(const AAeyerjiLootPickup* Loot, FVector& OutGoal) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav) return false;

	const APawn* ThisPawn = GetPawn();
	if (!ThisPawn) return false;

	OutGoal = ThisPawn->GetActorLocation();

	if (!IsValid(Loot))
	{
		return true;
	}

	const FVector Center = Loot->GetPickupNavCenter();
	const float Radius = FMath::Max(Loot->GetPickupNavRadius(), 30.f);
	const float AcceptRadiusSq = (PickupAcceptRadius > 5000.f) ? PickupAcceptRadius : FMath::Square(PickupAcceptRadius);
	const float AcceptRadius = FMath::Sqrt(FMath::Max(0.f, AcceptRadiusSq));

	FNavLocation Projected;
	const FVector PrimaryExtent(Radius, Radius, 600.f);

	auto FinalizeGoal = [&](const FVector& NavPoint)
	{
		FVector Goal = NavPoint;
		const FVector ToCenter = (Center - Goal).GetSafeNormal2D();
		if (!ToCenter.IsNearlyZero())
		{
			const float DesiredDepth = FMath::Clamp(Radius * 0.35f, 15.f, AcceptRadius * 0.9f);
			if (DesiredDepth > KINDA_SMALL_NUMBER)
			{
				const FVector Nudged = Goal + ToCenter * DesiredDepth;
				FNavLocation NudgedProjected;
				if (Nav->ProjectPointToNavigation(Nudged, NudgedProjected, FVector(40.f, 40.f, 600.f)))
				{
					Goal = NudgedProjected.Location;
				}
			}
		}

		OutGoal = Goal;
		return true;
	};

	auto TryProjectAndFinalize = [&](const FVector& Probe, const FVector& Extents)
	{
		if (!Nav->ProjectPointToNavigation(Probe, Projected, Extents))
		{
			return false;
		}
		return FinalizeGoal(Projected.Location);
	};

	if (TryProjectAndFinalize(Center, PrimaryExtent))
	{
		return true;
	}

	const FVector PawnLoc = ThisPawn->GetActorLocation();

	const int32 NumSamples = 8;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Angle = (2.f * PI * Index) / NumSamples;
		const FVector Dir = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const FVector Sample = Center + Dir * (Radius * 0.75f);
		if (TryProjectAndFinalize(Sample, FVector(60.f, 60.f, 600.f)))
		{
			return true;
		}
	}

	const FVector ToCenter = (Center - PawnLoc).GetSafeNormal2D();
	if (!ToCenter.IsNearlyZero())
	{
		const FVector AlongPath = Center - ToCenter * FMath::Min(Radius * 0.5f, 150.f);
		if (TryProjectAndFinalize(AlongPath, FVector(60.f, 60.f, 600.f)))
		{
			return true;
		}
	}

	if (Nav->GetRandomPointInNavigableRadius(Center, Radius, Projected))
	{
		return FinalizeGoal(Projected.Location);
	}

	UE_LOG(LogTemp, Warning, TEXT("[PC] Failed to find pickup goal for %s (center %s, radius %.1f)"), *GetNameSafe(Loot), *Center.ToString(), Radius);
	return false;
}

void AAeyerjiPlayerController::StartPendingPickup(AAeyerjiLootPickup* Loot)
{
	PendingPickup = Loot;
	GetWorldTimerManager().SetTimer(
		PendingPickupTimer, this,
		&AAeyerjiPlayerController::ProcessPendingPickup,
		PendingPickupInterval, true);

	AJ_LOG(this, TEXT("[PC] StartPendingPickup %s"), Loot ? *Loot->GetName() : TEXT("None"));
}

void AAeyerjiPlayerController::StopPendingPickup()
{
	GetWorldTimerManager().ClearTimer(PendingPickupTimer);
	AJ_LOG(this, TEXT("[PC] StopPendingPickup"));
	PendingPickup = nullptr;
}

void AAeyerjiPlayerController::ProcessPendingPickup()
{
	if (!PendingPickup.IsValid())
	{
		StopPendingPickup();
		return;
	}

	APawn* P = GetPawn();
	if (!P)
	{
		StopPendingPickup();
		return;
	}

	const FVector PickupCenter = PendingPickup->GetPickupNavCenter();
	const float D2 = FVector::DistSquared2D(P->GetActorLocation(), PickupCenter);
	const float AcceptRadiusSq = (PickupAcceptRadius > 5000.f) ? PickupAcceptRadius : FMath::Square(PickupAcceptRadius);
	if (D2 < AcceptRadiusSq)
	{
		AJ_LOG(this, TEXT("[PC] Close enough - calling Server_Pickup"));
		AbortMovement_Both();
		if (PendingPickup.IsValid())
		{
			Server_RequestPickupActor(PendingPickup.Get());
		}
		StopPendingPickup();
	}
}

void AAeyerjiPlayerController::OnShowLootPressed()
{
	AJ_LOG(this, TEXT("[PC] ShowLoot pressed - revealing loot labels"));
	UAeyerjiInventoryBPFL::SetAllLootLabelsVisible(this, true );
}

void AAeyerjiPlayerController::OnShowLootReleased()
{
	AJ_LOG(this, TEXT("[PC] ShowLoot released - hiding loot labels"));
	UAeyerjiInventoryBPFL::SetAllLootLabelsVisible(this, false);
}

void AAeyerjiPlayerController::Server_AddPickupIntent_Implementation(FName LootActorName)
{
	if (AAeyerjiLootPickup* Loot = FindLootPickupByName(LootActorName))
	{
		Loot->AddPickupIntent(this);
		AJ_LOG(this, TEXT("[PC-Server] AddIntent for %s"), *GetNameSafe(Loot));
	}
}

void AAeyerjiPlayerController::Server_ClearPickupIntent_Implementation(FName LootActorName)
{
	if (AAeyerjiLootPickup* Loot = FindLootPickupByName(LootActorName))
	{
		Loot->RemovePickupIntent(this);
		AJ_LOG(this, TEXT("[PC-Server] ClearIntent for %s"), *GetNameSafe(Loot));
	}
}

void AAeyerjiPlayerController::Server_RequestPickup_Implementation(FName LootActorName)
{
	if (AAeyerjiLootPickup* Loot = FindLootPickupByName(LootActorName))
	{
		AJ_LOG(this, TEXT("[PC-Server] RequestPickup for %s"), *GetNameSafe(Loot));
		Loot->ExecutePickup(this);
	}
	else
	{
		AJ_LOG(this, TEXT("[PC-Server] RequestPickup failed - '%s' not found"), *LootActorName.ToString());
	}
}

void AAeyerjiPlayerController::Server_RequestPickupActor_Implementation(AActor* LootActor)
{
	AAeyerjiLootPickup* TypedLoot = Cast<AAeyerjiLootPickup>(LootActor);

	if (!IsValid(TypedLoot))
	{
		AJ_LOG(this, TEXT("[PC-Server] RequestPickup failed - LootActor null/invalid"));
		return;
	}

	AJ_LOG(this, TEXT("[PC-Server] RequestPickup for %s"), *GetNameSafe(TypedLoot));
	TypedLoot->ExecutePickup(this);
}

AAeyerjiLootPickup* AAeyerjiPlayerController::FindLootPickupByName(FName LootActorName) const
{
	if (LootActorName.IsNone())
	{
		return nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AAeyerjiLootPickup> It(World); It; ++It)
		{
			if (AAeyerjiLootPickup* Loot = *It)
			{
				if (!IsValid(Loot))
				{
					continue;
				}

				if (Loot->GetFName() == LootActorName)
				{
					return Loot;
				}
			}
		}
	}

	return nullptr;
}

void AAeyerjiPlayerController::BeginAbilityTargeting(const FAeyerjiAbilitySlot& Slot)
{
	EnsureTargetingManagerInitialized();
	if (TargetingManager)
	{
		TargetingManager->BeginTargeting(Slot);
	}
}

bool AAeyerjiPlayerController::OnClick_Implementation()
{
	return false;
}

void AAeyerjiPlayerController::StartMoveToActorLoop(AActor* Target,
                                                    float AcceptanceRadius, bool bPreferBehind, float BehindDistance, float ArcHalfAngleDeg)
{
	if (!IsValid(Target)) { StopMoveToActorLoop(); return; }

	MoveLoopTarget              = Target;
	MoveLoopAcceptanceRadius    = FMath::Max(0.f, AcceptanceRadius);
	bMoveLoopPreferBehind       = bPreferBehind;
	MoveLoopBehindDistance      = FMath::Max(50.f, BehindDistance);
	MoveLoopArcHalfAngleDeg     = FMath::Clamp(ArcHalfAngleDeg, 0.f, 180.f);
	MoveLoopMode                = EAeyerjiMoveLoopMode::StopOnly;
	bMoveLoopArrivedBroadcast   = false;

	AJ_LOG(this, TEXT("[PC] StartMoveToActorLoop -> Mode=StopOnly Target=%s AR=%.1f"),
	       *GetNameSafe(Target), MoveLoopAcceptanceRadius);

	TickMoveLoop();
	GetWorldTimerManager().SetTimer(MoveLoopTimer, this,
	                                &AAeyerjiPlayerController::TickMoveLoop, MoveLoopInterval, true);
}

void AAeyerjiPlayerController::StopMoveToActorLoop()
{
	GetWorldTimerManager().ClearTimer(MoveLoopTimer);
	MoveLoopTarget = nullptr;
	bMoveLoopArrivedBroadcast = false;
}

void AAeyerjiPlayerController::TickMoveLoop()
{
	const APawn* MyPawn = GetPawn();
	AActor* Target = MoveLoopTarget.Get();

	if (!MyPawn || !IsValid(Target))
	{
		StopMoveToActorLoop();
		return;
	}

	const float Dist2D = FVector::Dist2D(MyPawn->GetActorLocation(), Target->GetActorLocation());

	// Treat actual capsule contact as "inside" too (helps when path goal is fuzzy but we're already bumping)
	// Tweak buffers if you want slightly looser/tighter contact detection.
	const bool bTouching = AreCapsulesTouching2D(MyPawn, Target, /*ExtraRadiusBufferCm=*/6.f, /*ZSlackCm=*/30.f);

	const bool bInside = bTouching || (Dist2D <= MoveLoopAcceptanceRadius);

	if (bInside)
	{
		switch (MoveLoopMode)
		{
		case EAeyerjiMoveLoopMode::StopOnly:
		{
			AbortMovement_Both();
			if (!bMoveLoopArrivedBroadcast)
			{
				bMoveLoopArrivedBroadcast = true;
				OnMoveLoopArrived.Broadcast(Target);
				AJ_LOG(this, TEXT("[PC] MoveLoop Arrived: Mode=%s Target=%s"),
				       *UEnum::GetValueAsString(MoveLoopMode), *GetNameSafe(Target));
			}
			return;
		}

		case EAeyerjiMoveLoopMode::FollowOnly:
		{
			const bool bFirstArrival = !bMoveLoopArrivedBroadcast;
			if (bFirstArrival)
			{
				AbortMovement_Local();
				// Friendly follow mode: stop movement but avoid firing attack-oriented arrival delegates.
				if (!HasAuthority())
				{
					// Ensure the server path following stops, but keep the follow loop active.
					Server_AbortMovement();
				}

				bMoveLoopArrivedBroadcast = true;
				AJ_LOG(this, TEXT("[PC] MoveLoop Arrived: Mode=%s Target=%s"),
				       *UEnum::GetValueAsString(MoveLoopMode), *GetNameSafe(Target));
			}
			return;
		}

		default:
			ensureMsgf(false, TEXT("Unknown MoveLoopMode value: %d"), (int32)MoveLoopMode);
			return;
		}
	}
	else
	{
		// left the bubble; allow another OnMoveLoopArrived later
		bMoveLoopArrivedBroadcast = false;
	}

    // Compute a sensible goal and issue move (unchanged)
    FVector Goal;
    if (!ComputeSmartGoalForTarget(Target, bMoveLoopPreferBehind,
                                   MoveLoopBehindDistance, MoveLoopArcHalfAngleDeg, Goal))
    {
        Goal = Target->GetActorLocation();
    }

    // If a short avoidance shim is active, keep steering to that until it times out
    if (bEnableShortAvoidance)
    {
        const double NowTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        const APawn* MyPawn2 = GetPawn();
        const bool bCloseToSidestep = (MyPawn2 && !ActiveAvoidanceGoal.IsNearlyZero()
            && FVector::Dist2D(MyPawn2->GetActorLocation(), ActiveAvoidanceGoal) <= AvoidanceEarlyReleaseDistance);

        if (bAvoidanceActive && (NowTime < AvoidanceEndTime) && !ActiveAvoidanceGoal.IsNearlyZero() && !bCloseToSidestep)
        {
            Goal = ActiveAvoidanceGoal; // continue holding current sidestep
        }
        else
        {
            // Clear hold if expired/close, then optionally pick a new sidestep for this tick
            bAvoidanceActive = false;
            ActiveAvoidanceGoal = FVector::ZeroVector;
            AvoidanceEndTime = 0.0;

            // Only attempt avoidance in the actor move-loop (not for one-shot ground clicks)
            if (MoveLoopTarget.IsValid())
            {
                (void)AdjustGoalForShortAvoidance(Goal);
            }
        }
    }

    if (FVector::DistSquared2D(Goal, PendingMoveGoal) > FMath::Square(20.f))
    {
        PendingMoveGoal = Goal;
        IssueMoveRPC(Goal);
        SpawnCursorFX(Goal);
    }
}

void AAeyerjiPlayerController::ApplyAvoidanceProfile(const UAeyerjiAvoidanceProfile* Profile)
{
    if (!Profile) return;

    // Apply PlayerController short-avoidance tuning
    bEnableShortAvoidance                         = Profile->bEnableShortAvoidance;
    AvoidanceProbeDistance                        = Profile->ProbeDistance;
    AvoidanceSideStepDistance                     = Profile->SideStepDistance;
    AvoidanceProbeRadiusScale                     = Profile->ProbeRadiusScale;
    AvoidanceHoldTimeMin                          = Profile->HoldTimeMin;
    AvoidanceHoldTimeMax                          = Profile->HoldTimeMax;
    bSkipAvoidanceWhenBlockingIsCurrentTarget     = Profile->bSkipWhenBlockingIsCurrentTarget;
    bBiasDetourAroundTargetTangent                = Profile->bBiasDetourAroundTargetTangent;
    bAvoidanceProjectToNavmesh                    = Profile->bProjectToNavmesh;
    AvoidanceMinDistanceToGoal                    = Profile->MinDistanceToGoal;
    AvoidanceMinSpeedCmPerSec                     = Profile->MinSpeedCmPerSec;
    AvoidanceMinTimeBetweenTriggers               = Profile->MinTimeBetweenTriggers;
    AvoidanceEarlyReleaseDistance                 = Profile->EarlyReleaseDistance;
    AvoidanceBlockedNudgeScale                    = Profile->BlockedNudgeScale;
    bAvoidanceDebugDraw                           = Profile->bDebugDraw;

    auto ApplyRVOToPawn = [&](APawn* TargetPawn, bool bEnableRVO)
    {
        if (!TargetPawn) return;
        UCharacterMovementComponent* Base = nullptr;
        if (ACharacter* Char = Cast<ACharacter>(TargetPawn))
        {
            Base = Char->GetCharacterMovement();
        }
        else
        {
            Base = Cast<UCharacterMovementComponent>(TargetPawn->GetMovementComponent());
        }
        if (auto* Move = Cast<UAeyerjiCharacterMovementComponent>(Base))
        {
            Move->bEnableRVOAvoidance = bEnableRVO;
            Move->RVOConsiderationRadius = Profile->RVOConsiderationRadius;
            Move->RVOAvoidanceWeight     = Profile->RVOAvoidanceWeight;
            if (bEnableRVO)
            {
                Move->bUseRVOAvoidance = true;
                Move->AvoidanceConsiderationRadius = Profile->RVOConsiderationRadius;
                Move->AvoidanceWeight = Profile->RVOAvoidanceWeight;
            }
        }
    };

    // Player pawn
    ApplyRVOToPawn(GetPawn(), Profile->bEnableRVO_Player);

    // Enemies (current)
    UWorld* World = GetWorld();
    if (World)
    {
        for (TActorIterator<APawn> It(World); It; ++It)
        {
            APawn* P = *It;
            if (!P || P == GetPawn()) continue;
            // Team check: different team than us => enemy
            const IGenericTeamAgentInterface* Me = Cast<IGenericTeamAgentInterface>(GetPawn());
            const IGenericTeamAgentInterface* Them = Cast<IGenericTeamAgentInterface>(P);
            const bool bEnemy = (Me && Them) ? (Me->GetGenericTeamId() != Them->GetGenericTeamId()) : P->IsA<APawn>();
            if (bEnemy)
            {
                ApplyRVOToPawn(P, Profile->bEnableRVO_Enemies);
            }
        }
    }
}

bool AAeyerjiPlayerController::ComputeSmartGoalForTarget(AActor* Target,
                                                         bool bPreferBehind,
                                                         float BehindDistance,
                                                         float ArcHalfAngleDeg,
                                                         FVector& OutGoal) const
{
	if (!IsValid(Target))
		return false;

	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const FVector TargetLoc = Target->GetActorLocation();
	const FVector Fwd       = Target->GetActorForwardVector().GetSafeNormal2D();

	// 1) Desired point: directly “behind” the target
	FVector Desired = TargetLoc - Fwd * BehindDistance;

	FNavLocation Projected;
	const FVector Ext = NavProjectExtents; // tweakable from BP

	// 2) Try the direct behind point first
	if (Nav && Nav->ProjectPointToNavigation(Desired, Projected, Ext))
	{
		OutGoal = Projected.Location;
		return true;
	}

	// 3) Fan left/right around an arc behind the target
	const float Radius = FMath::Max(BehindDistance, 120.f);
	const int32  Steps = 6; // granularity of the fan

	// Angle of the “pure behind” direction relative to world X/Y
	const float BaseTheta = FMath::Atan2((-Fwd).Y, (-Fwd).X);

	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Delta = FMath::DegreesToRadians((ArcHalfAngleDeg / Steps) * Step);
		for (int32 Side = -1; Side <= 1; Side += 2) // -1 = left, +1 = right
		{
			const float Theta = BaseTheta + (Side * Delta);
			const FVector Offset(Radius * FMath::Cos(Theta),
			                     Radius * FMath::Sin(Theta),
			                     0.f);
			const FVector Candidate = TargetLoc + Offset;

			if (!Nav)
			{
				OutGoal = Candidate;
				return true;
			}
			if (Nav->ProjectPointToNavigation(Candidate, Projected, Ext))
			{
				OutGoal = Projected.Location;
				return true;
			}
		}
	}

	// 4) Absolute fallback: go for the target’s current location
	OutGoal = TargetLoc;
	return true;
}

void AAeyerjiPlayerController::StartFollowActorLoop(AActor* Target,
                                                    float AcceptanceRadius, float BehindDistance, float ArcHalfAngleDeg)
{
	if (!IsValid(Target)) { StopMoveToActorLoop(); return; }

	MoveLoopTarget              = Target;
	MoveLoopAcceptanceRadius    = FMath::Max(0.f, AcceptanceRadius);
	bMoveLoopPreferBehind       = true;
	MoveLoopBehindDistance      = FMath::Max(50.f, BehindDistance);
	MoveLoopArcHalfAngleDeg     = FMath::Clamp(ArcHalfAngleDeg, 0.f, 180.f);
	MoveLoopMode                = EAeyerjiMoveLoopMode::FollowOnly;
	bMoveLoopArrivedBroadcast   = false;

	AJ_LOG(this, TEXT("[PC] StartFollowActorLoop -> Mode=FollowOnly Target=%s AR=%.1f"),
	       *GetNameSafe(Target), MoveLoopAcceptanceRadius);

	TickMoveLoop();
	GetWorldTimerManager().SetTimer(MoveLoopTimer, this,
	                                &AAeyerjiPlayerController::TickMoveLoop, MoveLoopInterval, true);
}

void AAeyerjiPlayerController::BeginPlay()
{
    Super::BeginPlay();
	EnableCheats();
	AddCheats(true);
    EnsureTargetingManagerInitialized();
    if (IsLocalController())
    {
        if (IMC_Default)
        {
            StartHoverPolling();
            if (auto* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
            {
                Sub->AddMappingContext(IMC_Default, 0);
            }
        }
        EnsureShowLootBinding();
    }

    // Apply profile if assigned (server authoritative, but run for local settings too)
    if (AvoidanceProfile)
    {
        ApplyAvoidanceProfile(AvoidanceProfile);
    }
}

void AAeyerjiPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (auto* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (IA_Attack_Click)
		{
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnAttackClickPressed);
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Triggered, this, &AAeyerjiPlayerController::OnAttackClickHeld);
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnAttackClickReleased);
		}
		if (IA_Move_Click)
		{
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnMoveClickPressed);
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Triggered, this, &AAeyerjiPlayerController::OnMoveClickHeld);
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnMoveClickReleased);
		}
		if (IA_ShowLoot)
		{
			EIC->BindAction(IA_ShowLoot, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnShowLootPressed);
			EIC->BindAction(IA_ShowLoot, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnShowLootReleased);
		}
		if (IA_DropItem)
		{
			EIC->BindAction(IA_DropItem, ETriggerEvent::Started, this, &AAeyerjiPlayerController::OnDropItemPressed);
		}
	}
}

// ---------------- NEW: BP intercept default impl ----------------
bool AAeyerjiPlayerController::OnPreClickPawnHit_Implementation(AActor* Actor, const FHitResult& Hit)
{
	// Default native behavior: do NOT consume; BP can override to return true to consume.
	return false;
}

// --------------- Server notify handler --------------------
void AAeyerjiPlayerController::Server_NotifyPawnClicked_Implementation(AActor* Actor)
{
	// Only meaningful on the server; broadcast for server-side BP listeners.
	OnServerPawnClicked.Broadcast(Actor);
}

void AAeyerjiPlayerController::OnAttackClickPressed(const FInputActionValue&)
{
	// Common per-click reset
	ResetForClick();
	EnsureTargetingManagerInitialized();
	bAttackClickHeld = true;

	// Give BP a chance to consume ANY click if really needed
	const bool ConsumedByClickBP = OnClick();
	
	// If we are in a special casting state (e.g., AwaitingGround), handle it and consume the click
	if (TargetingManager && TargetingManager->IsTargeting() && TargetingManager->HandleClick(BuildTargetingClickContext()))
	{
		return;
	}
	
	// Give Blueprint a chance to handle pawn clicks (replicate/override hook)
	FHitResult PawnHit;
	const bool PawnWasHit = TryGetPawnHit(PawnHit);
	if (PawnWasHit)
	{
		const bool WasConsumedByBP = TryConsumePawnHit(PawnHit);
		if (WasConsumedByBP)
		{
			return;
		}

		AActor* HitActor = PawnHit.GetActor();
		if (IsAttackableActor(HitActor))
		{
			CachedTarget = HitActor;
			if (APawn* HitPawn = Cast<APawn>(HitActor))
			{
				ReportMouseNavContextToServer(EMouseNavResult::ClickedPawn,
				                              HitPawn->GetActorLocation(),
				                              PawnHit.ImpactPoint,
				                              HitPawn);
			}

			return;
		}
	}

	if (ConsumedByClickBP)
	{
		return;
	}
    
	// If we clicked on loot, handle pickup logic (may enqueue intent and issue a move)
	FHitResult LootHit;
	const bool LootWasHit = TryGetLootHit(LootHit);
	if (LootWasHit)
	{
		AAeyerjiLootPickup* LootActor = Cast<AAeyerjiLootPickup>(LootHit.GetActor());
		if (LootActor != nullptr)
		{
			const bool WasConsumedByLoot = HandleLootUnderCursor(LootActor, LootHit);
			if (WasConsumedByLoot)
			{
				return; // do not also process ground click
			}
		}
	}
	
	FHitResult SurfaceHit;
	const bool HasSurfaceHit = TryGetGroundHit(SurfaceHit);
	if (!HasSurfaceHit)
	{
		return;
	}

	// Clear any pending pickup intent before moving
	ClearPickupIntentIfAny();

	// Move toward the ground location, and keep the actor (if any) as the current target
	MoveToGroundFromHit(SurfaceHit, /*bSpawnCursorFX=*/true, /*bIsContinuous=*/false);
	BeginCursorFollowHold(SurfaceHit.ImpactPoint);
}

void AAeyerjiPlayerController::OnAttackClickHeld(const FInputActionValue&)
{
	if (!bAttackClickHeld)
	{
		return;
	}

	if (TargetingManager && TargetingManager->IsTargeting())
	{
		return;
	}

	FHitResult SurfaceHit;
	const bool HasSurfaceHit = TryGetGroundHit(SurfaceHit);
	if (!HasSurfaceHit)
	{
		return;
	}

	if (!ShouldRunCursorFollowHold(SurfaceHit.ImpactPoint))
	{
		return;
	}

	MoveToGroundFromHit(SurfaceHit, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);
}

void AAeyerjiPlayerController::OnAttackClickReleased(const FInputActionValue&)
{
	bAttackClickHeld = false;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	ResetCursorFollowHold();
	ResetCursorFollowTurnRate();
	if (!HasAuthority())
	{
		Server_ResetCursorFollowTurnRate();
	}
}

bool AAeyerjiPlayerController::ActivatePrimaryAttackAbility()
{
	UAbilitySystemComponent* ASC = GetControlledAbilitySystem();
	if (!ASC)
	{
		return false;
	}

	FGameplayTagContainer TagSearch;
	if (!BuildPrimaryAttackTagSearch(ASC, TagSearch))
	{
		return false;
	}

	return ASC->TryActivateAbilitiesByTag(TagSearch, /*bAllowRemoteActivation=*/true);
}

bool AAeyerjiPlayerController::BuildPrimaryAttackTagSearch(UAbilitySystemComponent* ASC, FGameplayTagContainer& OutTags) const
{
	OutTags.Reset();

	if (!ASC)
	{
		return false;
	}

	FGameplayTag Leaf = UCharacterStatsLibrary::GetLeafTagFromBranchTag(ASC, AeyerjiTags::Ability_Primary);
	if (!Leaf.IsValid())
	{
		Leaf = AeyerjiTags::Ability_Primary;
	}

	if (!Leaf.IsValid())
	{
		return false;
	}

	OutTags.AddTag(Leaf);

	FString Name = Leaf.ToString();
	while (true)
	{
		int32 Dot = INDEX_NONE;
		if (!Name.FindLastChar('.', Dot))
		{
			break;
		}
		Name = Name.Left(Dot);
		const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(*Name);
		if (Parent.IsValid())
		{
			OutTags.AddTag(Parent);
			if (Parent == AeyerjiTags::Ability_Primary)
			{
				break;
			}
		}
		else
		{
			break;
		}
	}

	return OutTags.Num() > 0;
}

void AAeyerjiPlayerController::OnMoveClickPressed(const FInputActionValue& /*Val*/)
{
	if (HandleMovementBlockedByAbilities())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ClickPressed blocked by ability tags."));
		return;
	}

	// Additional move-only reset: clear targeting/casting, clear any pickup intent, clear pending move goal
	ResetForMoveOnly();

	// Move-only: we just need a ground point
	FHitResult SurfaceHit;
	const bool HasSurfaceHit = TryGetGroundHit(SurfaceHit);
	if (!HasSurfaceHit)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ClickPressed no ground hit (trace failed)."));
		return;
	}

	// Move-only: go to the ground location; do not set a target from the hit actor
	MoveToGroundFromHit(SurfaceHit, /*bSpawnCursorFX=*/true, /*bIsContinuous=*/false);
	BeginCursorFollowHold(SurfaceHit.ImpactPoint);
}

void AAeyerjiPlayerController::OnMoveClickHeld(const FInputActionValue& /*Val*/)
{
	static double LastBlockedWarnTime = -1.0;
	static double LastGroundWarnTime = -1.0;
	const UWorld* World = GetWorld();

	if (HandleMovementBlockedByAbilities())
	{
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (LastBlockedWarnTime < 0.0 || (World && (Now - LastBlockedWarnTime) >= 0.25))
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[Move] ClickHeld blocked by ability tags."));
			LastBlockedWarnTime = Now;
		}
		return;
	}

	FHitResult SurfaceHit;
	const bool HasSurfaceHit = TryGetGroundHit(SurfaceHit);
	if (!HasSurfaceHit)
	{
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (LastGroundWarnTime < 0.0 || (World && (Now - LastGroundWarnTime) >= 0.25))
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[Move] ClickHeld no ground hit (trace failed)."));
			LastGroundWarnTime = Now;
		}
		return;
	}

	if (!ShouldRunCursorFollowHold(SurfaceHit.ImpactPoint))
	{
		return;
	}

	MoveToGroundFromHit(SurfaceHit, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);
}

void AAeyerjiPlayerController::OnMoveClickReleased(const FInputActionValue& /*Val*/)
{
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	ResetCursorFollowHold();
	ResetCursorFollowTurnRate();
	if (!HasAuthority())
	{
		Server_ResetCursorFollowTurnRate();
	}
}

void AAeyerjiPlayerController::OnDropItemPressed(const FInputActionValue& /*Val*/)
{
	TryDropItemUnderCursor();
}

void AAeyerjiPlayerController::HandleMoveCommand(bool bSpawnCursorFX, bool bIsContinuous)
{
	// ---------- Gather context ----------
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] HandleMoveCommand aborted: no pawn."));
		return;
	}

	const bool bHasTarget = CachedTarget.IsValid();

	// Where are we trying to go? If there's a target, use its location; else the ground point.
	const FVector TargetLocation = bHasTarget
		? CachedTarget->GetActorLocation()
		: CachedGoal;

	// Distance-squared (cheap) from pawn to the chosen target location.
	const float DistSqToTarget = FVector::DistSquared(MyPawn->GetActorLocation(), TargetLocation);

	// Thresholds precomputed and named for readability.
	const float MinMoveDistSq = FMath::Square(MinMoveDistanceCm);
	const bool  bFarEnough    = (DistSqToTarget >= MinMoveDistSq);

	// ---------- Early out if too close ----------
	if (!bFarEnough)
	{
		// We are already close enough; do not spam move/attack.
		return;
	}

	// ---------- Command dispatch ----------
	// Prefer actor-targeted move/attack if we have a valid attackable target.

	if (bIsContinuous)
	{
		if (bHasTarget && IsAttackableActor(CachedTarget.Get()))
		{
			ResetCursorFollowTurnRate();
			bCursorFollowActive = false;
			if (PendingMoveTarget.Get() != CachedTarget.Get())
			{
				PendingMoveTarget = CachedTarget;
				IssueMoveRPC(CachedTarget.Get());
			}
		}
		else
		{
			PendingMoveTarget = nullptr;
			UpdateContinuousMoveGoal(TargetLocation);
		}
	}
	else
	{
		ResetCursorFollowTurnRate();
		bCursorFollowActive = false;
		PendingMoveTarget = nullptr;
		if (bHasTarget && IsAttackableActor(CachedTarget.Get()))
		{
			// Overload: AActor*
			IssueMoveRPC(CachedTarget.Get());
		}
		else
		{
			// Overload: FVector
			UpdateCursorFollowTurnRate(TargetLocation);
			if (!HasAuthority())
			{
				Server_ApplyCursorFollowTurnRate(TargetLocation);
			}
			IssueMoveRPC(TargetLocation);
			if (bSpawnCursorFX)
			{
				SpawnCursorFX(TargetLocation);
			}
		}
	}

}

void AAeyerjiPlayerController::SpawnCursorFX(const FVector& Loc) const
{
	if (FX_Cursor && IsLocalController())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX_Cursor, Loc);
	}
}

bool AAeyerjiPlayerController::IsAttackableActor(const AActor* Other) const
{
	if (!Other || Other == GetPawn()) return false;
	const IGenericTeamAgentInterface* Me = Cast<IGenericTeamAgentInterface>(GetPawn());
	const IGenericTeamAgentInterface* Rival = Cast<IGenericTeamAgentInterface>(Other);
	if (Me && Rival) return Me->GetGenericTeamId() != Rival->GetGenericTeamId();
	return Other->IsA<APawn>();
}

void AAeyerjiPlayerController::ResetForClick()
{
	CancelFaceActor();
	ClearPickupIntentIfAny();
	EnsureLocomotionRotationMode();
	PendingMoveGoal = FVector::ZeroVector;
	PendingMoveTarget = nullptr;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	ResetCursorFollowHold();
}

void AAeyerjiPlayerController::ResetForMoveOnly()
{
	CancelFaceActor();
	ClearPickupIntentIfAny();
	EnsureLocomotionRotationMode();
	ClearTargeting();
	PendingMoveGoal = FVector::ZeroVector;
	PendingMoveTarget = nullptr;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	ResetCursorFollowHold();
}

UAbilitySystemComponent* AAeyerjiPlayerController::GetControlledAbilitySystem() const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return nullptr;
	}

	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(ControlledPawn);
	return ASI ? ASI->GetAbilitySystemComponent() : nullptr;
}

bool AAeyerjiPlayerController::IsControlledPawnDead() const
{
	if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
	{
		if (ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		{
			return true;
		}
	}

	if (const APawn* ControlledPawn = GetPawn())
	{
		return ControlledPawn->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName());
	}

	return false;
}

bool AAeyerjiPlayerController::HandleMovementBlockedByAbilities()
{
	UAbilitySystemComponent* ASC = GetControlledAbilitySystem();
	if (!ASC)
	{
		return false;
	}

	const bool bHadMovementLock = ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_BlockMovement);

	FGameplayTagContainer ActiveMeleeTags;
	ActiveMeleeTags.AddTag(AeyerjiTags::State_Ability_PrimaryMelee_WindUp);
	ActiveMeleeTags.AddTag(AeyerjiTags::State_Ability_PrimaryMelee_HitWindow);
	ActiveMeleeTags.AddTag(AeyerjiTags::State_Ability_PrimaryMelee_Recovery);

	const bool bHadActiveMelee = ASC->HasAnyMatchingGameplayTags(ActiveMeleeTags);
	if (!bHadMovementLock && !bHadActiveMelee)
	{
		return false;
	}

	if (bHadActiveMelee)
	{
		FGameplayTagContainer CancelTags;
		CancelTags.AddTag(AeyerjiTags::Ability_Primary_Melee_Basic);
		ASC->CancelAbilities(&CancelTags);
	}

	const bool bStillLocked = ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_BlockMovement);
	return bStillLocked;
}

bool AAeyerjiPlayerController::TryDropItemUnderCursor()
{
	if (!IsLocalController())
	{
		return false;
	}

	TArray<UUserWidget*> TileWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, TileWidgets, UW_ItemTile::StaticClass(), false);
	for (UUserWidget* Widget : TileWidgets)
	{
		if (UW_ItemTile* Tile = Cast<UW_ItemTile>(Widget))
		{
			if (Tile->IsMouseOverItem())
			{
				return Tile->DropItemToGround();
			}
		}
	}

	TArray<UUserWidget*> SlotWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, SlotWidgets, UW_EquipmentSlot::StaticClass(), false);
	for (UUserWidget* Widget : SlotWidgets)
	{
		if (UW_EquipmentSlot* Slot = Cast<UW_EquipmentSlot>(Widget))
		{
			if (Slot->IsMouseOverItem())
			{
				return Slot->DropItemToGround();
			}
		}
	}

	TArray<UUserWidget*> BagWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, BagWidgets, UW_InventoryBag_Native::StaticClass(), false);
	for (UUserWidget* Widget : BagWidgets)
	{
		if (UW_InventoryBag_Native* Bag = Cast<UW_InventoryBag_Native>(Widget))
		{
			if (Bag->DropItemUnderCursor())
			{
				return true;
			}
		}
	}

	return false;
}

bool AAeyerjiPlayerController::TraceCursor(ECollisionChannel Channel, FHitResult& OutHit, bool bTraceComplex) const
{
	static double LastGroundTraceWarnTime = -1.0;
	const bool bIsGroundTrace = (Channel == ECC_GameTraceChannel2);
	const UWorld* WorldForTime = GetWorld();

	auto ShouldLogGroundTraceWarn = [&](const UWorld* InWorld) -> bool
	{
		if (!bIsGroundTrace)
		{
			return false;
		}

		const double Now = InWorld ? InWorld->GetTimeSeconds() : 0.0;
		if (LastGroundTraceWarnTime < 0.0 || (InWorld && (Now - LastGroundTraceWarnTime) >= 0.25))
		{
			LastGroundTraceWarnTime = Now;
			return true;
		}

		return false;
	};

	FVector WorldOrigin;
	FVector WorldDir;
	if (!DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
	{
		if (ShouldLogGroundTraceWarn(WorldForTime))
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[Move] TraceCursor failed: DeprojectMousePositionToWorld."));
		}
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		if (ShouldLogGroundTraceWarn(World))
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[Move] TraceCursor failed: no world."));
		}
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CursorTrace), bTraceComplex);
	if (const APawn* MyPawn = GetPawn())
	{
		Params.AddIgnoredActor(MyPawn);
	}

	const FVector TraceStart = WorldOrigin;
	const FVector TraceEnd = TraceStart + WorldDir * 100000.f;

	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Channel, Params))
		{
			if (ShouldLogGroundTraceWarn(World))
			{
				UE_LOG(LogAeyerji, Warning, TEXT("[Move] TraceCursor failed: no hit (channel=%d)."), static_cast<int32>(Channel));
			}
			return false;
		}

		if (ShouldIgnoreCursorActor(Hit.GetActor()))
		{
			Params.AddIgnoredActor(Hit.GetActor());
			continue;
		}

		OutHit = Hit;
		return true;
	}

	return false;
}

bool AAeyerjiPlayerController::ShouldIgnoreCursorActor(const AActor* Actor) const
{
	// Ignore player-controlled pawns (and their owned actors) so cursor traces click through them.
	if (!Actor)
	{
		return false;
	}

	const APawn* MyPawn = GetPawn();
	if (Actor == MyPawn)
	{
		return true;
	}

	if (MyPawn && Actor->IsOwnedBy(MyPawn))
	{
		return true;
	}

	const AActor* OwnerActor = Actor->GetOwner();
	while (OwnerActor)
	{
		if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
		{
			if (OwnerPawn->IsPlayerControlled())
			{
				return true;
			}
		}
		OwnerActor = OwnerActor->GetOwner();
	}

	if (const APawn* PawnActor = Cast<APawn>(Actor))
	{
		if (PawnActor->IsPlayerControlled())
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiPlayerController::TryGetGroundHit(FHitResult& OutHit) const
{
	return TraceCursor(ECC_GameTraceChannel2, OutHit, /*bTraceComplex=*/false);
}

bool AAeyerjiPlayerController::TryGetPawnHit(FHitResult& OutHit) const
{
	const bool bHit = TraceCursor(ECC_GameTraceChannel3, OutHit, /*bTraceComplex=*/false);
	if (bHit)
	{
		const APawn* MyPawn = GetPawn();
		if (MyPawn && OutHit.GetActor() == MyPawn)
		{
			// We hit ourselves first; do a second trace that ignores our pawn so we can target through it.
			FVector WorldOrigin, WorldDir;
			if (DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
			{
				if (UWorld* World = GetWorld())
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(CursorPawnSkipSelf), /*bTraceComplex=*/false);
					Params.AddIgnoredActor(MyPawn);

					FHitResult AltHit;
					const FVector TraceStart = WorldOrigin;
					const FVector TraceEnd   = TraceStart + WorldDir * 100000.f;
					if (World->LineTraceSingleByChannel(AltHit, TraceStart, TraceEnd, ECC_GameTraceChannel3, Params))
					{
						OutHit = AltHit;
					}
				}
			}
		}

		return true;
	}

	if (!bEnableTargetSnap || TargetSnapScreenRadiusPx <= 0.f)
	{
		return false;
	}

	if (bTargetSnapRequiresNoLootUnderCursor)
	{
		FHitResult LootHit;
		if (TryGetLootHit(LootHit))
		{
			return false;
		}
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	const FVector2D CursorPos(MouseX, MouseY);

	const float ScaleMin = FMath::Min(TargetSnapZoomScaleMin, TargetSnapZoomScaleMax);
	const float ScaleMax = FMath::Max(TargetSnapZoomScaleMin, TargetSnapZoomScaleMax);
	float SnapScale = 1.f;

	if (TargetSnapCameraDistanceRef > KINDA_SMALL_NUMBER)
	{
		if (const APawn* MyPawn = GetPawn())
		{
			if (PlayerCameraManager)
			{
				const float CamDist = FVector::Dist(PlayerCameraManager->GetCameraLocation(), MyPawn->GetActorLocation());
				if (CamDist > KINDA_SMALL_NUMBER)
				{
					SnapScale = CamDist / TargetSnapCameraDistanceRef;
				}
			}
		}
	}

	SnapScale = FMath::Clamp(SnapScale, ScaleMin, ScaleMax);

	const float SnapRadiusPx = TargetSnapScreenRadiusPx * SnapScale;
	if (SnapRadiusPx <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float SnapRadiusPxSq = FMath::Square(SnapRadiusPx);

	const float WorldRadius = TargetSnapWorldRadiusCm * SnapScale;
	const float WorldRadiusSq = (WorldRadius > 0.f) ? FMath::Square(WorldRadius) : 0.f;

	FVector WorldRef = FVector::ZeroVector;
	bool bHasWorldRef = false;

	if (WorldRadius > 0.f)
	{
		FHitResult GroundHit;
		if (TryGetGroundHit(GroundHit))
		{
			WorldRef = GroundHit.ImpactPoint;
			bHasWorldRef = true;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AEnemyParentNative* BestEnemy = nullptr;
	float BestScreenDistSq = SnapRadiusPxSq + 1.f;
	float BestWorldDistSq = 0.f;

	for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
	{
		AEnemyParentNative* Enemy = *It;
		if (!IsValid(Enemy) || !IsAttackableActor(Enemy))
		{
			continue;
		}

		FVector2D ScreenPos;
		if (!ProjectWorldLocationToScreen(Enemy->GetActorLocation(), ScreenPos))
		{
			continue;
		}

		const float ScreenDistSq = FVector2D::DistSquared(ScreenPos, CursorPos);
		if (ScreenDistSq > SnapRadiusPxSq)
		{
			continue;
		}

		float WorldDistSq = 0.f;
		if (bHasWorldRef)
		{
			WorldDistSq = FVector::DistSquared2D(Enemy->GetActorLocation(), WorldRef);
			if (WorldDistSq > WorldRadiusSq)
			{
				continue;
			}
		}

		if (!BestEnemy || ScreenDistSq < BestScreenDistSq
			|| (FMath::IsNearlyEqual(ScreenDistSq, BestScreenDistSq) && bHasWorldRef && WorldDistSq < BestWorldDistSq))
		{
			BestEnemy = Enemy;
			BestScreenDistSq = ScreenDistSq;
			BestWorldDistSq = WorldDistSq;
		}
	}

	if (!BestEnemy)
	{
		return false;
	}

	OutHit = FHitResult();
	OutHit.HitObjectHandle = FActorInstanceHandle(BestEnemy);
	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(BestEnemy->GetRootComponent()))
	{
		OutHit.Component = RootPrim;
	}
	OutHit.bBlockingHit = true;
	OutHit.Location = BestEnemy->GetActorLocation();
	OutHit.ImpactPoint = BestEnemy->GetActorLocation();
	OutHit.TraceStart = OutHit.Location;
	OutHit.TraceEnd = OutHit.Location;
	return true;
}

bool AAeyerjiPlayerController::TryGetLootHit(FHitResult& OutHit) const
{
	if (!TraceCursor(ECC_GameTraceChannel1, OutHit, /*bTraceComplex=*/false))
	{
		return false;
	}

	const APawn* MyPawn = GetPawn();
	if (MyPawn && OutHit.GetActor() == MyPawn)
	{
		FVector WorldOrigin, WorldDir;
		if (DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
		{
			if (UWorld* World = GetWorld())
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(CursorLootSkipSelf), /*bTraceComplex=*/false);
				Params.AddIgnoredActor(MyPawn);

				FHitResult AltHit;
				const FVector TraceStart = WorldOrigin;
				const FVector TraceEnd   = TraceStart + WorldDir * 100000.f;
				if (World->LineTraceSingleByChannel(AltHit, TraceStart, TraceEnd, ECC_GameTraceChannel1, Params))
				{
					OutHit = AltHit;
				}
				else
				{
					return false;
				}
			}
		}
	}

	AAeyerjiLootPickup* LootActor = Cast<AAeyerjiLootPickup>(OutHit.GetActor());
	if (!LootActor)
	{
		return false;
	}

	if (!LootActor->IsHoverTargetComponent(OutHit.GetComponent()))
	{
		return false;
	}

	return true;
}

FAeyerjiTargetingClickContext AAeyerjiPlayerController::BuildTargetingClickContext() const
{
	FAeyerjiTargetingClickContext Context;
	Context.HoveredEnemy = HoveredEnemy;
	Context.bHasGroundHit = TryGetGroundHit(Context.GroundHit);
	return Context;
}

void AAeyerjiPlayerController::EnsureTargetingManagerInitialized()
{
	if (TargetingManager)
	{
		TargetingManager->Initialize(this, TargetingTunables);
		return;
	}

	TargetingManager = NewObject<UAeyerjiTargetingManager>(this);
	if (!TargetingManager)
	{
		return;
	}

	TargetingManager->Initialize(this, TargetingTunables);

	FAeyerjiTargetingHooks Hooks;
	Hooks.GroundTrace = [this](FHitResult& Hit) { return TryGetGroundHit(Hit); };
	Hooks.ActivateAtLocation = [this](const FAeyerjiAbilitySlot& Slot, const FVector_NetQuantize& Target)
	{
		Server_ActivateAbilityAtLocation(Slot, Target);
	};
	Hooks.ActivateOnActor = [this](const FAeyerjiAbilitySlot& Slot, AActor* TargetActor)
	{
		Server_ActivateAbilityOnActor(Slot, TargetActor);
	};

	TargetingManager->SetHooks(MoveTemp(Hooks));
}

void AAeyerjiPlayerController::ClearPickupIntentIfAny()
{
	if (PendingPickup.IsValid())
	{
		PendingPickup->Server_RemovePickupIntent(this);
		PendingPickup = nullptr;
	}
}

void AAeyerjiPlayerController::ClearTargeting()
{
	if (TargetingManager)
	{
		TargetingManager->ClearTargeting();
	}
}

bool AAeyerjiPlayerController::TryConsumePawnHit(const FHitResult& PawnHit)
{
	AActor* HitActor = PawnHit.GetActor();
	if (!HitActor) { return false; }

	// 1) Fire local BP signal (non-consuming)
	OnCursorPawnHit.Broadcast(HitActor, PawnHit);

	// 2) Offer BP a chance to CONSUME this (runs BEFORE native flow continues)
	const bool bConsumed = OnPreClickPawnHit(HitActor, PawnHit);
	if (bConsumed)
	{
		// Optional: still tell the server if you need authoritative awareness
		if (!HasAuthority())
		{
			Server_NotifyPawnClicked(HitActor);
		}
	}
	
	return bConsumed;
}

bool AAeyerjiPlayerController::HandleLootUnderCursor(AAeyerjiLootPickup* Loot, const FHitResult& LootHit)
{
	if (!Loot) { return false; }

	APawn* P = GetPawn();
	if (!P) { return true; } // treat as consumed—no further processing

	const FVector PickupCenter = Loot->GetPickupNavCenter();
	const float D2 = FVector::DistSquared2D(P->GetActorLocation(), PickupCenter);
	const float AcceptRadiusSq = (PickupAcceptRadius > 5000.f) ? PickupAcceptRadius : FMath::Square(PickupAcceptRadius);
	
	if (D2 < AcceptRadiusSq)
	{
		AJ_LOG(this, TEXT("[PC-Server] HandleLootUnderCursor %s"), *GetNameSafe(Loot));
		AbortMovement_Both();
		Loot->RequestPickupFromClient(this);
	}
	else
	{
		// If we already had a different loot intent, clear it
		if (PendingPickup.IsValid() && PendingPickup.Get() != Loot)
		{
			Server_ClearPickupIntent(PendingPickup->GetFName());
			PendingPickup = nullptr;
		}

		Server_AddPickupIntent(Loot->GetFName());

		FVector Goal;
		const bool bFoundGoal = ComputePickupGoal(Loot, Goal);
		if (bFoundGoal)
		{
			IssueMoveRPC(Goal);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PC] Cannot find navigable pickup goal for %s"), *GetNameSafe(Loot));
		}
		StartPendingPickup(Loot);
	}

	return true; // consumed (don’t also process ground)
}

void AAeyerjiPlayerController::MoveToGroundFromHit(const FHitResult& SurfaceHit, bool bSpawnCursorFX, bool bIsContinuous)
{
	CachedGoal = SurfaceHit.ImpactPoint;
	HandleMoveCommand(bSpawnCursorFX, bIsContinuous);
}

AActor* AAeyerjiPlayerController::GetOrCreateCursorFollowActor()
{
	if (CursorFollowActor.IsValid())
	{
		return CursorFollowActor.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	ATargetPoint* Target = World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Target)
	{
		Target->SetActorHiddenInGame(true);
		Target->SetActorEnableCollision(false);
		Target->SetReplicates(false);
		Target->SetCanBeDamaged(false);
		CursorFollowActor = Target;
	}

	return CursorFollowActor.Get();
}

void AAeyerjiPlayerController::UpdateContinuousMoveGoal(const FVector& Goal)
{
	if (!GetPawn())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] UpdateContinuousMoveGoal ignored: no pawn."));
		return;
	}
	if (IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] UpdateContinuousMoveGoal ignored: pawn dead."));
		return;
	}

	EnsureLocomotionRotationMode();
	UpdateCursorFollowTurnRate(Goal);

	FVector SmoothedGoal = Goal;

	CursorFollowSmoothedGoal = SmoothedGoal;
	bCursorFollowHasSmoothedGoal = true;

	AActor* FollowActor = GetOrCreateCursorFollowActor();
	if (!FollowActor)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] UpdateContinuousMoveGoal failed: no cursor follow actor."));
		return;
	}

	FollowActor->SetActorLocation(SmoothedGoal);
	UpdateCursorFollowDebugProxy(FollowActor);

	UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
	const bool bShouldStartMove = !bCursorFollowActive || (PFC && PFC->GetStatus() == EPathFollowingStatus::Idle);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bGoalMoved = LastCursorFollowRepathTime < 0.0
		|| FVector::DistSquared2D(SmoothedGoal, LastCursorFollowRepathGoal) >= FMath::Square(CursorFollowRepathDistance);
	const bool bCanRepath = LastCursorFollowRepathTime < 0.0
		|| CursorFollowRepathInterval <= 0.f
		|| (Now - LastCursorFollowRepathTime) >= CursorFollowRepathInterval;
	const bool bShouldReissueMove = bShouldStartMove || (bGoalMoved && bCanRepath);
	if (bShouldReissueMove)
	{
		UAIBlueprintHelperLibrary::SimpleMoveToActor(this, FollowActor);
		bCursorFollowActive = true;
		LastCursorFollowRepathTime = Now;
		LastCursorFollowRepathGoal = SmoothedGoal;

		if (PFC)
		{
			if (FNavPathSharedPtr Path = PFC->GetPath())
			{
				Path->SetGoalActorObservation(*FollowActor, 35.f);
			}
		}
	}

	if (!HasAuthority())
	{
		Server_UpdateCursorFollowGoal(SmoothedGoal);
	}
}

void AAeyerjiPlayerController::UpdateCursorFollowTurnRate(const FVector& DesiredGoal)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(MyPawn->GetMovementComponent());
	if (!CMC || CursorFollowTurnRateBuckets.Num() == 0)
	{
		return;
	}

	if (!bCursorFollowBucketsSorted)
	{
		CursorFollowTurnRateBuckets.Sort([](const FCursorFollowTurnRateBucket& A, const FCursorFollowTurnRateBucket& B)
		{
			return A.MaxAngleDeg < B.MaxAngleDeg;
		});
		bCursorFollowBucketsSorted = true;
	}

	FVector Forward = MyPawn->GetActorForwardVector();
	FVector ToGoal = DesiredGoal - MyPawn->GetActorLocation();
	Forward.Z = 0.f;
	ToGoal.Z = 0.f;
	if (!Forward.Normalize() || !ToGoal.Normalize())
	{
		return;
	}

	const float Dot = FMath::Clamp(FVector::DotProduct(Forward, ToGoal), -1.f, 1.f);
	const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));

	if (!bCursorFollowTurnRateActive)
	{
		SavedCursorFollowYawRate = CMC->RotationRate.Yaw;
		bCursorFollowTurnRateActive = true;
	}

	float Scalar = CursorFollowTurnRateBuckets.Last().TurnRateScalar;
	for (const FCursorFollowTurnRateBucket& Bucket : CursorFollowTurnRateBuckets)
	{
		if (AngleDeg <= Bucket.MaxAngleDeg)
		{
			Scalar = Bucket.TurnRateScalar;
			break;
		}
	}

	CMC->RotationRate.Yaw = FMath::Max(1.f, SavedCursorFollowYawRate * Scalar);
}

void AAeyerjiPlayerController::ResetCursorFollowTurnRate()
{
	if (!bCursorFollowTurnRateActive)
	{
		return;
	}

	if (APawn* MyPawn = GetPawn())
	{
		if (UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(MyPawn->GetMovementComponent()))
		{
			CMC->RotationRate.Yaw = SavedCursorFollowYawRate;
		}
	}

	bCursorFollowTurnRateActive = false;
}

void AAeyerjiPlayerController::UpdateCursorFollowDebugProxy(AActor* FollowActor)
{
	if (!FollowActor)
	{
		return;
	}

	if (!bDrawCursorFollowProxy)
	{
		if (CursorFollowDebugMesh.IsValid())
		{
			CursorFollowDebugMesh->SetHiddenInGame(true);
		}
		FollowActor->SetActorHiddenInGame(true);
		return;
	}

	FollowActor->SetActorHiddenInGame(false);
	UStaticMeshComponent* MeshComp = CursorFollowDebugMesh.Get();
	if (!MeshComp)
	{
		MeshComp = NewObject<UStaticMeshComponent>(FollowActor, TEXT("CursorFollowDebugMesh"));
		if (MeshComp)
		{
			MeshComp->SetupAttachment(FollowActor->GetRootComponent());
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComp->SetCastShadow(false);
			MeshComp->SetHiddenInGame(false);
			MeshComp->SetVisibility(true, true);
			MeshComp->SetRelativeScale3D(FVector(0.25f));

			static UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			if (SphereMesh)
			{
				MeshComp->SetStaticMesh(SphereMesh);
			}

			MeshComp->RegisterComponent();
			CursorFollowDebugMesh = MeshComp;
		}
	}
	else
	{
		MeshComp->SetHiddenInGame(false);
		MeshComp->SetVisibility(true, true);
	}
}

void AAeyerjiPlayerController::BeginCursorFollowHold(const FVector& Goal)
{
	bCursorFollowHoldPrimed = true;
	bCursorFollowHoldActive = false;
	CursorFollowHoldStartGoal = Goal;
	if (const UWorld* World = GetWorld())
	{
		CursorFollowHoldStartTime = World->GetTimeSeconds();
	}
	else
	{
		CursorFollowHoldStartTime = 0.0;
	}
}

void AAeyerjiPlayerController::ResetCursorFollowHold()
{
	bCursorFollowHoldPrimed = false;
	bCursorFollowHoldActive = false;
	CursorFollowHoldStartTime = -1.0;
	CursorFollowHoldStartGoal = FVector::ZeroVector;
}

bool AAeyerjiPlayerController::ShouldRunCursorFollowHold(const FVector& Goal)
{
	if (!bCursorFollowHoldPrimed)
	{
		return false;
	}

	if (bCursorFollowHoldActive)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bDelayMet = CursorFollowHoldStartDelay <= 0.f
		|| (CursorFollowHoldStartTime >= 0.0 && (Now - CursorFollowHoldStartTime) >= CursorFollowHoldStartDelay);
	const bool bDistanceMet = CursorFollowHoldStartDistance <= 0.f
		|| FVector::DistSquared2D(Goal, CursorFollowHoldStartGoal) >= FMath::Square(CursorFollowHoldStartDistance);
	if (bDelayMet || bDistanceMet)
	{
		bCursorFollowHoldActive = true;
		return true;
	}

	return false;
}

void AAeyerjiPlayerController::StartHoverPolling()
{
	GetWorldTimerManager().SetTimer(
		HoverTimer, this, &AAeyerjiPlayerController::PollHoverUnderCursor,
		HoverInterval, true);
	LastHoverHitTime = -1.0;
	LastEnemyHoverHitTime = -1.0;
}

void AAeyerjiPlayerController::StopHoverPolling()
{
	GetWorldTimerManager().ClearTimer(HoverTimer);
	if (HoveredLoot.IsValid()) { HoveredLoot->SetHighlighted(false); }
	if (HoveredEnemy.IsValid()) { HoveredEnemy->SetEnemyHighlighted(false); }
	HoveredLoot = nullptr;
	HoveredEnemy = nullptr;
	LastHoverHitTime = -1.0;
	LastEnemyHoverHitTime = -1.0;
}

void AAeyerjiPlayerController::PollHoverUnderCursor()
{
	FHitResult InteractHit;
	const bool bHasInteractHit = TraceCursor(ECC_GameTraceChannel1, InteractHit, /*bTraceComplex=*/false);

	AAeyerjiLootPickup* NewLoot = nullptr;
	AEnemyParentNative* NewEnemy = nullptr;

	UPrimitiveComponent* LootComponent = nullptr;
	UPrimitiveComponent* EnemyComponent = nullptr;

	if (bHasInteractHit)
	{
		if (AAeyerjiLootPickup* Candidate = Cast<AAeyerjiLootPickup>(InteractHit.GetActor()))
		{
			LootComponent = InteractHit.GetComponent();
			const bool bSelectable = Candidate->IsHoverTargetComponent(LootComponent);
			if (!bSelectable)
			{
				AJ_LOG(this, TEXT("[HoverTrace] Candidate=%s component=%s rejected"),
					*GetNameSafe(Candidate),
					LootComponent ? *LootComponent->GetName() : TEXT("None"));
			}
			if (bSelectable)
			{
				NewLoot = Candidate;
			}
		}

		if (AEnemyParentNative* EnemyCandidate = Cast<AEnemyParentNative>(InteractHit.GetActor()))
		{
			EnemyComponent = InteractHit.GetComponent();
			if (EnemyCandidate->IsHoverTargetComponent(EnemyComponent))
			{
				NewEnemy = EnemyCandidate;
			}
		}
	}

	if (!NewLoot && HoveredLoot.IsValid() && bHasInteractHit && InteractHit.GetActor() == HoveredLoot.Get())
	{
		NewLoot = HoveredLoot.Get();
		LootComponent = InteractHit.GetComponent();
	}

	if (!NewEnemy && HoveredEnemy.IsValid() && bHasInteractHit && InteractHit.GetActor() == HoveredEnemy.Get())
	{
		NewEnemy = HoveredEnemy.Get();
		EnemyComponent = InteractHit.GetComponent();
	}

	if (!NewLoot || !NewEnemy)
	{
		FHitResult VisibilityHit;
		if (TraceCursor(ECC_Visibility, VisibilityHit, /*bTraceComplex=*/false))
		{
			if (!NewLoot)
			{
				if (AAeyerjiLootPickup* Candidate = Cast<AAeyerjiLootPickup>(VisibilityHit.GetActor()))
				{
					if (Candidate->IsHoverTargetComponent(VisibilityHit.GetComponent()))
					{
						NewLoot = Candidate;
						LootComponent = VisibilityHit.GetComponent();
					}
				}
				else if (HoveredLoot.IsValid() && VisibilityHit.GetActor() == HoveredLoot.Get())
				{
					NewLoot = HoveredLoot.Get();
					LootComponent = VisibilityHit.GetComponent();
				}
			}

			if (!NewEnemy)
			{
				if (AEnemyParentNative* Candidate = Cast<AEnemyParentNative>(VisibilityHit.GetActor()))
				{
					if (Candidate->IsHoverTargetComponent(VisibilityHit.GetComponent()))
					{
						NewEnemy = Candidate;
						EnemyComponent = VisibilityHit.GetComponent();
					}
				}
				else if (HoveredEnemy.IsValid() && VisibilityHit.GetActor() == HoveredEnemy.Get())
				{
					NewEnemy = HoveredEnemy.Get();
					EnemyComponent = VisibilityHit.GetComponent();
				}
			}
		}
	}

	const UWorld* World = GetWorld();
	if (NewLoot && World)
	{
		LastHoverHitTime = World->GetTimeSeconds();
	}
	else if (!NewLoot && HoveredLoot.IsValid() && HoverReleaseGrace > 0.f && World)
	{
		const double Now = World->GetTimeSeconds();
		if (LastHoverHitTime >= 0.0 && (Now - LastHoverHitTime) <= HoverReleaseGrace)
		{
			NewLoot = HoveredLoot.Get();
		}
	}

	if (NewEnemy && World)
	{
		LastEnemyHoverHitTime = World->GetTimeSeconds();
	}
	else if (!NewEnemy && HoveredEnemy.IsValid() && EnemyHoverReleaseGrace > 0.f && World)
	{
		const double Now = World->GetTimeSeconds();
		if (LastEnemyHoverHitTime >= 0.0 && (Now - LastEnemyHoverHitTime) <= EnemyHoverReleaseGrace)
		{
			NewEnemy = HoveredEnemy.Get();
		}
	}

	if (HoveredLoot.Get() != NewLoot)
	{
		AJ_LOG(this, TEXT("[Hover:Loot] %s -> %s (Component=%s)"),
			*GetNameSafe(HoveredLoot.Get()),
			*GetNameSafe(NewLoot),
			LootComponent ? *LootComponent->GetName() : TEXT("None"));

		if (HoveredLoot.IsValid())
		{
			HoveredLoot->SetHighlighted(false); // local only
		}
		HoveredLoot = NewLoot;
		if (HoveredLoot.IsValid())
		{
			HoveredLoot->SetHighlighted(true);  // local only
		}
	}

	if (HoveredEnemy.Get() != NewEnemy)
	{
		AJ_LOG(this, TEXT("[Hover:Enemy] %s -> %s (Component=%s)"),
			*GetNameSafe(HoveredEnemy.Get()),
			*GetNameSafe(NewEnemy),
			EnemyComponent ? *EnemyComponent->GetName() : TEXT("None"));

		if (HoveredEnemy.IsValid())
		{
			HoveredEnemy->SetEnemyHighlighted(false);
		}
		HoveredEnemy = NewEnemy;
		if (HoveredEnemy.IsValid())
		{
			HoveredEnemy->SetEnemyHighlighted(true);
		}
	}
}

void AAeyerjiPlayerController::IssueMoveRPC(const FVector& Goal)
{
	if (!GetPawn())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] IssueMoveRPC ignored: no pawn."));
		return;
	}
	if (IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] IssueMoveRPC ignored: pawn dead."));
		return;
	}
    
    EnsureLocomotionRotationMode();
    
    // Rate limit RPC calls to prevent flooding the network
    const double Now = FPlatformTime::Seconds();
    // Always run client-side prediction immediately for responsiveness
    UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, Goal);

    // Then send to server for authority
    if (!HasAuthority())
    {
        ServerMoveToLocation(Goal);
        LastServerCmdTs = Now;
    }
}

void AAeyerjiPlayerController::IssueMoveRPC(AActor* Target)
{
	if (!Target)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] IssueMoveRPC target null, falling back to CachedGoal."));
		IssueMoveRPC(CachedGoal);
		return;
	}

	if (IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] IssueMoveRPC ignored: pawn dead."));
		return;
	}
	
	EnsureLocomotionRotationMode();
	
	// Rate limit RPC calls
	const double Now = FPlatformTime::Seconds();

	// Always do local prediction for responsiveness
	UAIBlueprintHelperLibrary::SimpleMoveToActor(this, Target);

	// Then send to server for authority
	if (!HasAuthority())
	{
		ServerMoveToActor(Target);
		LastServerCmdTs = Now;
	}
}

void AAeyerjiPlayerController::ServerMoveToLocation_Implementation(const FVector& Goal)
{
	// Verify we have a valid pawn before attempting to move
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation ignored: no pawn."));
		return;
	}
	if (IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation ignored: pawn dead."));
		return;
	}

	// Only accept move commands that are a meaningful distance away
	if (FVector::DistSquared(Goal, ControlledPawn->GetActorLocation()) < FMath::Square(20.f))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation ignored: goal too close."));
		return;
	}

	// Use the AI subsystem to handle pathfinding and movement
    // Execute server-side movement with immediate force
    UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, Goal);

	// Force character to update its network relevancy to ensure movement replication
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->ForceNetUpdate();
	}
}

void AAeyerjiPlayerController::ServerMoveToActor_Implementation(AActor* Target, const float AcceptanceRadius)
{
	// Verify we have a valid pawn and target before attempting to move
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor ignored: no pawn."));
		return;
	}
	if (!IsValid(Target))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor ignored: invalid target."));
		return;
	}
	if (IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor ignored: pawn dead."));
		return;
	}

	// Only accept move commands that are a meaningful distance away
	if (FVector::DistSquared(Target->GetActorLocation(), ControlledPawn->GetActorLocation()) < FMath::Square(20.f))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor ignored: target too close."));
		return;
	}

	// Use the AI subsystem to handle pathfinding and movement
	UAIBlueprintHelperLibrary::SimpleMoveToActor(this, Target);
}

void AAeyerjiPlayerController::Server_UpdateCursorFollowGoal_Implementation(const FVector& Goal)
{
	if (!GetPawn() || IsControlledPawnDead())
	{
		return;
	}

	EnsureLocomotionRotationMode();
	UpdateCursorFollowTurnRate(Goal);

	AActor* FollowActor = GetOrCreateCursorFollowActor();
	if (!FollowActor)
	{
		return;
	}

	FollowActor->SetActorLocation(Goal);

	UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
	const bool bShouldStartMove = !bCursorFollowActive || (PFC && PFC->GetStatus() == EPathFollowingStatus::Idle);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bGoalMoved = LastCursorFollowRepathTime < 0.0
		|| FVector::DistSquared2D(Goal, LastCursorFollowRepathGoal) >= FMath::Square(CursorFollowRepathDistance);
	const bool bCanRepath = LastCursorFollowRepathTime < 0.0
		|| CursorFollowRepathInterval <= 0.f
		|| (Now - LastCursorFollowRepathTime) >= CursorFollowRepathInterval;
	const bool bShouldReissueMove = bShouldStartMove || (bGoalMoved && bCanRepath);
	if (bShouldReissueMove)
	{
		UAIBlueprintHelperLibrary::SimpleMoveToActor(this, FollowActor);
		bCursorFollowActive = true;
		LastCursorFollowRepathTime = Now;
		LastCursorFollowRepathGoal = Goal;

		if (PFC)
		{
			if (FNavPathSharedPtr Path = PFC->GetPath())
			{
				Path->SetGoalActorObservation(*FollowActor, 35.f);
			}
		}
	}
}

void AAeyerjiPlayerController::Server_ResetCursorFollowTurnRate_Implementation()
{
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
}

void AAeyerjiPlayerController::Server_ApplyCursorFollowTurnRate_Implementation(const FVector& Goal)
{
	UpdateCursorFollowTurnRate(Goal);
}

void AAeyerjiPlayerController::Server_ActivateAbilityAtLocation_Implementation(const FAeyerjiAbilitySlot& AbilitySlot, FVector_NetQuantize Target)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: no ASC")); return; }

	const bool bActivated = ASC->TryActivateAbilitiesByTag(AbilitySlot.Tag, false);
	if (!bActivated && AbilitySlot.Class)
	{
		const bool bClassActivated = ASC->TryActivateAbilityByClass(AbilitySlot.Class);
		AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: TryActivateByTag failed, TryActivateByClass %s (Tag=%s Class=%s)"),
		       bClassActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString(),
		       *GetNameSafe(AbilitySlot.Class));
	}
	else
	{
		AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: TryActivateAbilitiesByTag %s (Tag=%s)"),
		       bActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString());
	}

	FGameplayAbilityTargetingLocationInfo SrcLoc; SrcLoc.LocationType = EGameplayAbilityTargetingLocationType::ActorTransform; SrcLoc.SourceActor = P;
	FGameplayAbilityTargetingLocationInfo DstLoc; DstLoc.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform; DstLoc.LiteralTransform.SetLocation(Target);
	FGameplayAbilityTargetDataHandle TDH = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromLocations(SrcLoc, DstLoc);

	FGameplayEventData Ev; Ev.EventTag = FGameplayTag::RequestGameplayTag("Event.External.Target"); Ev.Instigator = P; Ev.TargetData = TDH;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
}

void AAeyerjiPlayerController::Server_ActivateAbilityOnActor_Implementation(const FAeyerjiAbilitySlot& AbilitySlot, AActor* TargetActor)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: no ASC")); return; }

	static const FGameplayTag GravitonAbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"));
	static const FGameplayTag GravitonEventTag   = FGameplayTag::RequestGameplayTag(TEXT("Event.Ability.AG.GravitonPull"));

	// GravitonPull: drive activation via gameplay event so the ability receives target data directly.
	if (AbilitySlot.Tag.HasTag(GravitonAbilityTag))
	{
		if (!TargetActor)
		{
			AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: no target actor provided for GravitonPull"));
			return;
		}

		FGameplayEventData Ev;
		Ev.EventTag   = GravitonEventTag;
		Ev.Instigator = P;
		Ev.Target     = TargetActor;
		Ev.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(TargetActor);

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
		return;
	}

	const bool bActivated = ASC->TryActivateAbilitiesByTag(AbilitySlot.Tag, false);
	if (!bActivated && AbilitySlot.Class)
	{
		const bool bClassActivated = ASC->TryActivateAbilityByClass(AbilitySlot.Class);
		AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: TryActivateByTag failed, TryActivateByClass %s (Tag=%s Class=%s Target=%s)"),
		       bClassActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString(),
		       *GetNameSafe(AbilitySlot.Class),
		       *GetNameSafe(TargetActor));
	}
	else
	{
		AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: TryActivateAbilitiesByTag %s (Tag=%s Target=%s)"),
		       bActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString(),
		       *GetNameSafe(TargetActor));
	}

	if (!TargetActor)
	{
		return;
	}

	FGameplayAbilityTargetDataHandle TDH = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(TargetActor);

	FGameplayEventData Ev; Ev.EventTag = FGameplayTag::RequestGameplayTag("Event.External.Target"); Ev.Instigator = P; Ev.TargetData = TDH;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
}

void AAeyerjiPlayerController::Server_ActivateAbilityInstant_Implementation(const FAeyerjiAbilitySlot& AbilitySlot)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: no ASC")); return; }

	const bool bActivated = ASC->TryActivateAbilitiesByTag(AbilitySlot.Tag, false);
	if (!bActivated && AbilitySlot.Class)
	{
		const bool bClassActivated = ASC->TryActivateAbilityByClass(AbilitySlot.Class);
		AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: TryActivateByTag failed, TryActivateByClass %s (Tag=%s Class=%s)"),
		       bClassActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString(),
		       *GetNameSafe(AbilitySlot.Class));
	}
	else
	{
		AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: TryActivateAbilitiesByTag %s (Tag=%s)"),
		       bActivated ? TEXT("succeeded") : TEXT("failed"),
		       *AbilitySlot.Tag.ToString());
	}
}

// ----------------- Facing helper implementation -----------------
void AAeyerjiPlayerController::StartFaceActorAndNotify(
	AActor* Target,
	const float /*Dont give a big value like attack angle*/AcceptAngleDeg,
	const float MaxTurnRateDegPerSec,
	const float TimeoutSec,
	const bool bFireOnTimeout,
	const bool bPauseMoveLoopWhileFacing)
{
	if (!IsValid(Target) || !GetPawn())
	{
		CancelFaceActor();
		return;
	}

	// Optionally pause the chase loop so we don't slide past while turning
	bPauseMoveLoopDuringFacing = bPauseMoveLoopWhileFacing;
	if (bPauseMoveLoopDuringFacing)
	{
		StopMoveToActorLoop();
	}
	
	PushFacingRotationMode(MaxTurnRateDegPerSec);
	
	FaceTarget               = Target;
	FaceAcceptAngleDeg       = FMath::Max(0.1f, AcceptAngleDeg);
	FaceMaxTurnRateDegPerSec = FMath::Max(30.f,  MaxTurnRateDegPerSec);
	bFaceFireOnTimeout       = bFireOnTimeout;
	FaceDeadline             = GetWorld() ? (GetWorld()->GetTimeSeconds() + FMath::Max(0.f, TimeoutSec)) : 0.0;

	// Kick immediately, then run at FaceLoopInterval
	TickFaceLoop();
	GetWorldTimerManager().SetTimer(FaceLoopTimer, this,
	                                &AAeyerjiPlayerController::TickFaceLoop, FaceLoopInterval, true);
}

void AAeyerjiPlayerController::CancelFaceActor()
{
	GetWorldTimerManager().ClearTimer(FaceLoopTimer);
	FaceTarget = nullptr;
}

static float AJ_FindDeltaYawDeg(const FRotator& From, const FRotator& To)
{
	return FMath::FindDeltaAngleDegrees(From.Yaw, To.Yaw);
}

void AAeyerjiPlayerController::TickFaceLoop()
{
	APawn* MyPawn = GetPawn();
	AActor* Tgt   = FaceTarget.Get();
	if (!MyPawn || !IsValid(Tgt))
	{
		CancelFaceActor();
		return;
	}

	const FVector PawnLoc = MyPawn->GetActorLocation();
	const FVector TgtLoc  = Tgt->GetActorLocation();

	const FRotator Desired = (TgtLoc - PawnLoc).Rotation(); // yaw-only facing
	const FRotator Current = GetControlRotation();

	const float DeltaYaw = AJ_FindDeltaYawDeg(Current, Desired);
	const float AbsDelta = FMath::Abs(DeltaYaw);

	// Arrived (within tolerance)
	if (AbsDelta <= FaceAcceptAngleDeg)
	{
		SetControlRotation(FRotator(0.f, Desired.Yaw, 0.f));
		OnFacingReady.Broadcast(Tgt);
		CancelFaceActor(); // restores flags
		return;
	}

	// Timeout path (optional "good enough")
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (bFaceFireOnTimeout && Now >= FaceDeadline)
	{
		OnFacingReady.Broadcast(Tgt);
		CancelFaceActor();
		return;
	}

	// Turn step this frame
	const float Dt      = GetWorld() ? GetWorld()->GetDeltaSeconds() : FaceLoopInterval;
	const float Step    = FaceMaxTurnRateDegPerSec * Dt;         // deg this frame
	const float Clamped = FMath::Clamp(DeltaYaw, -Step, Step);
	const float NewYaw  = Current.Yaw + Clamped;

	SetControlRotation(FRotator(0.f, NewYaw, 0.f));
}

bool AAeyerjiPlayerController::ExtractCapsuleParams(const AActor* Actor, float& OutRadius, float& OutHalfHeight)
{
	OutRadius = 0.f; OutHalfHeight = 0.f;
	if (!IsValid(Actor)) return false;

	// Prefer a real capsule if present
	if (const AAeyerjiCharacter* Char = Cast<AAeyerjiCharacter>(Actor))
	{
		if (const UCapsuleComponent* Cap = Char->GetCapsuleComponent())
		{
			OutRadius = Cap->GetScaledCapsuleRadius();
			OutHalfHeight = Cap->GetScaledCapsuleHalfHeight();
			return true;
		}
	}
	if (const UCapsuleComponent* Cap = Actor->FindComponentByClass<UCapsuleComponent>())
	{
		OutRadius = Cap->GetScaledCapsuleRadius();
		OutHalfHeight = Cap->GetScaledCapsuleHalfHeight();
		return true;
	}

	// Fallback: approximate from actor bounds (works for anything with primitives)
	FVector Origin, Extents;
	Actor->GetActorBounds(/*bOnlyCollidingComponents=*/true, Origin, Extents);
	OutRadius     = FMath::Max(Extents.X, Extents.Y);
	OutHalfHeight = Extents.Z;
	return (OutRadius > 0.f);
}

// Make the character follow control yaw (temporarily)
void AAeyerjiPlayerController::PushFacingRotationMode(float DesiredYawRateDegPerSec)
{
	ACharacter* C = Cast<ACharacter>(GetPawn());
	if (!C) { return; }
	UCharacterMovementComponent* CMC = C->GetCharacterMovement();
	if (!CMC) { return; }

	// Save current
	SavedFacingMode.bValid                        = true;
	SavedFacingMode.bUseControllerRotationYaw     = C->bUseControllerRotationYaw;
	SavedFacingMode.bOrientRotationToMovement     = CMC->bOrientRotationToMovement;
	SavedFacingMode.bUseControllerDesiredRotation = CMC->bUseControllerDesiredRotation;
	SavedFacingMode.SavedRotationRateYaw          = CMC->RotationRate.Yaw;

	// Force controller-driven yaw while facing
	C->bUseControllerRotationYaw      = true;
	CMC->bOrientRotationToMovement    = false;
	CMC->bUseControllerDesiredRotation= true;
	CMC->RotationRate.Yaw             = DesiredYawRateDegPerSec;
}

void AAeyerjiPlayerController::PopFacingRotationMode()
{
	if (!SavedFacingMode.bValid) return;

	ACharacter* C = Cast<ACharacter>(GetPawn());
	if (C)
	{
		if (UCharacterMovementComponent* CMC = C->GetCharacterMovement())
		{
			CMC->bOrientRotationToMovement     = SavedFacingMode.bOrientRotationToMovement;
			CMC->bUseControllerDesiredRotation = SavedFacingMode.bUseControllerDesiredRotation;
			CMC->RotationRate.Yaw              = SavedFacingMode.SavedRotationRateYaw;
		}
		C->bUseControllerRotationYaw = SavedFacingMode.bUseControllerRotationYaw;
	}

	SavedFacingMode = {};
}

bool AAeyerjiPlayerController::AreCapsulesTouching2D(const APawn* SelfPawn, const AActor* OtherActor,
                                                     float ExtraRadiusBufferCm, float ZSlackCm)
{
	if (!IsValid(SelfPawn) || !IsValid(OtherActor)) return false;

	float R0=0.f, H0=0.f, R1=0.f, H1=0.f;
	if (!ExtractCapsuleParams(SelfPawn,  R0, H0)) return false;
	if (!ExtractCapsuleParams(OtherActor, R1, H1)) return false;

	const FVector L0 = SelfPawn->GetActorLocation();
	const FVector L1 = OtherActor->GetActorLocation();

	// If vertically far apart, don't count as touching (helpful on ramps/ledges)
	if (FMath::Abs(L0.Z - L1.Z) > (H0 + H1 + ZSlackCm))
		return false;

	const float Dist2D = FVector::Dist2D(L0, L1);
	const float TouchDist = R0 + R1 + ExtraRadiusBufferCm;
    return Dist2D <= TouchDist;
}

void AAeyerjiPlayerController::RefreshLootScalingDebug()
{

	if (HasAuthority())
	{
		RefreshLootScalingDebug_Internal();
	}
	else
	{
		ServerRefreshLootScalingDebug();
	}
}

void AAeyerjiPlayerController::ServerRefreshLootScalingDebug_Implementation()
{
	RefreshLootScalingDebug_Internal();
}

void AAeyerjiPlayerController::RefreshLootScalingDebug_Internal()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	ULootService* LootService = GameInstance ? GameInstance->GetSubsystem<ULootService>() : nullptr;
	const UAeyerjiLootTable* LootTable = LootService ? LootService->GetLootTable() : nullptr;

	if (!LootTable)
	{
		AJ_LOG(this, TEXT("RefreshLootScalingDebug aborted - LootTable missing"));
		return;
	}

	int32 InventoryUpdated = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UAeyerjiInventoryComponent*> InventoryComponents(*It);
		for (UAeyerjiInventoryComponent* Inventory : InventoryComponents)
		{
			if (Inventory)
			{
				InventoryUpdated += Inventory->DebugRefreshItemScaling(*LootTable);
			}
		}
	}

	int32 PickupUpdated = 0;
	for (TActorIterator<AAeyerjiLootPickup> It(World); It; ++It)
	{
		if (AAeyerjiLootPickup* Pickup = *It)
		{
			PickupUpdated += Pickup->DebugRefreshItemScaling(*LootTable);
		}
	}

	AJ_LOG(this, TEXT("RefreshLootScalingDebug finished - Table=%s Inventories=%d Pickups=%d"),
		*GetNameSafe(LootTable),
		InventoryUpdated,
		PickupUpdated);
}

void AAeyerjiPlayerController::ShowPopupMessage(const FText& Message, float Duration)
{
	AJ_LOG(this, TEXT("ShowPopupMessage: %s"), *Message.ToString());
	BP_ShowPopupMessage(Message, Duration);
}

// --- Short-range local avoidance ---
bool AAeyerjiPlayerController::AdjustGoalForShortAvoidance(FVector& InOutGoal)
{
    if (!bEnableShortAvoidance)
        return false;

    APawn* MyPawn = GetPawn();
    if (!MyPawn)
        return false;

    UWorld* World = GetWorld();
    if (!World)
        return false;

    // If currently holding a sidestep goal, keep it until time elapses
    const double Now = World->GetTimeSeconds();
    // Cooldown to prevent thrashing
    if (LastAvoidanceTriggerTime > 0.0 && (Now - LastAvoidanceTriggerTime) < AvoidanceMinTimeBetweenTriggers)
    {
        return false;
    }
    if (bAvoidanceActive)
    {
        if (Now < AvoidanceEndTime && !ActiveAvoidanceGoal.IsNearlyZero())
        {
            InOutGoal = ActiveAvoidanceGoal;
            return true;
        }
        bAvoidanceActive = false;
        ActiveAvoidanceGoal = FVector::ZeroVector;
        AvoidanceEndTime = 0.0;
    }

    const FVector PawnLoc = MyPawn->GetActorLocation();
    FVector DesiredDir = (InOutGoal - PawnLoc);
    DesiredDir.Z = 0.f;
    if (!DesiredDir.Normalize())
        return false;

    // Skip avoidance if already close to final goal
    if (FVector::Dist2D(PawnLoc, InOutGoal) <= AvoidanceMinDistanceToGoal)
    {
        return false;
    }

    // Skip avoidance if moving too slowly
    if (const UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(MyPawn->GetMovementComponent()))
    {
        if (CMC->Velocity.Size2D() < AvoidanceMinSpeedCmPerSec)
        {
            return false;
        }
    }

    // Sweep ahead for a pawn blocking the immediate path
    float CapRadius = 34.f, CapHalfHeight = 88.f; // sensible defaults
    ExtractCapsuleParams(MyPawn, CapRadius, CapHalfHeight);
    const float SweepRadius = FMath::Max(20.f, CapRadius * AvoidanceProbeRadiusScale);
    const float SweepDist   = FMath::Max(60.f,  AvoidanceProbeDistance);

    const FVector Start = PawnLoc + DesiredDir * (CapRadius * 0.5f);
    const FVector End   = Start   + DesiredDir * SweepDist;

    FCollisionObjectQueryParams ObjParams;
    ObjParams.AddObjectTypesToQuery(ECC_Pawn);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShortAvoidance), /*bTraceComplex=*/false);
    QueryParams.AddIgnoredActor(MyPawn);

    FHitResult Hit;
    const bool bHit = World->SweepSingleByObjectType(
        Hit,
        Start, End,
        FQuat::Identity,
        ObjParams,
        FCollisionShape::MakeSphere(SweepRadius),
        QueryParams);

    if (!bHit || !Hit.GetActor())
        return false;

    AActor* BlockingActor = Hit.GetActor();
    const AActor* CurrentTargetActor = CachedTarget.Get();

    // If the blocking pawn is exactly our current target, optionally skip avoidance
    if (BlockingActor == CurrentTargetActor && bSkipAvoidanceWhenBlockingIsCurrentTarget)
    {
        return false;
    }

    // Choose a lateral sidestep (left/right) that is clear
    FVector BasisDir = DesiredDir;
    if (BlockingActor == CurrentTargetActor && bBiasDetourAroundTargetTangent)
    {
        // Bias around the target's tangent instead of our world-path direction
        const FVector ToTarget = (CurrentTargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
        if (!ToTarget.IsNearlyZero())
        {
            BasisDir = ToTarget;
        }
    }
    const FVector Right = FVector::CrossProduct(BasisDir, FVector::UpVector).GetSafeNormal();
    const FVector Left  = -Right;

    const float SideDist = FMath::Max(80.f, AvoidanceSideStepDistance);
    const FVector CandidateL = PawnLoc + Left  * SideDist;
    const FVector CandidateR = PawnLoc + Right * SideDist;

    auto IsPathClear = [&](const FVector& A, const FVector& B) -> bool
    {
        FHitResult Tmp;
        return !World->SweepSingleByObjectType(
            Tmp,
            A, B,
            FQuat::Identity,
            ObjParams,
            FCollisionShape::MakeSphere(SweepRadius),
            QueryParams);
    };

    // Prefer the clearer side; if both clear, bias randomly for variety
    const bool bLeftClear  = IsPathClear(Start, CandidateL);
    const bool bRightClear = IsPathClear(Start, CandidateR);

    FVector Chosen = CandidateR;
    if (bLeftClear != bRightClear)
    {
        Chosen = bLeftClear ? CandidateL : CandidateR;
    }
    else if (bLeftClear && bRightClear)
    {
        Chosen = (FMath::RandBool()) ? CandidateL : CandidateR;
    }
    else
    {
        // Both blocked: nudge slightly to the less-penetrating side using hit normal
        const FVector Nudge = FVector::VectorPlaneProject(Hit.ImpactNormal, FVector::UpVector).GetSafeNormal();
        if (!Nudge.IsNearlyZero())
        {
            Chosen = PawnLoc + Nudge * (SideDist * AvoidanceBlockedNudgeScale);
        }
    }

    // Safety guards: avoid extreme turns and long detours
    const FVector ToGoal    = (InOutGoal - PawnLoc).GetSafeNormal2D();
    const FVector ToChosen  = (Chosen    - PawnLoc).GetSafeNormal2D();
    const float TurnAngle   = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ToGoal, ToChosen), -1.f, 1.f)));
    if (TurnAngle > AvoidanceMaxDetourAngleDeg)
    {
        return false; // too sharp of a detour
    }

    const float DistToGoal   = FVector::Dist2D(PawnLoc, InOutGoal);
    const float DistToChosen = FVector::Dist2D(PawnLoc, Chosen);
    if (DistToGoal > KINDA_SMALL_NUMBER && (DistToChosen > DistToGoal * AvoidanceMaxGoalDistanceFactor))
    {
        return false; // would increase path length too much
    }

    // Project to navmesh to keep it valid (optional, best-effort)
    if (bAvoidanceProjectToNavmesh)
    {
        if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            FNavLocation Projected;
            if (Nav->ProjectPointToNavigation(Chosen, Projected, NavProjectExtents))
            {
                Chosen = Projected.Location;
            }
        }
    }

    if (bAvoidanceDebugDraw)
    {
        DrawDebugLine(World, Start, End, FColor::Yellow, false, 0.25f, 0, 1.5f);
        DrawDebugSphere(World, Hit.ImpactPoint, SweepRadius * 0.6f, 12, FColor::Red, false, 0.25f);
        DrawDebugLine(World, PawnLoc, Chosen, FColor::Cyan, false, 0.5f, 0, 2.f);
        DrawDebugSphere(World, Chosen, 12.f, 8, FColor::Cyan, false, 0.5f);
    }

    // Arm the avoidance hold window and override the goal
    const float Hold = FMath::FRandRange(AvoidanceHoldTimeMin, AvoidanceHoldTimeMax);
    bAvoidanceActive     = true;
    ActiveAvoidanceGoal  = Chosen;
    AvoidanceEndTime     = Now + Hold;
    LastAvoidanceTriggerTime = Now;
    InOutGoal            = Chosen;

    return true;
}
