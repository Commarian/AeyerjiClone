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
#include "AeyerjiCharacter.h"
#include "AeyerjiCharacterMovementComponent.h"
#include "Enemy/EnemyParentNative.h"
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
#include "MouseNavBlueprintLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/Blink/GABlink.h"
#include "Abilities/Blink/DA_Blink.h"
#include "CharacterStatsLibrary.h"
#include "UObject/UnrealType.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "Attributes/AeyerjiAttributeSet.h"

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

	LoadIfNull(IMC_Default, TEXT("/Game/Player/Input/IMC_Default.IMC_Default"));
	LoadIfNull(IA_Attack_Click,    TEXT("/Game/Player/Input/Actions/IA_Attack_Click.IA_Attack_Click"));
	LoadIfNull(IA_Move_Click,    TEXT("/Game/Player/Input/Actions/IA_Move_Click.IA_Move_Click"));
	LoadIfNull(FX_Cursor,   TEXT("/Game/Cursor/FX_Cursor.FX_Cursor"));
	LoadIfNull(IA_ShowLoot,   TEXT("/Game/Player/Input/Actions/IA_ShowLoot.IA_ShowLoot"));

	bAutoManageActiveCameraTarget = false;
}
void AAeyerjiPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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

	FNavLocation Projected;
	const FVector PrimaryExtent(Radius, Radius, 600.f);

	auto FinalizeGoal = [&](const FVector& NavPoint)
	{
		FVector Goal = NavPoint;
		const FVector ToCenter = (Center - Goal).GetSafeNormal2D();
		if (!ToCenter.IsNearlyZero())
		{
			const float DesiredDepth = FMath::Clamp(Radius * 0.35f, 15.f, PickupAcceptRadius * 0.9f);
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

	const float D2 = FVector::DistSquared2D(P->GetActorLocation(), PendingPickup->GetActorLocation());
	if (D2 < PickupAcceptRadius)
	{
		AJ_LOG(this, TEXT("[PC] Close enough - calling Server_Pickup"));
		AbortMovement_Both();
		if (PendingPickup.IsValid())
		{
			Server_RequestPickup(PendingPickup->GetFName());
		}
		StopPendingPickup();
	}
}

void AAeyerjiPlayerController::OnShowLootPressed()
{
	UAeyerjiInventoryBPFL::SetAllLootLabelsVisible(this, true );
}

void AAeyerjiPlayerController::OnShowLootReleased()
{
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
	PendingSlot = Slot;
	StopAbilityRangePreview();
	switch (Slot.TargetMode)
	{
	case EAeyerjiTargetMode::GroundLocation: CastFlow = ECastFlow::AwaitingGround;  break;
	case EAeyerjiTargetMode::EnemyActor:     CastFlow = ECastFlow::AwaitingEnemy;   break;
	case EAeyerjiTargetMode::FriendlyActor:  CastFlow = ECastFlow::AwaitingFriend;  break;
	default:                                 CastFlow = ECastFlow::Normal;          return;
	}
	StartAbilityRangePreview(Slot);
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
    if (IsLocalController() && IMC_Default)
    {
        StartHoverPolling();
        if (auto* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
        {
            Sub->AddMappingContext(IMC_Default, 0);
        }
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
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Triggered, this, &AAeyerjiPlayerController::OnAttackClickPressed);
		}
		if (IA_Move_Click)
		{
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnMoveClickPressed);
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Triggered, this, &AAeyerjiPlayerController::OnMoveClickPressed);
		}
		if (IA_ShowLoot)
		{
			EIC->BindAction(IA_ShowLoot, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnShowLootPressed);
			EIC->BindAction(IA_ShowLoot, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnShowLootReleased);
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
	
	// Give BP a chance to consume ANY click if really needed
	const bool ConsumedByClickBP = OnClick();
	if (ConsumedByClickBP)
	{
		return;
	}
	
	// If we are in a special casting state (e.g., AwaitingGround), handle it and consume the click
	if (HandleCastFlowClick())
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
			return; // BP chose to consume this click
		}
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
	MoveToGroundFromHit(SurfaceHit);
}

void AAeyerjiPlayerController::OnMoveClickPressed(const FInputActionValue& /*Val*/)
{
	if (HandleMovementBlockedByAbilities())
	{
		return;
	}

	// Additional move-only reset: clear targeting/casting, clear any pickup intent, clear pending move goal
	ResetForMoveOnly();

	// Move-only: we just need a ground point
	FHitResult SurfaceHit;
	const bool HasSurfaceHit = TryGetGroundHit(SurfaceHit);
	if (!HasSurfaceHit)
	{
		return;
	}

	// Move-only: go to the ground location; do not set a target from the hit actor
	MoveToGroundFromHit(SurfaceHit);
}

void AAeyerjiPlayerController::HandleMoveCommand()
{
	// ---------- Gather context ----------
	if (!GetPawn())
	{
		return;
	}

	const bool bHasTarget = CachedTarget.IsValid();

	// Where are we trying to go? If there’s a target, use its location; else the ground point.
	const FVector TargetLocation = bHasTarget
		                               ? CachedTarget->GetActorLocation()
		                               : CachedGoal;

	// Distance-squared (cheap) from pawn to the chosen target location.
	const float DistSqToTarget = FVector::DistSquared(GetPawn()->GetActorLocation(), TargetLocation);

	// Thresholds precomputed and named for readability.
	const float MinMoveDistSq = FMath::Square(MinMoveDistanceCm);
	const bool  bFarEnough    = (DistSqToTarget >= MinMoveDistSq);

	// ---------- Early out if too close ----------
	if (!bFarEnough)
	{
		// We are already close enough; don’t spam move/attack.
		return;
	}
	
	// ---------- Command dispatch ----------
	// Prefer actor-targeted move/attack if we have a valid attackable target.

	if (bHasTarget && IsAttackableActor(CachedTarget.Get()))
	{
		// Overload: AActor*
		IssueMoveRPC(CachedTarget.Get());
	}
	else
	{
		// Overload: FVector
		IssueMoveRPC(TargetLocation);
		SpawnCursorFX(TargetLocation);
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
}

void AAeyerjiPlayerController::ResetForMoveOnly()
{
	CancelFaceActor();
	ClearPickupIntentIfAny();
	EnsureLocomotionRotationMode();
	ClearTargeting();
	PendingMoveGoal = FVector::ZeroVector;
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

bool AAeyerjiPlayerController::TraceCursor(ECollisionChannel Channel, FHitResult& OutHit, bool bTraceComplex) const
{
	return GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(Channel), bTraceComplex, OutHit);
}

bool AAeyerjiPlayerController::TryGetGroundHit(FHitResult& OutHit) const
{
	return TraceCursor(ECC_GameTraceChannel2, OutHit, /*bTraceComplex=*/false);
}

bool AAeyerjiPlayerController::TryGetPawnHit(FHitResult& OutHit) const
{
	const bool bHit = TraceCursor(ECC_GameTraceChannel3, OutHit, /*bTraceComplex=*/false);
	if (!bHit)
	{
		return false;
	}

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
					return true;
				}
			}
		}
	}

	return bHit;
}

bool AAeyerjiPlayerController::TryGetLootHit(FHitResult& OutHit) const
{
	if (!TraceCursor(ECC_GameTraceChannel1, OutHit, /*bTraceComplex=*/false))
	{
		return false;
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

void AAeyerjiPlayerController::StartAbilityRangePreview(const FAeyerjiAbilitySlot& Slot)
{
	if (!IsLocalController())
	{
		return;
	}

	const float Range = ResolveAbilityPreviewRange(Slot);
	if (Range <= KINDA_SMALL_NUMBER)
	{
		StopAbilityRangePreview();
		return;
	}

	AbilityRangePreview.bActive = true;
	AbilityRangePreview.Range = Range;
	AbilityRangePreview.Mode = Slot.TargetMode;

	// Draw once immediately, then keep refreshing on a short cadence.
	DrawAbilityRangePreview(Range, Slot.TargetMode);
	const float PreviewTickRate = 0.05f; // 20 Hz feels responsive enough for cursor movement
	GetWorldTimerManager().SetTimer(
		AbilityRangePreviewTimer, this,
		&AAeyerjiPlayerController::TickAbilityRangePreview,
		PreviewTickRate, true);
}

void AAeyerjiPlayerController::StopAbilityRangePreview()
{
	AbilityRangePreview = {};
	GetWorldTimerManager().ClearTimer(AbilityRangePreviewTimer);
}

void AAeyerjiPlayerController::TickAbilityRangePreview()
{
	if (!AbilityRangePreview.bActive || CastFlow == ECastFlow::Normal)
	{
		StopAbilityRangePreview();
		return;
	}

	DrawAbilityRangePreview(AbilityRangePreview.Range, AbilityRangePreview.Mode);
}

float AAeyerjiPlayerController::ResolveAbilityPreviewRange(const FAeyerjiAbilitySlot& Slot) const
{
	float Range = 0.f;

	if (Slot.Class)
	{
		if (const UGameplayAbility* AbilityCDO = Slot.Class->GetDefaultObject<UGameplayAbility>())
		{
			// Preferred path: if the ability exposes a DA_Blink property, evaluate it using ASC level.
			if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
			{
				const FObjectProperty* BlinkDAProp = FindFProperty<FObjectProperty>(Slot.Class, TEXT("BlinkConfig"));
				if (!BlinkDAProp)
				{
					// Look for any UDA_Blink property as a fallback
					for (TFieldIterator<FObjectProperty> It(Slot.Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
					{
						if (It->PropertyClass && It->PropertyClass->IsChildOf(UDA_Blink::StaticClass()))
						{
							BlinkDAProp = *It;
							break;
						}
					}
				}

				if (BlinkDAProp && BlinkDAProp->PropertyClass->IsChildOf(UDA_Blink::StaticClass()))
				{
					if (const UDA_Blink* BlinkDA = Cast<UDA_Blink>(BlinkDAProp->GetObjectPropertyValue_InContainer(AbilityCDO)))
					{
						float DAValue = BlinkDA->Tunables.MaxRange;

						const FRichCurve* Curve = BlinkDA->Tunables.RangeByLevel.GetRichCurveConst();
						if (Curve && Curve->GetNumKeys() > 0)
						{
							const float Level = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute());
							DAValue = Curve->Eval(Level, BlinkDA->Tunables.MaxRange);
						}

						DAValue *= FMath::Max(0.0f, BlinkDA->Tunables.RangeScalar);
						Range = FMath::Max(Range, DAValue);
					}
				}
			}

			if (const UGABlink* BlinkCDO = Cast<UGABlink>(AbilityCDO))
			{
				if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
				{
					Range = FMath::Max(Range, BlinkCDO->GetMaxBlinkRange(ASC));
				}
				else
				{
					Range = FMath::Max(Range, BlinkCDO->GetMaxBlinkRange(nullptr));
				}
			}

			auto TryReadFloatProperty = [&](const TCHAR* PropName)
			{
				if (const FProperty* Prop = Slot.Class->FindPropertyByName(PropName))
				{
					if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
					{
						Range = FMath::Max(Range, FloatProp->GetFloatingPointPropertyValue(FloatProp->ContainerPtrToValuePtr<void>(AbilityCDO)));
					}
				}
			};

			// Common property names we use in blink/data-asset driven abilities
			TryReadFloatProperty(TEXT("MaxBlinkDistance"));
			TryReadFloatProperty(TEXT("MaxRange"));
			TryReadFloatProperty(TEXT("Range"));
			TryReadFloatProperty(TEXT("DefaultBlinkRange")); // BP-only blink fallback
			TryReadFloatProperty(TEXT("BlinkRange"));
			TryReadFloatProperty(TEXT("BlinkDistance"));
		}
	}

	// Fallback: use attack range attribute (keeps something visible for other ability types)
	if (Range <= 0.f)
	{
		if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
		{
			if (const UAttributeSet_Ranges* RangeSet = ASC->GetSet<UAttributeSet_Ranges>())
			{
				Range = FMath::Max(Range, RangeSet->GetBlinkRange());
			}
		}

		if (Range <= 0.f)
		{
			if (APawn* LocalPawn = GetPawn())
			{
				Range = UCharacterStatsLibrary::GetAttackRangeFromActorASC(LocalPawn, /*FallbackRange=*/600.f);
			}
		}
	}

	return FMath::Max(0.f, Range);
}

void AAeyerjiPlayerController::DrawAbilityRangePreview(float Range, EAeyerjiTargetMode Mode)
{
	if (!IsLocalController() || Range <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	UWorld* World = GetWorld();
	APawn* LocalPawn = GetPawn();
	if (!World || !LocalPawn)
	{
		return;
	}

	FVector Center = LocalPawn->GetActorLocation();
	FHitResult GroundHit;
	if (TryGetGroundHit(GroundHit))
	{
		Center.Z = GroundHit.ImpactPoint.Z + 2.f;
	}

	const FColor Color =
		(Mode == EAeyerjiTargetMode::GroundLocation) ? FColor::Purple :
		(Mode == EAeyerjiTargetMode::EnemyActor)     ? FColor::Red :
		(Mode == EAeyerjiTargetMode::FriendlyActor)  ? FColor::Green :
		                                              FColor::Silver;

	constexpr int32 Segments = 64;
	constexpr float Thickness = 2.5f;
	constexpr float Life = 0.06f; // slightly longer than the tick to avoid flicker

	DrawDebugCircle(World, Center, Range, Segments, Color, false, Life, 0, Thickness, FVector(1, 0, 0), FVector(0, 1, 0), false);
}

bool AAeyerjiPlayerController::HandleCastFlowClick()
{
	if (CastFlow == ECastFlow::Normal)
	{
		return false;
	}

	AJ_LOG(this, TEXT("HandleCastFlowClick() CastFlow=%d"), static_cast<int32>(CastFlow));

	FHitResult Hit;
	if (TryGetGroundHit(Hit))
	{
		AJ_LOG(this, TEXT("HandleCastFlowClick() ground hit at %s"), *Hit.ImpactPoint.ToString());
		if (CastFlow == ECastFlow::AwaitingGround)
		{
			Server_ActivateAbilityAtLocation(PendingSlot, FVector_NetQuantize(Hit.ImpactPoint));
		}
	}
	else
	{
		AJ_LOG(this, TEXT("HandleCastFlowClick() no ground hit"));
	}

	// Reset cast state regardless
	CastFlow = ECastFlow::Normal;
	PendingSlot = {};
	StopAbilityRangePreview();
	return true; // consumed the click
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
	CastFlow = ECastFlow::Normal;
	PendingSlot = {};
	StopAbilityRangePreview();
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

	const float D2 = FVector::DistSquared2D(P->GetActorLocation(), Loot->GetActorLocation());
	
	if (D2 < PickupAcceptRadius)
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

void AAeyerjiPlayerController::MoveToGroundFromHit(const FHitResult& SurfaceHit)
{
	CachedGoal = SurfaceHit.ImpactPoint;
	HandleMoveCommand();
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
	const bool bHasInteractHit = GetHitResultUnderCursorByChannel(
		UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1), false, InteractHit);

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
		if (GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, VisibilityHit))
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
        return;
    
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
		IssueMoveRPC(CachedGoal);
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
    if (!ControlledPawn) return;

    // Only accept move commands that are a meaningful distance away
    if (FVector::DistSquared(Goal, ControlledPawn->GetActorLocation()) < FMath::Square(20.f)) return;

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
	if (!ControlledPawn || !IsValid(Target)) return;

	// Only accept move commands that are a meaningful distance away
	if (FVector::DistSquared(Target->GetActorLocation(), ControlledPawn->GetActorLocation()) < FMath::Square(20.f)) return;

	// Use the AI subsystem to handle pathfinding and movement
	UAIBlueprintHelperLibrary::SimpleMoveToActor(this, Target);
}

void AAeyerjiPlayerController::Server_ActivateAbilityAtLocation_Implementation(const FAeyerjiAbilitySlot& AbilitySlot, FVector_NetQuantize Target)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: no ASC")); return; }

	const bool bActivated = ASC->TryActivateAbilitiesByTag(AbilitySlot.Tag, false);
	AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: TryActivateAbilitiesByTag %s (Tag=%s)"), bActivated ? TEXT("succeeded") : TEXT("failed"), *AbilitySlot.Tag.ToString());

	FGameplayAbilityTargetingLocationInfo SrcLoc; SrcLoc.LocationType = EGameplayAbilityTargetingLocationType::ActorTransform; SrcLoc.SourceActor = P;
	FGameplayAbilityTargetingLocationInfo DstLoc; DstLoc.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform; DstLoc.LiteralTransform.SetLocation(Target);
	FGameplayAbilityTargetDataHandle TDH = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromLocations(SrcLoc, DstLoc);

	FGameplayEventData Ev; Ev.EventTag = FGameplayTag::RequestGameplayTag("Event.External.Target"); Ev.Instigator = P; Ev.TargetData = TDH;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
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



