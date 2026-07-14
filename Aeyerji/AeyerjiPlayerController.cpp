// ===============================
// File: AeyerjiPlayerController.cpp
// ===============================
// ReSharper disable CppTooWideScopeInitStatement
// ReSharper disable CppTooWideScope
#include "AeyerjiPlayerController.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Interaction/AeyerjiInteractable.h"
#include "World/AeyerjiLinkedTeleporter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiGameState.h"
#include "AeyerjiGameInstance.h"
#include "AeyerjiPlayerState.h"
#include "AeyerjiCharacterMovementComponent.h"
#include "Enemy/EnemyParentNative.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "CharacterStatsLibrary.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiRiftRules.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericTeamAgentInterface.h"
#include "HAL/PlatformApplicationMisc.h"
#include "NavigationSystem.h"
#include "Engine/LocalPlayer.h"
#include "NiagaraFunctionLibrary.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Blink/GABlink.h"
#include "Components/CapsuleComponent.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "AeyerjiGameplayTags.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Logging/AeyerjiLog.h"
#include "Items/InventoryComponent.h"
#include "MouseNavBlueprintLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "GUI/W_InventoryBag_Native.h"
#include "EngineUtils.h"
#include "Player/PlayerParentNative.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/TargetPoint.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AeyerjiCameraOcclusionFadeComponent.h"
#include "Components/AeyerjiNavSafetyComponent.h"
#include "Components/AeyerjiViewDistanceCullComponent.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "GUI/W_EquipmentSlot.h"
#include "GUI/W_EndRunScreen.h"
#include "GUI/W_ItemTile.h"
#include "GUI/W_AeyerjiMissionHUD.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"

template <class TAsset>
static void LoadIfNull(TObjectPtr<TAsset>& Dest, const TCHAR* AssetPath)
{
	if (!Dest)
	{
		ConstructorHelpers::FObjectFinder<TAsset> Finder(AssetPath);
		if (Finder.Succeeded())
		{
			Dest = Finder.Object;
		}
	}
}

namespace
{
	EWindowMode::Type ResolveWindowModeFromIndex(const int32 WindowMode)
	{
		switch (WindowMode)
		{
		case 0:
			return EWindowMode::Fullscreen;
		case 2:
			return EWindowMode::Windowed;
		default:
			return EWindowMode::WindowedFullscreen;
		}
	}

	const TCHAR* GetWindowModeLabel(const EWindowMode::Type WindowMode)
	{
		switch (WindowMode)
		{
		case EWindowMode::Fullscreen:
			return TEXT("Fullscreen");
		case EWindowMode::WindowedFullscreen:
			return TEXT("WindowedFullscreen");
		case EWindowMode::Windowed:
			return TEXT("Windowed");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* BoolText(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	const TCHAR* MoveLoopModeText(const EAeyerjiMoveLoopMode Mode)
	{
		switch (Mode)
		{
		case EAeyerjiMoveLoopMode::StopOnly:
			return TEXT("StopOnly");
		case EAeyerjiMoveLoopMode::FollowOnly:
			return TEXT("FollowOnly");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* PathStatusText(const EPathFollowingStatus::Type Status)
	{
		switch (Status)
		{
		case EPathFollowingStatus::Idle:
			return TEXT("Idle");
		case EPathFollowingStatus::Waiting:
			return TEXT("Waiting");
		case EPathFollowingStatus::Paused:
			return TEXT("Paused");
		case EPathFollowingStatus::Moving:
			return TEXT("Moving");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* MovementModeText(const EMovementMode Mode)
	{
		switch (Mode)
		{
		case MOVE_None:
			return TEXT("None");
		case MOVE_Walking:
			return TEXT("Walking");
		case MOVE_NavWalking:
			return TEXT("NavWalking");
		case MOVE_Falling:
			return TEXT("Falling");
		case MOVE_Swimming:
			return TEXT("Swimming");
		case MOVE_Flying:
			return TEXT("Flying");
		case MOVE_Custom:
			return TEXT("Custom");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* NetRoleText(const ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_None:
			return TEXT("None");
		case ROLE_SimulatedProxy:
			return TEXT("SimulatedProxy");
		case ROLE_AutonomousProxy:
			return TEXT("AutonomousProxy");
		case ROLE_Authority:
			return TEXT("Authority");
		default:
			return TEXT("Unknown");
		}
	}

	FString DescribePathFollowing(const UPathFollowingComponent* PFC)
	{
		if (!PFC)
		{
			return TEXT("PFC=None");
		}

		const FNavPathSharedPtr Path = PFC->GetPath();
		return FString::Printf(
			TEXT("PFC=%s Active=%s Tick=%s Path=%s Points=%d"),
			PathStatusText(PFC->GetStatus()),
			BoolText(PFC->IsActive()),
			BoolText(PFC->PrimaryComponentTick.IsTickFunctionEnabled()),
			Path.IsValid() ? TEXT("Valid") : TEXT("None"),
			Path.IsValid() ? Path->GetPathPoints().Num() : 0);
	}

	FString DescribePawnMovement(const APawn* Pawn)
	{
		if (!Pawn)
		{
			return TEXT("Pawn=None");
		}

		const UCharacterMovementComponent* CMC = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent());
		return FString::Printf(
			TEXT("Pawn=%s Role=%s Loc=%s Vel=%s CMC=%s MoveMode=%s DesiredVel=%s"),
			*GetNameSafe(Pawn),
			NetRoleText(Pawn->GetLocalRole()),
			*Pawn->GetActorLocation().ToCompactString(),
			*Pawn->GetVelocity().ToCompactString(),
			CMC ? TEXT("Valid") : TEXT("None"),
			CMC ? MovementModeText(CMC->MovementMode) : TEXT("None"),
			CMC ? *CMC->GetCurrentAcceleration().ToCompactString() : TEXT("None"));
	}

	bool BuildSyntheticCursorHit(const AActor* TargetActor, const FVector& CursorLocation, FHitResult& OutHit)
	{
		if (!IsValid(TargetActor))
		{
			return false;
		}

		OutHit = FHitResult();
		OutHit.HitObjectHandle = FActorInstanceHandle(const_cast<AActor*>(TargetActor));
		OutHit.Component = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent());
		OutHit.bBlockingHit = true;
		OutHit.Location = TargetActor->GetActorLocation();
		OutHit.ImpactPoint = CursorLocation;
		OutHit.TraceStart = CursorLocation;
		OutHit.TraceEnd = CursorLocation;
		return true;
	}

	bool IsCursorNearActorScreenLocation(const APlayerController* PlayerController, const AActor* TargetActor, const float RadiusPx)
	{
		if (!IsValid(PlayerController) || !IsValid(TargetActor) || RadiusPx <= 0.f)
		{
			return false;
		}

		float MouseX = 0.f;
		float MouseY = 0.f;
		if (!PlayerController->GetMousePosition(MouseX, MouseY))
		{
			return false;
		}

		FVector2D TargetScreenPos;
		if (!PlayerController->ProjectWorldLocationToScreen(TargetActor->GetActorLocation(), TargetScreenPos))
		{
			return false;
		}

		return FVector2D::DistSquared(TargetScreenPos, FVector2D(MouseX, MouseY)) <= FMath::Square(RadiusPx);
	}

	bool AbilityUsesExternalTargetEvent(const TSubclassOf<UGameplayAbility>& AbilityClass)
	{
		return AbilityClass
			&& (AbilityClass->IsChildOf(UGA_AeyerjiTargetedEffectBase::StaticClass())
				|| AbilityClass->IsChildOf(UGABlink::StaticClass()));
	}

	bool TryActivateAbilitySlotDirectly(UAbilitySystemComponent* ASC, const FAeyerjiAbilitySlot& AbilitySlot, const UObject* LogContext, const TCHAR* ContextLabel)
	{
		if (!ASC)
		{
			return false;
		}

		const bool bActivatedByTag = ASC->TryActivateAbilitiesByTag(AbilitySlot.Tag, false);
		if (!bActivatedByTag && AbilitySlot.Class)
		{
			const bool bActivatedByClass = ASC->TryActivateAbilityByClass(AbilitySlot.Class);
			AJ_LOG(LogContext, TEXT("%s: TryActivateByTag failed, TryActivateByClass %s (Tag=%s Class=%s)"),
				ContextLabel,
				bActivatedByClass ? TEXT("succeeded") : TEXT("failed"),
				*AbilitySlot.Tag.ToString(),
				*GetNameSafe(AbilitySlot.Class));
			return bActivatedByClass;
		}

		AJ_LOG(LogContext, TEXT("%s: TryActivateAbilitiesByTag %s (Tag=%s)"),
			ContextLabel,
			bActivatedByTag ? TEXT("succeeded") : TEXT("failed"),
			*AbilitySlot.Tag.ToString());
		return bActivatedByTag;
	}
}


AAeyerjiPlayerController::AAeyerjiPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = true;
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	CameraOcclusionFade = CreateDefaultSubobject<UAeyerjiCameraOcclusionFadeComponent>(TEXT("CameraOcclusionFade"));
	ViewDistanceCull = CreateDefaultSubobject<UAeyerjiViewDistanceCullComponent>(TEXT("ViewDistanceCull"));

	LoadIfNull(IMC_Default, TEXT("/Game/Player/Input/IMC_Default.IMC_Default"));
	LoadIfNull(IA_Attack_Click,    TEXT("/Game/Player/Input/Actions/IA_Attack_Click.IA_Attack_Click"));
	LoadIfNull(IA_Move_Click,    TEXT("/Game/Player/Input/Actions/IA_Move_Click.IA_Move_Click"));
	LoadIfNull(IA_Interact, TEXT("/Game/Player/Input/Actions/IA_Interact.IA_Interact"));
	LoadIfNull(FX_Cursor,   TEXT("/Game/Cursor/FX_Cursor.FX_Cursor"));
	LoadIfNull(IA_ShowLoot,   TEXT("/Game/Player/Input/Actions/IA_ShowLoot.IA_ShowLoot"));
	LoadIfNull(IA_DropItem, TEXT("/Game/Player/Input/Actions/IA_DropItem.IA_DropItem"));
	LoadIfNull(IA_CancelAction, TEXT("/Game/Player/Input/Actions/IA_CancelAction.IA_CancelAction"));

	static ConstructorHelpers::FClassFinder<UUserWidget> MainMenuWidgetBPClass(TEXT("/Game/GUI/MainMenu/W_MainMenu"));
	if (MainMenuWidgetBPClass.Succeeded())
	{
		MainMenuWidgetClass = MainMenuWidgetBPClass.Class;
	}

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
	UpdatePrimaryMeleeRotationLock();

	if (!IsLocalController())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	RefreshExtractionCountdownState();
	if (!MouseCommandRecoveryASC.IsValid())
	{
		BindMouseCommandRecoveryDelegates();
	}

	if (bHasQueuedMovementCommand && !IsAbilityCastInputLocked())
	{
		FlushQueuedMovementCommandIfAllowed();
	}

	if (!bMoveClickHeld && WasInputKeyJustPressed(MoveClickPhysicalKey))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Move press recovered by physical key poll; Key=%s IA_Move_Click Started was not received first."),
			*MoveClickPhysicalKey.ToString());
		OnMoveClickPressed(FInputActionValue());
	}

	if (!bAttackClickHeld && WasInputKeyJustPressed(AttackClickPhysicalKey))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Left/action press recovered by physical key poll; Key=%s IA_Attack_Click Started was not received first."),
			*AttackClickPhysicalKey.ToString());
		OnAttackClickPressed(FInputActionValue());
	}

	static double LastMovePhysicalDownDiagTime = -1.0;
	if (!bMoveClickHeld && IsInputKeyDown(MoveClickPhysicalKey))
	{
		const double Now = World->GetTimeSeconds();
		if (LastMovePhysicalDownDiagTime < 0.0 || (Now - LastMovePhysicalDownDiagTime) >= 0.5)
		{
			UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Move physical key is down, but IA_Move_Click hold is not active. Key=%s JustPressed=%s Suppressed=%s IA_Move_Click=%s Pawn=%s"),
				*MoveClickPhysicalKey.ToString(),
				BoolText(WasInputKeyJustPressed(MoveClickPhysicalKey)),
				BoolText(IsGameplayInputSuppressedByModalUI()),
				*GetNameSafe(IA_Move_Click),
				*GetNameSafe(GetPawn()));
			LastMovePhysicalDownDiagTime = Now;
		}
	}

	if (bMoveClickHeld && !IsInputKeyDown(MoveClickPhysicalKey))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Move hold ended by physical key poll; Key=%s release event was not received."),
			*MoveClickPhysicalKey.ToString());
		OnMoveClickReleased(FInputActionValue());
	}

	if (bAttackClickHeld && !IsInputKeyDown(AttackClickPhysicalKey))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Left/action hold ended by physical key poll; Key=%s release event was not received."),
			*AttackClickPhysicalKey.ToString());
		OnAttackClickReleased(FInputActionValue());
	}

	if (IsGameplayInputSuppressedByModalUI() || IsControlledPawnDead())
	{
		if (bMouseCommandRecoveryPending || bMouseCommandPausedByAbilityCast)
		{
			CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/true);
		}
	}
	else
	{
		UpdateMouseCommand(DeltaSeconds);
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
	UnbindMouseCommandRecoveryDelegates();
	Super::OnPossess(InPawn);

	bMoveClickHeld = false;
	bAttackClickHeld = false;
	PopPrimaryMeleeRotationLockMode();
	PrimaryMeleeRotationLockReleaseTime = -1.0;
	bPrimaryMeleeMovementBlockActive = false;
	ClearQueuedMovementCommand();
	ResetForMoveOnly();
	ResetExtractionCountdownState();
	BindMouseCommandRecoveryDelegates();

	if (UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>())
	{
		PFC->UpdateCachedComponents();
	}

	UpdatePathFollowingForPawnState();
	BindWorldFlowDelegates();
	BindGoldDelegate();
	RefreshZoneRuntimeReferences();
	RebindInventoryWidgetsToCurrentPawn();
	if (const AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get())
	{
		if (GameState->GetWorldFlowPhase() == EAeyerjiWorldFlowPhase::Gameplay)
		{
			ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
		}
	}
	else if (GetPawn())
	{
		ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
	}
}

void AAeyerjiPlayerController::OnUnPossess()
{
	UnbindMouseCommandRecoveryDelegates();
	CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/false);
	AbortMovement_Both();
	StopPendingTeleporter();
	StopPendingInteraction();
	bMoveClickHeld = false;
	bAttackClickHeld = false;
	ClearMouseCommandData();
	CancelFaceActor();
	PopPrimaryMeleeRotationLockMode();
	PrimaryMeleeRotationLockReleaseTime = -1.0;
	bPrimaryMeleeMovementBlockActive = false;
	PendingMoveGoal = FVector::ZeroVector;
	PendingMoveTarget = nullptr;
	ClearQueuedMovementCommand();
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	LastCursorFollowClientDiagTime = -1.0;
	LastCursorFollowServerDiagTime = -1.0;
	ResetCursorFollowHold();
	ResetExtractionCountdownState();

	Super::OnUnPossess();

	UpdatePathFollowingForPawnState();
	RebindInventoryWidgetsToCurrentPawn();
}

void AAeyerjiPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindMouseCommandRecoveryDelegates();
	CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/false);
	StopPendingTeleporter();
	StopPendingInteraction();

	if (HasAuthority())
	{
		if (AAeyerjiPlayerState* AeyerjiPlayerState = GetPlayerState<AAeyerjiPlayerState>())
		{
			AeyerjiPlayerState->CommitCheckpointProfile(EAeyerjiSaveCheckpointReason::LogoutOrShutdown);
		}
	}

	if (AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get())
	{
		GameState->OnWorldFlowPhaseChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleWorldFlowPhaseChanged);
		GameState->OnZoneGameplayReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleZoneGameplayReady);
		GameState->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunStateChanged);
		GameState->OnRunResultsReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunResultsReady);
		GameState->OnObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleObjectiveStateChanged);
		GameState->OnSurvivalRoundStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalRoundStateChanged);
		GameState->OnSurvivalUpgradeOfferChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged);
	}

	UnbindGoldDelegate();
	BoundWorldFlowGameState.Reset();

	if (MissionHUDWidget)
	{
		MissionHUDWidget->RemoveFromParent();
	}

	if (EndRunScreenWidget)
	{
		EndRunScreenWidget->RemoveFromParent();
	}

	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
	}

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiPlayerController::OnRep_Pawn()
{
	UnbindMouseCommandRecoveryDelegates();
	Super::OnRep_Pawn();

	AbortMovement_Local();
	StopMoveToActorLoop();
	StopPendingTeleporter();
	StopPendingInteraction();
	bMoveClickHeld = false;
	bAttackClickHeld = false;
	CancelFaceActor();
	PopPrimaryMeleeRotationLockMode();
	PrimaryMeleeRotationLockReleaseTime = -1.0;
	bPrimaryMeleeMovementBlockActive = false;
	ClearQueuedMovementCommand();
	ResetForMoveOnly();
	ResetExtractionCountdownState();
	BindMouseCommandRecoveryDelegates();

	if (UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>())
	{
		PFC->UpdateCachedComponents();
	}

	UpdatePathFollowingForPawnState();
	BindWorldFlowDelegates();
	BindGoldDelegate();
	RefreshZoneRuntimeReferences();
	RebindInventoryWidgetsToCurrentPawn();
	if (const AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get())
	{
		if (GameState->GetWorldFlowPhase() == EAeyerjiWorldFlowPhase::Gameplay)
		{
			ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
		}
	}
	else if (GetPawn())
	{
		ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
	}
}

void AAeyerjiPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	BindGoldDelegate();
	ApplyCurrentGoldStateToMissionHUD();
}

void AAeyerjiPlayerController::RebindInventoryWidgetsToCurrentPawn()
{
	if (!IsLocalController())
	{
		return;
	}

	APlayerParentNative* PlayerPawn = Cast<APlayerParentNative>(GetPawn());
	TArray<UUserWidget*> BagWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, BagWidgets, UW_InventoryBag_Native::StaticClass(), false);

	if (!PlayerPawn)
	{
		if (BagWidgets.Num() > 0)
		{
			AJ_LOG(this, TEXT("[InventoryUI] Rebind skipped: no APlayerParentNative pawn. Widgets=%d Pawn=%s"),
				BagWidgets.Num(),
				*GetNameSafe(GetPawn()));
		}
		return;
	}

	UAeyerjiInventoryComponent* InventoryComponent = PlayerPawn->EnsureInventoryComponent();
	AJ_LOG(this, TEXT("[InventoryUI] Rebinding inventory widgets. Pawn=%s Inventory=%s Widgets=%d"),
		*GetNameSafe(PlayerPawn),
		*GetNameSafe(InventoryComponent),
		BagWidgets.Num());

	for (UUserWidget* Widget : BagWidgets)
	{
		UW_InventoryBag_Native* Bag = Cast<UW_InventoryBag_Native>(Widget);
		if (!Bag)
		{
			continue;
		}

		AJ_LOG(this, TEXT("[InventoryUI] Binding widget %s to pawn %s inventory %s"),
			*GetNameSafe(Bag),
			*GetNameSafe(PlayerPawn),
			*GetNameSafe(InventoryComponent));
		Bag->BindToPlayer(PlayerPawn);
	}
}

void AAeyerjiPlayerController::BindWorldFlowDelegates()
{
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			if (BoundWorldFlowGameState.Get() != GameState)
			{
				if (AAeyerjiGameState* PreviousGameState = BoundWorldFlowGameState.Get())
				{
					PreviousGameState->OnWorldFlowPhaseChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleWorldFlowPhaseChanged);
					PreviousGameState->OnZoneGameplayReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleZoneGameplayReady);
					PreviousGameState->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunStateChanged);
					PreviousGameState->OnRunResultsReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunResultsReady);
					PreviousGameState->OnObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleObjectiveStateChanged);
					PreviousGameState->OnSurvivalRoundStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalRoundStateChanged);
					PreviousGameState->OnSurvivalUpgradeOfferChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged);
				}

				BoundWorldFlowGameState = GameState;
				GameState->OnWorldFlowPhaseChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleWorldFlowPhaseChanged);
				GameState->OnWorldFlowPhaseChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleWorldFlowPhaseChanged);
				GameState->OnZoneGameplayReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleZoneGameplayReady);
				GameState->OnZoneGameplayReady.AddDynamic(this, &AAeyerjiPlayerController::HandleZoneGameplayReady);
				GameState->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunStateChanged);
				GameState->OnRunStateChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleRunStateChanged);
				GameState->OnRunResultsReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunResultsReady);
				GameState->OnRunResultsReady.AddDynamic(this, &AAeyerjiPlayerController::HandleRunResultsReady);
				GameState->OnObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleObjectiveStateChanged);
				GameState->OnObjectiveStateChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleObjectiveStateChanged);
				GameState->OnSurvivalRoundStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalRoundStateChanged);
				GameState->OnSurvivalRoundStateChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleSurvivalRoundStateChanged);
				GameState->OnSurvivalUpgradeOfferChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged);
				GameState->OnSurvivalUpgradeOfferChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged);

				if (IsLocalController()
					&& GameState->GetRunState() == EAeyerjiRunState::RunComplete
					&& GameState->GetRunResults().ResultsVersion > 0)
				{
					ShowEndRunScreen(GameState->GetRunResults());
				}
			}

			if (IsLocalController())
			{
				HandleWorldFlowPhaseChanged(GameState->GetWorldFlowPhase(), GameState->GetWorldFlowPhase(), GameState->GetTransitionId());
			}
		}
		else if (AAeyerjiGameState* PreviousGameState = BoundWorldFlowGameState.Get())
		{
			PreviousGameState->OnWorldFlowPhaseChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleWorldFlowPhaseChanged);
			PreviousGameState->OnZoneGameplayReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleZoneGameplayReady);
			PreviousGameState->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunStateChanged);
			PreviousGameState->OnRunResultsReady.RemoveDynamic(this, &AAeyerjiPlayerController::HandleRunResultsReady);
			PreviousGameState->OnObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleObjectiveStateChanged);
			PreviousGameState->OnSurvivalRoundStateChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalRoundStateChanged);
			PreviousGameState->OnSurvivalUpgradeOfferChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged);
			BoundWorldFlowGameState.Reset();
		}
	}
}

void AAeyerjiPlayerController::HandleZoneGameplayReady(const FName ZoneId, const int32 ReadyTransitionId)
{
	UE_LOG(LogTemp, Display,
		TEXT("PlayerController::HandleZoneGameplayReady Zone=%s TransitionId=%d Controller=%s"),
		*ZoneId.ToString(),
		ReadyTransitionId,
	*GetNameSafe(this));

	ResetExtractionCountdownState();
	HideEndRunScreen(/*bRestoreGameplayInput=*/false);
	RefreshZoneRuntimeReferences();
	ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
	EnsureMissionHUD();
	ApplyCurrentObjectiveStateFromGameState();
	ApplyCurrentSurvivalRoundStateFromGameState();
	ApplyCurrentSurvivalUpgradeOfferFromGameState();
	ApplyCurrentGoldStateToMissionHUD();

	if (IsLocalController())
	{
		UE_LOG(LogTemp, Display,
			TEXT("PlayerController::HandleZoneGameplayReady reapplied top-down mouse mode Controller=%s CaptureMode=CapturePermanently_IncludingInitialMouseDown"),
			*GetNameSafe(this));
	}
}

void AAeyerjiPlayerController::HandleWorldFlowPhaseChanged(const EAeyerjiWorldFlowPhase NewPhase, const EAeyerjiWorldFlowPhase OldPhase, const int32 AppliedTransitionId)
{
	static_cast<void>(AppliedTransitionId);

	if (!IsLocalController())
	{
		return;
	}

	if (NewPhase != EAeyerjiWorldFlowPhase::Gameplay)
	{
		ResetExtractionCountdownState();
		HideEndRunScreen(/*bRestoreGameplayInput=*/false);
		if (MissionHUDWidget)
		{
			MissionHUDWidget->RemoveFromParent();
		}
		if (NewPhase == EAeyerjiWorldFlowPhase::Menu)
		{
			if (OldPhase != NewPhase)
			{
				ResetMainMenuWidgetInstance();
			}
			EnsureMainMenuWidget(/*bAllowCreate=*/OldPhase != NewPhase);
		}
		else
		{
			HideMainMenuWidget();
		}
		return;
	}

	HideMainMenuWidget();
	EnsureMissionHUD();
	ApplyCurrentObjectiveStateFromGameState();
	ApplyCurrentSurvivalRoundStateFromGameState();
	ApplyCurrentSurvivalUpgradeOfferFromGameState();
	ApplyCurrentGoldStateToMissionHUD();
}

void AAeyerjiPlayerController::HandleRunStateChanged(const EAeyerjiRunState NewState, const EAeyerjiRunState OldState)
{
	static_cast<void>(OldState);

	if (!IsLocalController())
	{
		return;
	}

	if (NewState == EAeyerjiRunState::ObjectiveComplete)
	{
		// Uses GlobalStringTable.csv key. Reimport string table asset after CSV changes.
		ShowPopupMessage(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ExtractionPortalOpened")), 2.f);
		return;
	}

	if (NewState == EAeyerjiRunState::RunComplete)
	{
		ResetExtractionCountdownState();
		if (MissionHUDWidget)
		{
			MissionHUDWidget->RemoveFromParent();
		}
		return;
	}

	if (NewState == EAeyerjiRunState::ReturnToMenu)
	{
		ResetExtractionCountdownState();
		HideEndRunScreen(/*bRestoreGameplayInput=*/false);
		if (MissionHUDWidget)
		{
			MissionHUDWidget->RemoveFromParent();
		}
		return;
	}

	if (NewState == EAeyerjiRunState::PreRun || NewState == EAeyerjiRunState::InRun)
	{
		ResetExtractionCountdownState();
		const AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
		const bool bRestoreGameplayInput = IsValid(GameState) && GameState->GetWorldFlowPhase() == EAeyerjiWorldFlowPhase::Gameplay;
		HideEndRunScreen(bRestoreGameplayInput);
	}
}

void AAeyerjiPlayerController::HandleRunResultsReady(const FAeyerjiRunResults& Results)
{
	if (!IsLocalController())
	{
		return;
	}

	ShowEndRunScreen(Results);
}

void AAeyerjiPlayerController::HandleObjectiveStateChanged(const FAeyerjiObjectiveState& ObjectiveState)
{
	if (!IsLocalController())
	{
		return;
	}

	ApplyObjectiveStateToMissionHUD(ObjectiveState);
}

void AAeyerjiPlayerController::HandleSurvivalRoundStateChanged(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	if (!IsLocalController())
	{
		return;
	}

	ApplySurvivalRoundStateToMissionHUD(SurvivalState);
}

void AAeyerjiPlayerController::HandleSurvivalUpgradeOfferChanged(const FAeyerjiSurvivalUpgradeOfferState& OfferState)
{
	if (!IsLocalController())
	{
		return;
	}

	ApplySurvivalUpgradeOfferToMissionHUD(OfferState);
}

void AAeyerjiPlayerController::HandleGoldChanged(const int64 NewGold, const int64 Delta)
{
	if (!IsLocalController())
	{
		return;
	}

	EnsureMissionHUD();
	if (MissionHUDWidget)
	{
		MissionHUDWidget->ApplyGoldState(NewGold, Delta);
	}
}

bool AAeyerjiPlayerController::IsGameplayInputSuppressedByModalUI() const
{
	return IsLocalController()
		&& EndRunScreenWidget
		&& EndRunScreenWidget->IsInViewport();
}

void AAeyerjiPlayerController::EnsureMissionHUD()
{
	if (!IsLocalController() || !MissionHUDClass)
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (IsValid(GameState) && GameState->GetWorldFlowPhase() != EAeyerjiWorldFlowPhase::Gameplay)
	{
		return;
	}

	if (!MissionHUDWidget)
	{
		MissionHUDWidget = CreateWidget<UW_AeyerjiMissionHUD>(this, MissionHUDClass);
	}

	if (!MissionHUDWidget)
	{
		return;
	}

	if (!MissionHUDWidget->IsInViewport())
	{
		MissionHUDWidget->AddToViewport(MissionHUDZOrder);
	}

	ApplyCurrentGoldStateToMissionHUD();
}

void AAeyerjiPlayerController::ApplyCurrentObjectiveStateFromGameState()
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (!IsValid(GameState))
	{
		return;
	}

	ApplyObjectiveStateToMissionHUD(GameState->GetCurrentObjectiveState());
}

void AAeyerjiPlayerController::ApplyCurrentSurvivalRoundStateFromGameState()
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (!IsValid(GameState))
	{
		return;
	}

	ApplySurvivalRoundStateToMissionHUD(GameState->GetCurrentSurvivalRoundState());
}

void AAeyerjiPlayerController::ApplyCurrentSurvivalUpgradeOfferFromGameState()
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (!IsValid(GameState))
	{
		return;
	}

	ApplySurvivalUpgradeOfferToMissionHUD(GameState->GetCurrentSurvivalUpgradeOfferState());
}

void AAeyerjiPlayerController::ApplyCurrentGoldStateToMissionHUD()
{
	if (!IsLocalController() || !MissionHUDWidget)
	{
		return;
	}

	const AAeyerjiPlayerState* AeyerjiPS = GetPlayerState<AAeyerjiPlayerState>();
	MissionHUDWidget->ApplyGoldState(AeyerjiPS ? AeyerjiPS->GetGold() : 0, 0);
}

void AAeyerjiPlayerController::ApplyObjectiveStateToMissionHUD(const FAeyerjiObjectiveState& ObjectiveState)
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (IsValid(GameState) && GameState->GetWorldFlowPhase() != EAeyerjiWorldFlowPhase::Gameplay)
	{
		if (MissionHUDWidget)
		{
			MissionHUDWidget->RemoveFromParent();
		}
		return;
	}

	EnsureMissionHUD();
	if (!MissionHUDWidget)
	{
		return;
	}

	MissionHUDWidget->ApplyObjectiveState(ObjectiveState);
}

void AAeyerjiPlayerController::ApplySurvivalRoundStateToMissionHUD(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (IsValid(GameState) && GameState->GetWorldFlowPhase() != EAeyerjiWorldFlowPhase::Gameplay)
	{
		return;
	}

	EnsureMissionHUD();
	if (!MissionHUDWidget)
	{
		return;
	}

	MissionHUDWidget->ApplySurvivalRoundState(SurvivalState);
}

void AAeyerjiPlayerController::ApplySurvivalUpgradeOfferToMissionHUD(const FAeyerjiSurvivalUpgradeOfferState& OfferState)
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiGameState* GameState = BoundWorldFlowGameState.Get();
	if (!IsValid(GameState))
	{
		if (UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<AAeyerjiGameState>();
		}
	}

	if (IsValid(GameState) && GameState->GetWorldFlowPhase() != EAeyerjiWorldFlowPhase::Gameplay)
	{
		return;
	}

	EnsureMissionHUD();
	if (!MissionHUDWidget)
	{
		return;
	}

	if (OfferState.bActive)
	{
		MissionHUDWidget->ApplySurvivalUpgradeOffer(OfferState);
	}
	else
	{
		MissionHUDWidget->ClearSurvivalUpgradeOffer();
	}
}

void AAeyerjiPlayerController::BindGoldDelegate()
{
	if (!IsLocalController())
	{
		return;
	}

	AAeyerjiPlayerState* AeyerjiPS = GetPlayerState<AAeyerjiPlayerState>();
	if (BoundGoldPlayerState.Get() == AeyerjiPS)
	{
		return;
	}

	UnbindGoldDelegate();
	BoundGoldPlayerState = AeyerjiPS;
	if (AeyerjiPS)
	{
		AeyerjiPS->OnGoldChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleGoldChanged);
		AeyerjiPS->OnGoldChanged.AddDynamic(this, &AAeyerjiPlayerController::HandleGoldChanged);
	}
}

void AAeyerjiPlayerController::UnbindGoldDelegate()
{
	if (AAeyerjiPlayerState* AeyerjiPS = BoundGoldPlayerState.Get())
	{
		AeyerjiPS->OnGoldChanged.RemoveDynamic(this, &AAeyerjiPlayerController::HandleGoldChanged);
	}
	BoundGoldPlayerState.Reset();
}

void AAeyerjiPlayerController::ShowEndRunScreen(const FAeyerjiRunResults& Results)
{
	if (!IsLocalController() || !EndRunScreenClass)
	{
		return;
	}

	if (!EndRunScreenWidget)
	{
		EndRunScreenWidget = CreateWidget<UW_EndRunScreen>(this, EndRunScreenClass);
	}

	if (!EndRunScreenWidget)
	{
		return;
	}

	AbortMovement_Both();
	StopPendingTeleporter();
	StopPendingInteraction();
	ResetForMoveOnly();
	ResetExtractionCountdownState();
	if (MissionHUDWidget)
	{
		MissionHUDWidget->RemoveFromParent();
	}
	EndRunScreenWidget->ApplyRunResults(Results);
	if (!EndRunScreenWidget->IsInViewport())
	{
		EndRunScreenWidget->AddToViewport(200);
	}

	UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
		this,
		EndRunScreenWidget,
		EMouseLockMode::DoNotLock,
		/*bHideCursorDuringCapture=*/false,
		/*bFlushInput=*/true);
	bShowMouseCursor = true;
}

void AAeyerjiPlayerController::HideEndRunScreen(const bool bRestoreGameplayInput)
{
	if (EndRunScreenWidget)
	{
		EndRunScreenWidget->RemoveFromParent();
	}

	if (bRestoreGameplayInput)
	{
		ApplyGameplayMouseInputMode(/*bFlushInput=*/true);
	}
}

void AAeyerjiPlayerController::EnsureMainMenuWidget(const bool bAllowCreate)
{
	if (!IsLocalController() || !MainMenuWidgetClass)
	{
		return;
	}

	if (!MainMenuWidget)
	{
		TArray<UUserWidget*> ExistingWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, ExistingWidgets, MainMenuWidgetClass, false);
		for (UUserWidget* ExistingWidget : ExistingWidgets)
		{
			if (IsValid(ExistingWidget))
			{
				MainMenuWidget = ExistingWidget;
				break;
			}
		}
	}

	if (!MainMenuWidget && bAllowCreate)
	{
		MainMenuWidget = CreateWidget<UUserWidget>(this, MainMenuWidgetClass);
	}

	if (!MainMenuWidget)
	{
		return;
	}

	MainMenuWidget->SetVisibility(ESlateVisibility::Visible);
	if (!MainMenuWidget->IsInViewport())
	{
		MainMenuWidget->AddToViewport(MainMenuWidgetZOrder);
	}

	UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx(
		this,
		MainMenuWidget,
		EMouseLockMode::DoNotLock,
		/*bFlushInput=*/true);
	bShowMouseCursor = true;
}

void AAeyerjiPlayerController::HideMainMenuWidget()
{
	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
	}
}

void AAeyerjiPlayerController::ResetMainMenuWidgetInstance()
{
	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}
}

void AAeyerjiPlayerController::Client_BeginExtractionCountdown_Implementation(const float DurationSeconds)
{
	StartExtractionCountdownState(DurationSeconds);
}

void AAeyerjiPlayerController::Client_ResetExtractionCountdown_Implementation()
{
	ResetExtractionCountdownState();
}

void AAeyerjiPlayerController::StartExtractionCountdownState(const float DurationSeconds)
{
	const float ClampedDuration = FMath::Max(DurationSeconds, 0.1f);
	const bool bStateChanged = !bExtractionCountdownActive
		|| !FMath::IsNearlyEqual(ExtractionCountdownDurationSeconds, ClampedDuration)
		|| !FMath::IsNearlyZero(ExtractionCountdownElapsedSeconds)
		|| !FMath::IsNearlyEqual(ExtractionCountdownSecondsRemaining, ClampedDuration)
		|| !FMath::IsNearlyZero(ExtractionCountdownProgress);

	bExtractionCountdownActive = true;
	ExtractionCountdownDurationSeconds = ClampedDuration;
	ExtractionCountdownElapsedSeconds = 0.f;
	ExtractionCountdownSecondsRemaining = ClampedDuration;
	ExtractionCountdownProgress = 0.f;
	ExtractionCountdownStartTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (bStateChanged)
	{
		BroadcastExtractionCountdownUpdated();
	}
}

void AAeyerjiPlayerController::RefreshExtractionCountdownState()
{
	if (!bExtractionCountdownActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		ResetExtractionCountdownState();
		return;
	}

	const float ClampedDuration = FMath::Max(ExtractionCountdownDurationSeconds, 0.1f);
	const float NewElapsedSeconds = FMath::Clamp(
		static_cast<float>(World->GetTimeSeconds() - ExtractionCountdownStartTimeSeconds),
		0.f,
		ClampedDuration);
	const float NewSecondsRemaining = FMath::Max(ClampedDuration - NewElapsedSeconds, 0.f);
	const float NewProgress = FMath::Clamp(NewElapsedSeconds / ClampedDuration, 0.f, 1.f);

	if (FMath::IsNearlyEqual(ExtractionCountdownElapsedSeconds, NewElapsedSeconds)
		&& FMath::IsNearlyEqual(ExtractionCountdownSecondsRemaining, NewSecondsRemaining)
		&& FMath::IsNearlyEqual(ExtractionCountdownProgress, NewProgress))
	{
		return;
	}

	ExtractionCountdownElapsedSeconds = NewElapsedSeconds;
	ExtractionCountdownSecondsRemaining = NewSecondsRemaining;
	ExtractionCountdownProgress = NewProgress;
	BroadcastExtractionCountdownUpdated();
}

void AAeyerjiPlayerController::ResetExtractionCountdownState()
{
	const bool bStateChanged = bExtractionCountdownActive
		|| !FMath::IsNearlyZero(ExtractionCountdownDurationSeconds)
		|| !FMath::IsNearlyZero(ExtractionCountdownElapsedSeconds)
		|| !FMath::IsNearlyZero(ExtractionCountdownSecondsRemaining)
		|| !FMath::IsNearlyZero(ExtractionCountdownProgress);

	bExtractionCountdownActive = false;
	ExtractionCountdownDurationSeconds = 0.f;
	ExtractionCountdownElapsedSeconds = 0.f;
	ExtractionCountdownSecondsRemaining = 0.f;
	ExtractionCountdownProgress = 0.f;
	ExtractionCountdownStartTimeSeconds = -1.0;

	if (bStateChanged)
	{
		BroadcastExtractionCountdownUpdated();
	}
}

void AAeyerjiPlayerController::BroadcastExtractionCountdownUpdated()
{
	if (IsLocalController())
	{
		OnExtractionCountdownUpdated.Broadcast();
	}
}

void AAeyerjiPlayerController::RefreshZoneRuntimeReferences()
{
	EncounterDirector = nullptr;
	LevelDirector = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerController::RefreshZoneRuntimeReferences failed - World missing for %s."),
			*GetNameSafe(this));
		return;
	}

	for (TActorIterator<AAeyerjiEncounterDirector> It(World); It; ++It)
	{
		EncounterDirector = *It;
		break;
	}

	for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
	{
		LevelDirector = *It;
		break;
	}

	if (EncounterDirector)
	{
		UE_LOG(LogTemp, Display,
			TEXT("PlayerController::RefreshZoneRuntimeReferences found EncounterDirector=%s Controller=%s"),
			*GetNameSafe(EncounterDirector),
			*GetNameSafe(this));
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("PlayerController::RefreshZoneRuntimeReferences found no EncounterDirector Controller=%s"),
			*GetNameSafe(this));
	}

	if (LevelDirector)
	{
		UE_LOG(LogTemp, Display,
			TEXT("PlayerController::RefreshZoneRuntimeReferences found LevelDirector=%s Controller=%s"),
			*GetNameSafe(LevelDirector),
			*GetNameSafe(this));
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("PlayerController::RefreshZoneRuntimeReferences found no LevelDirector Controller=%s"),
			*GetNameSafe(this));
	}
}

bool AAeyerjiPlayerController::HasActiveRunTimeLimit() const
{
	return LevelDirector && LevelDirector->HasRunTimeLimit();
}

float AAeyerjiPlayerController::GetActiveRunTimeLimitSeconds() const
{
	// Legacy objective widgets divide by this value directly, so clamp the UI-facing accessor.
	return (LevelDirector && LevelDirector->HasRunTimeLimit())
		? FMath::Max(LevelDirector->RunTimeLimitSeconds, 0.01f)
		: 1.f;
}

float AAeyerjiPlayerController::GetRunTimerProgress01() const
{
	if (!LevelDirector || !LevelDirector->HasRunTimeLimit())
	{
		return 0.f;
	}

	const float TimeLimitSeconds = FMath::Max(LevelDirector->RunTimeLimitSeconds, 0.01f);
	return FMath::Clamp(LevelDirector->GetRunTimeSeconds() / TimeLimitSeconds, 0.f, 1.f);
}

float AAeyerjiPlayerController::GetRunTimerSecondsRemaining() const
{
	return LevelDirector ? LevelDirector->GetRemainingRunTimeSeconds() : 0.f;
}

bool AAeyerjiPlayerController::IsKillTargetRun() const
{
	if (!LevelDirector)
	{
		return false;
	}

	const EAeyerjiRunWinCondition WinCondition = LevelDirector->GetRunWinCondition();
	return WinCondition == EAeyerjiRunWinCondition::KillTarget || WinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss;
}

bool AAeyerjiPlayerController::IsBossClearedRun() const
{
	if (!LevelDirector)
	{
		return true;
	}

	const EAeyerjiRunWinCondition WinCondition = LevelDirector->GetRunWinCondition();
	return WinCondition == EAeyerjiRunWinCondition::BossCleared || WinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss;
}

bool AAeyerjiPlayerController::IsKillTargetThenBossRun() const
{
	return LevelDirector && LevelDirector->GetRunWinCondition() == EAeyerjiRunWinCondition::KillTargetThenBoss;
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
			if (!bPathFollowingTickSuppressed)
			{
				AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Suppressing path following. HasPawn=%s HasMoveComp=%s Dead=%s %s"),
					BoolText(ControlledPawn != nullptr),
					BoolText(bHasMoveComp),
					BoolText(IsControlledPawnDead()),
					*DescribePathFollowing(PFC));
			}
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
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Re-enabled path following. %s %s"),
				*DescribePawnMovement(ControlledPawn),
				*DescribePathFollowing(PFC));
		}
	}
}


void AAeyerjiPlayerController::AbortMovement_Local() const
{
	UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] AbortMovement_Local. %s %s"),
		*DescribePawnMovement(GetPawn()),
		*DescribePathFollowing(PFC));
	if (PFC)
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
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Server_AbortMovement received."));
	AbortMovement_Local();
}

void AAeyerjiPlayerController::AbortMovement_Both()
{
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] AbortMovement_Both. HasAuthority=%s MoveLoopTarget=%s Mode=%s"),
		BoolText(HasAuthority()),
		*GetNameSafe(MoveLoopTarget.Get()),
		MoveLoopModeText(MoveLoopMode));
	StopMoveToActorLoop();
	AbortMovement_Local();
	if (!HasAuthority())
	{
		Server_AbortMovement();
	}
}

void AAeyerjiPlayerController::BeginLocalAbilityCastInputLock(float DurationSeconds)
{
	AbortMovement_Both();
	CancelMouseOwnedMovement();

	CachedTarget = nullptr;
	PendingMoveTarget = nullptr;
	MouseCommand.IssuedMoveTarget = nullptr;
	bMouseCommandPausedByAbilityCast = MouseCommand.Owner != EAeyerjiMouseButton::None;

	if (UWorld* World = GetWorld())
	{
		LocalAbilityCastInputLockEndTime = World->GetTimeSeconds() + FMath::Max(0.f, DurationSeconds);
	}
	else
	{
		LocalAbilityCastInputLockEndTime = -1.0;
	}

	UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MouseCommand] Ability cast paused command. Owner=%d Intent=%d Target=%s LeftDown=%s RightDown=%s Duration=%.3f"),
		static_cast<int32>(MouseCommand.Owner),
		static_cast<int32>(MouseCommand.Intent),
		*GetNameSafe(MouseCommand.TargetActor.Get()),
		BoolText(IsInputKeyDown(AttackClickPhysicalKey)),
		BoolText(IsInputKeyDown(MoveClickPhysicalKey)),
		DurationSeconds);
}


void AAeyerjiPlayerController::ReportMouseNavContextToServer(EMouseNavResult Result, const FVector& NavLocation, const FVector& CursorLocation, APawn* ClickedPawn)
{
	if (!IsLocalController())
	{
		return;
	}

	APawn* PawnToReport = ClickedPawn;
	if (PawnToReport)
	{
		const bool bReportablePawn =
			PawnToReport->GetIsReplicated() &&
			!PawnToReport->IsPlayerControlled() &&
			!PawnToReport->IsA<ASpectatorPawn>();

		if (!bReportablePawn)
		{
			PawnToReport = nullptr;
			if (Result == EMouseNavResult::ClickedPawn)
			{
				Result = EMouseNavResult::NavLocation;
			}
		}
	}

	if (Result == EMouseNavResult::None)
	{
		MouseNavServerCache.Invalidate();
		if (!HasAuthority())
		{
			Server_SetMouseNavContext(Result, NavLocation, CursorLocation, nullptr);
		}
		return;
	}

	SetMouseNavContextInternal(Result, NavLocation, CursorLocation, PawnToReport);

	if (!HasAuthority())
	{
		Server_SetMouseNavContext(Result, NavLocation, CursorLocation, PawnToReport);
	}
}

void AAeyerjiPlayerController::Server_SetMouseNavContext_Implementation(EMouseNavResult Result, FVector NavLocation, FVector CursorLocation, APawn* ClickedPawn)
{
	SetMouseNavContextInternal(Result, NavLocation, CursorLocation, ClickedPawn);
}

bool AAeyerjiPlayerController::GetCachedMouseNavContext(EMouseNavResult& OutResult, FVector& OutNavLocation, FVector& OutCursorLocation, APawn*& OutPawn, float MaxAgeSeconds) const
{
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
	if (Result == EMouseNavResult::None)
	{
		ReportMouseNavContextToServer(EMouseNavResult::None, FVector::ZeroVector, FVector::ZeroVector, nullptr);
	}
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
	UpdatePrimaryMeleeRotationLock();
	if (bPrimaryMeleeRotationLockActive)
	{
		return;
	}

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

bool AAeyerjiPlayerController::HasActivePrimaryMeleePhaseTag(const UAbilitySystemComponent* ASC) const
{
	if (!ASC)
	{
		return false;
	}

	return ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_WindUp)
		|| ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_HitWindow)
		|| ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_Recovery)
		|| ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_PrimaryMelee_BlockMovement);
}

void AAeyerjiPlayerController::PushPrimaryMeleeRotationLockMode()
{
	if (bPrimaryMeleeRotationLockActive)
	{
		return;
	}

	ACharacter* PawnCharacter = Cast<ACharacter>(GetPawn());
	if (!PawnCharacter)
	{
		return;
	}

	UCharacterMovementComponent* CMC = PawnCharacter->GetCharacterMovement();
	if (!CMC)
	{
		return;
	}

	SavedPrimaryMeleeRotationMode.bValid = true;
	if (SavedFacingMode.bValid)
	{
		SavedPrimaryMeleeRotationMode.bUseControllerRotationYaw = SavedFacingMode.bUseControllerRotationYaw;
		SavedPrimaryMeleeRotationMode.bOrientRotationToMovement = SavedFacingMode.bOrientRotationToMovement;
		SavedPrimaryMeleeRotationMode.bUseControllerDesiredRotation = SavedFacingMode.bUseControllerDesiredRotation;
		SavedPrimaryMeleeRotationMode.SavedRotationRateYaw = SavedFacingMode.SavedRotationRateYaw;
	}
	else
	{
		SavedPrimaryMeleeRotationMode.bUseControllerRotationYaw = PawnCharacter->bUseControllerRotationYaw;
		SavedPrimaryMeleeRotationMode.bOrientRotationToMovement = CMC->bOrientRotationToMovement;
		SavedPrimaryMeleeRotationMode.bUseControllerDesiredRotation = CMC->bUseControllerDesiredRotation;
		SavedPrimaryMeleeRotationMode.SavedRotationRateYaw = CMC->RotationRate.Yaw;
	}

	// Freeze yaw updates from locomotion and controller while melee tags are active.
	PawnCharacter->bUseControllerRotationYaw = false;
	CMC->bOrientRotationToMovement = false;
	CMC->bUseControllerDesiredRotation = false;
	CMC->RotationRate.Yaw = 0.f;

	ResetCursorFollowTurnRate();
	bPrimaryMeleeRotationLockActive = true;
}

void AAeyerjiPlayerController::PopPrimaryMeleeRotationLockMode()
{
	if (!bPrimaryMeleeRotationLockActive)
	{
		return;
	}

	ACharacter* PawnCharacter = Cast<ACharacter>(GetPawn());
	if (PawnCharacter && SavedPrimaryMeleeRotationMode.bValid)
	{
		if (UCharacterMovementComponent* CMC = PawnCharacter->GetCharacterMovement())
		{
			CMC->bOrientRotationToMovement = SavedPrimaryMeleeRotationMode.bOrientRotationToMovement;
			CMC->bUseControllerDesiredRotation = SavedPrimaryMeleeRotationMode.bUseControllerDesiredRotation;
			CMC->RotationRate.Yaw = SavedPrimaryMeleeRotationMode.SavedRotationRateYaw;
		}

		PawnCharacter->bUseControllerRotationYaw = SavedPrimaryMeleeRotationMode.bUseControllerRotationYaw;
	}

	SavedPrimaryMeleeRotationMode = {};
	bPrimaryMeleeRotationLockActive = false;
}

void AAeyerjiPlayerController::QueueMovementCommand(const FVector& Goal, bool bSpawnCursorFX, bool bIsContinuous)
{
	if (IsControlledPawnDead())
	{
		ClearQueuedMovementCommand();
		return;
	}

	QueuedMovementGoal = Goal;
	QueuedMovementTarget = nullptr;
	bQueuedMovementIsActor = false;
	bQueuedMovementSpawnCursorFX = bSpawnCursorFX;
	bQueuedMovementWasContinuous = bIsContinuous;
	bHasQueuedMovementCommand = true;

	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Queued movement location during blocked movement. Goal=%s Continuous=%s SpawnFX=%s"),
		*Goal.ToCompactString(),
		BoolText(bIsContinuous),
		BoolText(bSpawnCursorFX));
}

void AAeyerjiPlayerController::QueueMovementCommand(AActor* Target, bool bIsContinuous)
{
	if (!IsValid(Target) || IsControlledPawnDead())
	{
		ClearQueuedMovementCommand();
		return;
	}

	QueuedMovementGoal = Target->GetActorLocation();
	QueuedMovementTarget = Target;
	bQueuedMovementIsActor = true;
	bQueuedMovementSpawnCursorFX = false;
	bQueuedMovementWasContinuous = bIsContinuous;
	bHasQueuedMovementCommand = true;

	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Queued movement actor during primary melee. Target=%s Continuous=%s"),
		*GetNameSafe(Target),
		BoolText(bIsContinuous));
}

void AAeyerjiPlayerController::ClearQueuedMovementCommand()
{
	bHasQueuedMovementCommand = false;
	bQueuedMovementIsActor = false;
	bQueuedMovementSpawnCursorFX = false;
	bQueuedMovementWasContinuous = false;
	QueuedMovementGoal = FVector::ZeroVector;
	QueuedMovementTarget = nullptr;
}

void AAeyerjiPlayerController::FlushQueuedMovementCommandIfAllowed()
{
	if (!bHasQueuedMovementCommand || HasActivePrimaryMeleePhaseTag(GetControlledAbilitySystem()) || IsControlledPawnDead())
	{
		return;
	}

	const bool bWasActorCommand = bQueuedMovementIsActor && QueuedMovementTarget.IsValid();
	const bool bSpawnFX = bQueuedMovementSpawnCursorFX;
	const bool bWasContinuous = bQueuedMovementWasContinuous;
	const FVector Goal = QueuedMovementGoal;
	AActor* Target = QueuedMovementTarget.Get();

	ClearQueuedMovementCommand();

	if (bWasActorCommand)
	{
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Flushing queued actor movement after blocked movement. Target=%s Continuous=%s"),
			*GetNameSafe(Target),
			BoolText(bWasContinuous));
		IssueMoveRPC(Target);
		return;
	}

	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Flushing queued location movement after blocked movement. Goal=%s Continuous=%s SpawnFX=%s"),
		*Goal.ToCompactString(),
		BoolText(bWasContinuous),
		BoolText(bSpawnFX));

	IssueMoveRPC(Goal);
	if (bSpawnFX)
	{
		SpawnCursorFX(Goal);
	}
}

void AAeyerjiPlayerController::UpdatePrimaryMeleeRotationLock()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		PopPrimaryMeleeRotationLockMode();
		PrimaryMeleeRotationLockReleaseTime = -1.0;
		bPrimaryMeleeMovementBlockActive = false;
		return;
	}

	const bool bHasMeleePhase = HasActivePrimaryMeleePhaseTag(GetControlledAbilitySystem());
	const double Now = World->GetTimeSeconds();

	if (bHasMeleePhase)
	{
		PrimaryMeleeRotationLockReleaseTime = Now + FMath::Max(0.f, PrimaryMeleeRotationLockGraceSeconds);
	}

	const bool bWithinGrace = PrimaryMeleeRotationLockReleaseTime >= 0.0 && Now < PrimaryMeleeRotationLockReleaseTime;
	const bool bShouldLock = bHasMeleePhase || bWithinGrace;
	const bool bWasMovementBlocked = bPrimaryMeleeMovementBlockActive;
	bPrimaryMeleeMovementBlockActive = bShouldLock;

	if (bShouldLock)
	{
		if (!bWasMovementBlocked)
		{
			CancelFaceActor();
			StopMoveToActorLoop();
			AbortMovement_Local();
			if (!HasAuthority())
			{
				Server_AbortMovement();
			}
		}
		PushPrimaryMeleeRotationLockMode();
	}
	else
	{
		PopPrimaryMeleeRotationLockMode();
		if (bWasMovementBlocked)
		{
			FlushQueuedMovementCommandIfAllowed();
		}
	}
}

void AAeyerjiPlayerController::StartPendingTeleporter(AAeyerjiLinkedTeleporter* Teleporter, const uint8 EndpointIndex)
{
	PendingTeleporter = Teleporter;
	PendingTeleporterEndpointIndex = EndpointIndex;
	GetWorldTimerManager().SetTimer(
		PendingTeleporterTimer,
		this,
		&AAeyerjiPlayerController::ProcessPendingTeleporter,
		PendingTeleporterInterval,
		true);

	AJ_LOG(this, TEXT("[PC] StartPendingTeleporter %s Endpoint=%d"),
		*GetNameSafe(Teleporter),
		static_cast<int32>(EndpointIndex));
}

void AAeyerjiPlayerController::StopPendingTeleporter()
{
	GetWorldTimerManager().ClearTimer(PendingTeleporterTimer);
	PendingTeleporter = nullptr;
	PendingTeleporterEndpointIndex = 0;
}

void AAeyerjiPlayerController::ProcessPendingTeleporter()
{
	AAeyerjiLinkedTeleporter* Teleporter = PendingTeleporter.Get();
	if (!IsValid(Teleporter))
	{
		StopPendingTeleporter();
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		StopPendingTeleporter();
		return;
	}

	if (!Teleporter->IsEndpointEnabledForUse(PendingTeleporterEndpointIndex))
	{
		StopPendingTeleporter();
		return;
	}

	if (Teleporter->IsPawnInInteractionRange(ControlledPawn, PendingTeleporterEndpointIndex))
	{
		AJ_LOG(this, TEXT("[PC] Close enough - requesting linked teleporter use"));
		AbortMovement_Both();
		Server_RequestLinkedTeleporterUse(Teleporter, PendingTeleporterEndpointIndex);
		StopPendingTeleporter();
	}
}

bool AAeyerjiPlayerController::ComputeTeleporterGoal(const AAeyerjiLinkedTeleporter* Teleporter, const uint8 EndpointIndex, FVector& OutGoal) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return false;
	}

	const APawn* ThisPawn = GetPawn();
	if (!ThisPawn)
	{
		return false;
	}

	OutGoal = ThisPawn->GetActorLocation();

	if (!IsValid(Teleporter))
	{
		return true;
	}

	const FVector Center = Teleporter->GetEndpointLocation(EndpointIndex);
	const float Radius = FMath::Max(Teleporter->GetEndpointInteractionRadius(), 30.f);
	FNavLocation Projected;

	auto TryProject = [&](const FVector& Candidate, const FVector& Extents) -> bool
	{
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, Extents))
		{
			return false;
		}

		OutGoal = Projected.Location;
		return true;
	};

	if (TryProject(Center, FVector(Radius, Radius, 600.f)))
	{
		return true;
	}

	const FVector PawnLocation = ThisPawn->GetActorLocation();
	const FVector ToCenter = (Center - PawnLocation).GetSafeNormal2D();
	if (!ToCenter.IsNearlyZero())
	{
		const FVector NearEdge = Center - ToCenter * FMath::Clamp(Radius * 0.35f, 40.f, Radius);
		if (TryProject(NearEdge, FVector(80.f, 80.f, 600.f)))
		{
			return true;
		}
	}

	constexpr int32 NumSamples = 8;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Angle = (2.f * PI * Index) / NumSamples;
		const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const FVector Sample = Center + Direction * (Radius * 0.75f);
		if (TryProject(Sample, FVector(80.f, 80.f, 600.f)))
		{
			return true;
		}
	}

	if (Nav->GetRandomPointInNavigableRadius(Center, Radius, Projected))
	{
		OutGoal = Projected.Location;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PC] Failed to find teleporter goal for %s endpoint %d"),
		*GetNameSafe(Teleporter),
		static_cast<int32>(EndpointIndex));
	return false;
}

void AAeyerjiPlayerController::StartPendingInteraction(AActor* InteractableActor)
{
	PendingInteractable = InteractableActor;
	GetWorldTimerManager().SetTimer(
		PendingInteractionTimer,
		this,
		&AAeyerjiPlayerController::ProcessPendingInteraction,
		PendingInteractionInterval,
		true);

	AJ_LOG(this, TEXT("[Interaction] Pending interaction started Target=%s"), *GetNameSafe(InteractableActor));
}

void AAeyerjiPlayerController::StopPendingInteraction()
{
	if (PendingInteractable.IsValid())
	{
		AJ_LOG_VERY_VERBOSE(this, TEXT("[Interaction] Pending interaction stopped Target=%s"),
			*GetNameSafe(PendingInteractable.Get()));
	}

	GetWorldTimerManager().ClearTimer(PendingInteractionTimer);
	PendingInteractable = nullptr;
}

void AAeyerjiPlayerController::ProcessPendingInteraction()
{
	AActor* InteractableActor = PendingInteractable.Get();
	if (!IsValid(InteractableActor) || !InteractableActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		AJ_LOG(this, TEXT("[Interaction] Pending interaction canceled: invalid target Target=%s"),
			*GetNameSafe(InteractableActor));
		StopPendingInteraction();
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		AJ_LOG(this, TEXT("[Interaction] Pending interaction canceled: controller has no pawn Target=%s"),
			*GetNameSafe(InteractableActor));
		StopPendingInteraction();
		return;
	}

	const FVector InteractionLocation = IAeyerjiInteractable::Execute_GetInteractionLocation(InteractableActor);
	const float InteractionRadius = IAeyerjiInteractable::Execute_GetInteractionRadius(InteractableActor);
	const float Distance2D = FVector::Dist2D(ControlledPawn->GetActorLocation(), InteractionLocation);
	if (InteractionRadius <= 0.f || Distance2D <= InteractionRadius)
	{
		AJ_LOG(this, TEXT("[Interaction] Pending interaction reached range Target=%s Pawn=%s Distance=%.1f Radius=%.1f Unlimited=%d"),
			*GetNameSafe(InteractableActor),
			*GetNameSafe(ControlledPawn),
			Distance2D,
			InteractionRadius,
			InteractionRadius <= 0.f ? 1 : 0);
		AbortMovement_Both();
		Server_RequestInteractableUse(InteractableActor);
		StopPendingInteraction();
	}
}

bool AAeyerjiPlayerController::ComputeInteractionGoal(AActor* InteractableActor, FVector& OutGoal) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const APawn* ControlledPawn = GetPawn();
	if (!Nav || !ControlledPawn || !IsValid(InteractableActor)
		|| !InteractableActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		return false;
	}

	const FVector Center = IAeyerjiInteractable::Execute_GetInteractionLocation(InteractableActor);
	const float Radius = FMath::Max(IAeyerjiInteractable::Execute_GetInteractionRadius(InteractableActor), 30.f);
	FNavLocation Projected;

	auto TryProject = [&](const FVector& Probe, const FVector& Extents)
	{
		if (!Nav->ProjectPointToNavigation(Probe, Projected, Extents))
		{
			return false;
		}

		OutGoal = Projected.Location;
		return true;
	};

	const FVector PawnLoc = ControlledPawn->GetActorLocation();
	const FVector FromCenterToPawn = (PawnLoc - Center).GetSafeNormal2D();
	if (!FromCenterToPawn.IsNearlyZero())
	{
		const FVector Preferred = Center + FromCenterToPawn * FMath::Clamp(Radius * 0.75f, 60.f, Radius);
		if (TryProject(Preferred, FVector(80.f, 80.f, 600.f)))
		{
			return true;
		}
	}

	if (TryProject(Center, FVector(Radius, Radius, 600.f)))
	{
		return true;
	}

	const int32 NumSamples = 8;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Angle = (2.f * PI * Index) / NumSamples;
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		if (TryProject(Center + Dir * Radius, FVector(80.f, 80.f, 600.f)))
		{
			return true;
		}
	}

	UE_LOG(LogAeyerji, Warning, TEXT("[Interaction] Failed to find interaction goal Target=%s"), *GetNameSafe(InteractableActor));
	return false;
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

void AAeyerjiPlayerController::Server_RequestLinkedTeleporterUse_Implementation(AActor* TeleporterActor, const uint8 EndpointIndex)
{
	AAeyerjiLinkedTeleporter* Teleporter = Cast<AAeyerjiLinkedTeleporter>(TeleporterActor);
	if (!IsValid(Teleporter))
	{
		AJ_LOG(this, TEXT("[PC-Server] Linked teleporter request failed - invalid actor"));
		return;
	}

	AJ_LOG(this, TEXT("[PC-Server] RequestLinkedTeleporterUse Teleporter=%s Endpoint=%d"),
		*GetNameSafe(Teleporter),
		static_cast<int32>(EndpointIndex));
	Teleporter->TryTeleport(this, EndpointIndex);
}

void AAeyerjiPlayerController::Server_RequestInteractableUse_Implementation(AActor* InteractableActor)
{
	if (!IsValid(InteractableActor) || !InteractableActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		AJ_LOG(this, TEXT("[Interaction][Server] Request failed: invalid interactable Target=%s"),
			*GetNameSafe(InteractableActor));
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		AJ_LOG(this, TEXT("[Interaction][Server] Request rejected: controller has no valid pawn Target=%s"),
			*GetNameSafe(InteractableActor));
		return;
	}

	const FVector InteractionLocation = IAeyerjiInteractable::Execute_GetInteractionLocation(InteractableActor);
	const float InteractionRadius = IAeyerjiInteractable::Execute_GetInteractionRadius(InteractableActor);
	const float Distance2D = FVector::Dist2D(ControlledPawn->GetActorLocation(), InteractionLocation);
	if (InteractionRadius > 0.f && Distance2D > InteractionRadius)
	{
		AJ_LOG(this, TEXT("[Interaction][Server] Request rejected: out of range Pawn=%s Target=%s Distance=%.1f Radius=%.1f"),
			*GetNameSafe(ControlledPawn),
			*GetNameSafe(InteractableActor),
			Distance2D,
			InteractionRadius);
		return;
	}

	AJ_LOG(this, TEXT("[Interaction][Server] Request accepted Target=%s Pawn=%s Distance=%.1f Radius=%.1f Unlimited=%d; executing interactable-owned validation"),
		*GetNameSafe(InteractableActor),
		*GetNameSafe(ControlledPawn),
		Distance2D,
		InteractionRadius,
		InteractionRadius <= 0.f ? 1 : 0);
	IAeyerjiInteractable::Execute_Interact(InteractableActor, this);
}

void AAeyerjiPlayerController::Server_RequestDefenseObjectiveRepair_Implementation(AActor* ObjectiveActor, const FName OptionId)
{
	if (!HasAuthority())
	{
		return;
	}

	AAeyerjiLevelDirector* Director = LevelDirector;
	if (!IsValid(Director))
	{
		RefreshZoneRuntimeReferences();
		Director = LevelDirector;
	}

	if (!IsValid(Director))
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
			{
				Director = *It;
				break;
			}
		}
	}

	if (Director)
	{
		Director->TryRepairSurvivalDefenseObjective(this, ObjectiveActor, OptionId);
	}
}

void AAeyerjiPlayerController::Client_ShowDefenseObjectiveRepairMenu_Implementation(
	AActor* ObjectiveActor,
	const TArray<FAeyerjiDefenseRepairOption>& RepairOptions,
	const int64 CurrentGold,
	const float CurrentHealth,
	const float MaxHealth)
{
	if (!IsLocalController())
	{
		return;
	}

	EnsureMissionHUD();
	if (MissionHUDWidget)
	{
		MissionHUDWidget->ShowDefenseObjectiveRepairMenu(ObjectiveActor, RepairOptions, CurrentGold, CurrentHealth, MaxHealth);
	}
}

void AAeyerjiPlayerController::Client_ShowMissionMessageKey_Implementation(const FName MessageKey, const float DisplaySeconds)
{
	if (!IsLocalController())
	{
		return;
	}

	EnsureMissionHUD();
	if (MissionHUDWidget)
	{
		MissionHUDWidget->ShowMissionMessageKey(MessageKey, DisplaySeconds);
	}
}

void AAeyerjiPlayerController::Server_SelectSurvivalUpgrade_Implementation(const FName OptionId, const int32 OfferRevision)
{
	if (!HasAuthority())
	{
		return;
	}

	AAeyerjiPlayerState* AeyerjiPS = GetPlayerState<AAeyerjiPlayerState>();
	if (!AeyerjiPS)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("Server_SelectSurvivalUpgrade: No AeyerjiPlayerState for controller %s"), *GetNameSafe(this));
		return;
	}

	AAeyerjiLevelDirector* Director = LevelDirector;
	if (!IsValid(Director))
	{
		RefreshZoneRuntimeReferences();
		Director = LevelDirector;
	}

	if (!IsValid(Director))
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
			{
				Director = *It;
				break;
			}
		}
	}

	if (!Director)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("Server_SelectSurvivalUpgrade: No LevelDirector found for player %s choosing %s (rev=%d)"),
			*GetNameSafe(AeyerjiPS), *OptionId.ToString(), OfferRevision);
		return;
	}

	const bool bAccepted = Director->SubmitSurvivalUpgradeChoice(AeyerjiPS, OptionId, OfferRevision);
	UE_LOG(LogAeyerji, Display, TEXT("Server_SelectSurvivalUpgrade: Player=%s Option=%s Rev=%d Accepted=%d"),
		*GetNameSafe(AeyerjiPS), *OptionId.ToString(), OfferRevision, bAccepted ? 1 : 0);
}

void AAeyerjiPlayerController::Server_SetDifficultySlider_Implementation(const float NewDifficultySlider)
{
	if (UWorld* World = GetWorld())
	{
		if (UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(World->GetGameInstance()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("PlayerController::Server_SetDifficultySlider RPC Value=%.2f Controller=%s"),
				NewDifficultySlider,
				*GetNameSafe(this));
			GI->SetWorldTier(UAeyerjiDifficultySettings::DifficultySliderToWorldTier(NewDifficultySlider));
		}
	}
}

void AAeyerjiPlayerController::Server_SetWorldTier_Implementation(const int32 NewWorldTier)
{
	if (UWorld* World = GetWorld())
	{
		if (UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(World->GetGameInstance()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("PlayerController::Server_SetWorldTier RPC Value=%d Controller=%s"),
				NewWorldTier,
				*GetNameSafe(this));
			GI->SetWorldTier(NewWorldTier);
		}
	}
}

void AAeyerjiPlayerController::Server_RequestZoneTransition_Implementation(const FName TargetZoneId)
{
	if (TargetZoneId.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerController::Server_RequestZoneTransition rejected None ZoneId."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			const bool bStarted = GS->Server_BeginWorldTransition(TargetZoneId);
			UE_LOG(LogTemp, Display,
				TEXT("PlayerController::Server_RequestZoneTransition ZoneId=%s Result=%s Controller=%s"),
				*TargetZoneId.ToString(),
				bStarted ? TEXT("started") : TEXT("failed"),
				*GetNameSafe(this));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("PlayerController::Server_RequestZoneTransition failed - GameState missing for ZoneId=%s"),
				*TargetZoneId.ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerController::Server_RequestZoneTransition failed - World missing."));
	}
}

void AAeyerjiPlayerController::Server_ReportZoneReady_Implementation(const int32 ReportedTransitionId)
{
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			GS->Server_ReportPlayerZoneReady(GetPlayerState<APlayerState>(), ReportedTransitionId);
		}
	}
}

void AAeyerjiPlayerController::BeginAbilityTargeting(const FAeyerjiAbilitySlot& Slot)
{
	EnsureTargetingManagerInitialized();
	if (TargetingManager)
	{
		TargetingManager->BeginTargeting(Slot);
	}
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

	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] StartMoveToActorLoop. Mode=StopOnly Target=%s AR=%.1f PreferBehind=%s Behind=%.1f Arc=%.1f Interval=%.3f %s %s"),
		*GetNameSafe(Target),
		MoveLoopAcceptanceRadius,
		BoolText(bMoveLoopPreferBehind),
		MoveLoopBehindDistance,
		MoveLoopArcHalfAngleDeg,
		MoveLoopInterval,
		*DescribePawnMovement(GetPawn()),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));

	TickMoveLoop();
	GetWorldTimerManager().SetTimer(MoveLoopTimer, this,
	                                &AAeyerjiPlayerController::TickMoveLoop, MoveLoopInterval, true);
}

void AAeyerjiPlayerController::StopMoveToActorLoop()
{
	const bool bHadMoveLoop = MoveLoopTarget.IsValid() || GetWorldTimerManager().IsTimerActive(MoveLoopTimer);
	if (bHadMoveLoop)
	{
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] StopMoveToActorLoop. Target=%s Mode=%s ArrivedBroadcast=%s %s"),
			*GetNameSafe(MoveLoopTarget.Get()),
			MoveLoopModeText(MoveLoopMode),
			BoolText(bMoveLoopArrivedBroadcast),
			*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
	}
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
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] TickMoveLoop stopped: invalid pawn/target. HasPawn=%s Target=%s"),
			BoolText(MyPawn != nullptr),
			*GetNameSafe(Target));
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
				AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] MoveLoop arrived. Mode=%s Target=%s Dist2D=%.1f Touching=%s AR=%.1f"),
					MoveLoopModeText(MoveLoopMode),
					*GetNameSafe(Target),
					Dist2D,
					BoolText(bTouching),
					MoveLoopAcceptanceRadius);
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
				AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] MoveLoop arrived. Mode=%s Target=%s Dist2D=%.1f Touching=%s AR=%.1f"),
					MoveLoopModeText(MoveLoopMode),
					*GetNameSafe(Target),
					Dist2D,
					BoolText(bTouching),
					MoveLoopAcceptanceRadius);
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
		if (bMoveLoopArrivedBroadcast)
		{
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] MoveLoop left acceptance bubble. Mode=%s Target=%s Dist2D=%.1f AR=%.1f Touching=%s"),
				MoveLoopModeText(MoveLoopMode),
				*GetNameSafe(Target),
				Dist2D,
				MoveLoopAcceptanceRadius,
				BoolText(bTouching));
		}
		bMoveLoopArrivedBroadcast = false;
	}

    // Compute a sensible goal and issue move (unchanged)
    FVector Goal;
    if (!ComputeSmartGoalForTarget(Target, bMoveLoopPreferBehind,
                                   MoveLoopBehindDistance, MoveLoopArcHalfAngleDeg, Goal))
    {
        AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] MoveLoop using target-location fallback goal. Mode=%s Target=%s TargetLoc=%s"),
            MoveLoopModeText(MoveLoopMode),
            *GetNameSafe(Target),
            *Target->GetActorLocation().ToCompactString());
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
        const float PendingGoalDelta = PendingMoveGoal.IsNearlyZero()
            ? -1.f
            : FVector::Dist2D(Goal, PendingMoveGoal);
        AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] MoveLoop issue move. Mode=%s Target=%s Dist2D=%.1f AR=%.1f Goal=%s PendingDelta=%.1f Avoidance=%s %s %s"),
            MoveLoopModeText(MoveLoopMode),
            *GetNameSafe(Target),
            Dist2D,
            MoveLoopAcceptanceRadius,
            *Goal.ToCompactString(),
            PendingGoalDelta,
            BoolText(bAvoidanceActive),
            *DescribePawnMovement(MyPawn),
            *DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
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

	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] StartFollowActorLoop. Mode=FollowOnly Target=%s AR=%.1f Behind=%.1f Arc=%.1f Interval=%.3f %s %s"),
		*GetNameSafe(Target),
		MoveLoopAcceptanceRadius,
		MoveLoopBehindDistance,
		MoveLoopArcHalfAngleDeg,
		MoveLoopInterval,
		*DescribePawnMovement(GetPawn()),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));

	TickMoveLoop();
	GetWorldTimerManager().SetTimer(MoveLoopTimer, this,
	                                &AAeyerjiPlayerController::TickMoveLoop, MoveLoopInterval, true);
}

void AAeyerjiPlayerController::ApplyGameplayMouseInputMode(bool bFlushInput)
{
	if (!IsLocalController())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Keep world clicks and UI clicks working at the same time without consuming the first mouse-down.
	UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
		this,
		nullptr,
		EMouseLockMode::LockOnCapture,
		/*bHideCursorDuringCapture=*/false,
		bFlushInput);
	if (UGameViewportClient* GameViewport = World->GetGameViewport())
	{
		GameViewport->SetMouseLockMode(EMouseLockMode::LockOnCapture);
		GameViewport->SetHideCursorDuringCapture(false);
		GameViewport->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
	}
	UWidgetBlueprintLibrary::SetFocusToGameViewport();

	bShowMouseCursor = true;
}

void AAeyerjiPlayerController::BeginPlay()
{
    Super::BeginPlay();
	EnableCheats();
	AddCheats(true);
    EnsureTargetingManagerInitialized();
	BindWorldFlowDelegates();
	RefreshZoneRuntimeReferences();
	ApplyGameplayMouseInputMode();

    if (IsLocalController())
    {
		EnsureViewportConsole();

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
	if (IMC_Default)
	{
		auto ResolvePhysicalKeyForAction = [this](const UInputAction* Action, FKey& InOutPhysicalKey, const TCHAR* Label)
		{
			if (!Action)
			{
				return;
			}

			FKey FirstMappedKey;
			for (const FEnhancedActionKeyMapping& Mapping : IMC_Default->GetMappings())
			{
				if (Mapping.Action != Action || !Mapping.Key.IsValid())
				{
					continue;
				}

				if (!FirstMappedKey.IsValid())
				{
					FirstMappedKey = Mapping.Key;
				}

				if (Mapping.Key.IsMouseButton())
				{
					InOutPhysicalKey = Mapping.Key;
					UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] %s physical key resolved from IMC_Default: %s"),
						Label,
						*InOutPhysicalKey.ToString());
					return;
				}
			}

			if (FirstMappedKey.IsValid())
			{
				InOutPhysicalKey = FirstMappedKey;
				UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] %s physical key resolved from IMC_Default fallback mapping: %s"),
					Label,
					*InOutPhysicalKey.ToString());
			}
		};

		ResolvePhysicalKeyForAction(IA_Attack_Click, AttackClickPhysicalKey, TEXT("IA_Attack_Click"));
		ResolvePhysicalKeyForAction(IA_Move_Click, MoveClickPhysicalKey, TEXT("IA_Move_Click"));
		ResolvePhysicalKeyForAction(IA_Interact, InteractClickPhysicalKey, TEXT("IA_Interact"));
	}

	if (auto* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (IA_Attack_Click)
		{
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnAttackClickPressed);
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Canceled,  this, &AAeyerjiPlayerController::OnAttackClickReleased);
			EIC->BindAction(IA_Attack_Click, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnAttackClickReleased);
			UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Bound IA_Attack_Click=%s to Started/Canceled/Completed."),
				*GetNameSafe(IA_Attack_Click));
		}
		else
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[MoveHold] IA_Attack_Click is null; left/action input will not bind."));
		}
		if (IA_Move_Click)
		{
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Started,   this, &AAeyerjiPlayerController::OnMoveClickPressed);
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Canceled,  this, &AAeyerjiPlayerController::OnMoveClickReleased);
			EIC->BindAction(IA_Move_Click, ETriggerEvent::Completed, this, &AAeyerjiPlayerController::OnMoveClickReleased);
			UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Bound IA_Move_Click=%s to Started/Canceled/Completed."),
				*GetNameSafe(IA_Move_Click));
		}
		else
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[MoveHold] IA_Move_Click is null; right-click movement input will not bind."));
		}
		if (IA_Interact)
		{
			if (InteractClickPhysicalKey.IsValid() && InteractClickPhysicalKey == AttackClickPhysicalKey)
			{
				UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] IA_Interact shares %s with IA_Attack_Click; binding both and suppressing duplicate same-key handling."),
					*InteractClickPhysicalKey.ToString());
			}
			EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &AAeyerjiPlayerController::OnInteractClickPressed);
			UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Input] Bound IA_Interact=%s to Started."),
				*GetNameSafe(IA_Interact));
		}
		else
		{
			UE_LOG(LogAeyerji, Warning, TEXT("[Interaction][Input] IA_Interact is null; create /Game/Player/Input/Actions/IA_Interact and map it if interaction click input is needed."));
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
		if (IA_CancelAction)
		{
			EIC->BindAction(IA_CancelAction, ETriggerEvent::Started, this, &AAeyerjiPlayerController::OnCancelActionPressed);
		}
	}
	else
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[MoveHold] InputComponent is not an EnhancedInputComponent; movement input bindings were not installed. InputComponent=%s"),
			*GetNameSafe(InputComponent));
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


void AAeyerjiPlayerController::ClearMouseCommandData()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MouseCommandRecoveryTimerHandle);
	}
	MouseCommand = FMouseCommandState();
	bMouseCommandPausedByAbilityCast = false;
	bMouseCommandRecoveryPending = false;
	RecoveryBlockedCommandSerial = 0;
	LastMouseAttackChaseLogTime = -1.0;
	LastMouseAttackRangeLogTime = -1.0;
}

void AAeyerjiPlayerController::CancelMouseOwnedMovement()
{
	PendingMoveTarget = nullptr;
	MouseCommand.IssuedMoveTarget = nullptr;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	LastCursorFollowClientDiagTime = -1.0;
	LastCursorFollowServerDiagTime = -1.0;
	ResetCursorFollowHold();
	ResetCursorFollowTurnRate();
	if (IsLocalController() && !HasAuthority())
	{
		Server_ResetCursorFollowTurnRate();
	}
}

void AAeyerjiPlayerController::CancelMouseOwnedCombat()
{
	MouseCommand.IssuedMoveTarget = nullptr;
	CachedTarget = nullptr;
	PendingMoveTarget = nullptr;
	CancelFaceActor();
	EnsureLocomotionRotationMode();
}

void AAeyerjiPlayerController::CancelMouseOwnedInteraction()
{
	StopPendingTeleporter();
	StopPendingInteraction();
}

void AAeyerjiPlayerController::CancelMouseCommandCompletely()
{
	CancelMouseOwnedMovement();
	CancelMouseOwnedCombat();
	CancelMouseOwnedInteraction();
	ClearMouseCommandData();
	bMoveClickHeld = false;
	bAttackClickHeld = false;
}

bool AAeyerjiPlayerController::IsMouseButtonPhysicallyDown(const EAeyerjiMouseButton Button) const
{
	switch (Button)
	{
	case EAeyerjiMouseButton::Left:
		return IsInputKeyDown(AttackClickPhysicalKey);
	case EAeyerjiMouseButton::Right:
		return IsInputKeyDown(MoveClickPhysicalKey);
	default:
		return false;
	}
}

bool AAeyerjiPlayerController::TryResolveDirectHostileUnderCursor(FHitResult& OutHit, AActor*& OutTarget) const
{
	OutTarget = nullptr;
	if (ResolveDirectAttackTargetUnderCursor(OutHit))
	{
		AActor* HitActor = ResolveAttackableActorFromCursorHit(OutHit);
		if (HitActor)
		{
			OutTarget = HitActor;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Hostile resolved from PawnCustom trace. Target=%s HitActor=%s Component=%s Impact=%s"),
				*GetNameSafe(OutTarget),
				*GetNameSafe(OutHit.GetActor()),
				*GetNameSafe(OutHit.GetComponent()),
				*OutHit.ImpactPoint.ToCompactString());
			return true;
		}
	}

	FHitResult GroundHit;
	if (TryGetGroundHit(GroundHit))
	{
		if (AActor* GroundTarget = ResolveAttackableActorFromCursorHit(GroundHit))
		{
			OutHit = GroundHit;
			OutTarget = GroundTarget;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Hostile resolved from Ground trace fallback. Target=%s HitActor=%s Component=%s Impact=%s"),
				*GetNameSafe(OutTarget),
				*GetNameSafe(GroundHit.GetActor()),
				*GetNameSafe(GroundHit.GetComponent()),
				*GroundHit.ImpactPoint.ToCompactString());
			return true;
		}
	}

	FHitResult VisibilityHit;
	if (TraceCursor(ECC_Visibility, VisibilityHit, /*bTraceComplex=*/false))
	{
		if (AActor* VisibilityTarget = ResolveAttackableActorFromCursorHit(VisibilityHit))
		{
			OutHit = VisibilityHit;
			OutTarget = VisibilityTarget;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Hostile resolved from Visibility trace fallback. Target=%s HitActor=%s Component=%s Impact=%s"),
				*GetNameSafe(OutTarget),
				*GetNameSafe(VisibilityHit.GetActor()),
				*GetNameSafe(VisibilityHit.GetComponent()),
				*VisibilityHit.ImpactPoint.ToCompactString());
			return true;
		}
	}

	FHitResult ForgivingHit;
	if (ResolveAttackTargetUnderCursor(ForgivingHit))
	{
		if (AActor* ForgivingTarget = ResolveAttackableActorFromCursorHit(ForgivingHit))
		{
			OutHit = ForgivingHit;
			OutTarget = ForgivingTarget;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Hostile resolved from hover/snap fallback. Target=%s HitActor=%s Component=%s Impact=%s"),
				*GetNameSafe(OutTarget),
				*GetNameSafe(ForgivingHit.GetActor()),
				*GetNameSafe(ForgivingHit.GetComponent()),
				*ForgivingHit.ImpactPoint.ToCompactString());
			return true;
		}
	}

	UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] No hostile resolved under cursor. GroundHit=%s GroundActor=%s GroundComponent=%s"),
		BoolText(GroundHit.bBlockingHit),
		*GetNameSafe(GroundHit.GetActor()),
		*GetNameSafe(GroundHit.GetComponent()));
	return false;
}

AActor* AAeyerjiPlayerController::ResolveAttackableActorFromCursorHit(const FHitResult& Hit) const
{
	if (AActor* HitActor = Hit.GetActor())
	{
		if (IsAttackableActor(HitActor))
		{
			return HitActor;
		}

		for (AActor* OwnerActor = HitActor->GetOwner(); OwnerActor; OwnerActor = OwnerActor->GetOwner())
		{
			if (IsAttackableActor(OwnerActor))
			{
				return OwnerActor;
			}
		}

		if (AActor* AttachedParent = HitActor->GetAttachParentActor())
		{
			if (IsAttackableActor(AttachedParent))
			{
				return AttachedParent;
			}
		}
	}

	if (const UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		if (AActor* ComponentOwner = HitComponent->GetOwner())
		{
			if (IsAttackableActor(ComponentOwner))
			{
				return ComponentOwner;
			}

			for (AActor* OwnerActor = ComponentOwner->GetOwner(); OwnerActor; OwnerActor = OwnerActor->GetOwner())
			{
				if (IsAttackableActor(OwnerActor))
				{
					return OwnerActor;
				}
			}
		}
	}

	return nullptr;
}

bool AAeyerjiPlayerController::IsMouseCommandTargetInBasicAttackRange(AActor* TargetActor) const
{
	const APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn || !TargetActor)
	{
		return false;
	}

	const float AttackRange = UCharacterStatsLibrary::GetAttackRangeFromActorASC(ControlledPawn, 150.f);
	const float StopRange = FMath::Max(0.f, AttackRange * PrimaryAttackMoveStopAtPercentOfRange + PrimaryAttackMoveStopExtraBufferCm);
	return FVector::DistSquared2D(ControlledPawn->GetActorLocation(), TargetActor->GetActorLocation()) <= FMath::Square(StopRange);
}

void AAeyerjiPlayerController::EnsureMouseActorChase(AActor* TargetActor)
{
	if (!IsAttackableActor(TargetActor) || IsControlledPawnDead())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Chase skipped: invalid target or dead pawn. Target=%s Pawn=%s Dead=%s"),
			*GetNameSafe(TargetActor),
			*GetNameSafe(GetPawn()),
			BoolText(IsControlledPawnDead()));
		return;
	}

	if (HandleMovementBlockedByAbilities())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Chase queued by ability movement block. Target=%s"),
			*GetNameSafe(TargetActor));
		QueueMovementCommand(TargetActor, /*bIsContinuous=*/true);
		return;
	}

	EnsureLocomotionRotationMode();
	CancelFaceActor();
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;

	UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
	const bool bPathIdle = !PFC || PFC->GetStatus() == EPathFollowingStatus::Idle;
	if (MouseCommand.IssuedMoveTarget.Get() != TargetActor || bPathIdle)
	{
		MouseCommand.IssuedMoveTarget = TargetActor;
		IssueMoveRPC(TargetActor);
		const APawn* ControlledPawn = GetPawn();
		const float Dist2D = ControlledPawn ? FVector::Dist2D(ControlledPawn->GetActorLocation(), TargetActor->GetActorLocation()) : -1.f;
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Chase issued. Target=%s Idle=%s Dist2D=%.1f PFC=%s"),
			*GetNameSafe(TargetActor),
			BoolText(bPathIdle),
			Dist2D,
			*DescribePathFollowing(PFC));
	}
	else if (const UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		if (LastMouseAttackChaseLogTime < 0.0 || (Now - LastMouseAttackChaseLogTime) >= 0.25)
		{
			LastMouseAttackChaseLogTime = Now;
			const APawn* ControlledPawn = GetPawn();
			const float Dist2D = ControlledPawn ? FVector::Dist2D(ControlledPawn->GetActorLocation(), TargetActor->GetActorLocation()) : -1.f;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Chase continuing without reissue. Target=%s Dist2D=%.1f IssuedTarget=%s %s"),
				*GetNameSafe(TargetActor),
				Dist2D,
				*GetNameSafe(MouseCommand.IssuedMoveTarget.Get()),
				*DescribePathFollowing(PFC));
		}
	}
}

void AAeyerjiPlayerController::StartMouseGroundMove(const FVector& Goal, const bool bSpawnCursorFX)
{
	if (!GetPawn() || IsControlledPawnDead())
	{
		return;
	}

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, bSpawnCursorFX, /*bIsContinuous=*/false);
		return;
	}

	EnsureLocomotionRotationMode();
	PendingMoveTarget = nullptr;
	MouseCommand.IssuedMoveTarget = nullptr;
	IssueMoveRPC(Goal);
	BeginCursorFollowHold(Goal);
	if (bSpawnCursorFX)
	{
		SpawnCursorFX(Goal);
	}
}

void AAeyerjiPlayerController::UpdateMouseGroundMove(const FVector& Goal)
{
	if (!GetPawn() || IsControlledPawnDead())
	{
		return;
	}

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		return;
	}

	EnsureLocomotionRotationMode();
	PendingMoveTarget = nullptr;
	MouseCommand.IssuedMoveTarget = nullptr;
	UpdateContinuousMoveGoal(Goal);
}

void AAeyerjiPlayerController::TransitionMouseIntent(const EAeyerjiMouseIntent NewIntent, AActor* NewTarget, const FVector& NewGroundGoal, const bool bSpawnMoveFx)
{
	const EAeyerjiMouseIntent OldIntent = MouseCommand.Intent;
	AActor* OldTarget = MouseCommand.TargetActor.Get();
	const bool bTargetChanged = OldTarget != NewTarget;
	const bool bIntentChanged = OldIntent != NewIntent;

	if (bIntentChanged || bTargetChanged)
	{
		if (OldIntent == EAeyerjiMouseIntent::BasicAttack)
		{
			CancelMouseOwnedCombat();
		}
		else if (OldIntent == EAeyerjiMouseIntent::GroundMove)
		{
			CancelMouseOwnedMovement();
		}
	}

	MouseCommand.Intent = NewIntent;
	MouseCommand.TargetActor = NewTarget;
	MouseCommand.GroundGoal = NewGroundGoal;

	if (bIntentChanged || bTargetChanged)
	{
		MouseCommand.bAttackCommitted = false;
		MouseCommand.LastAttackAttemptTime = -1.0;
	}

	switch (NewIntent)
	{
	case EAeyerjiMouseIntent::GroundMove:
		if (bIntentChanged || bTargetChanged)
		{
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Intent -> GroundMove. Owner=%d Phase=%d OldIntent=%d OldTarget=%s Goal=%s SpawnFX=%s"),
				static_cast<int32>(MouseCommand.Owner),
				static_cast<int32>(MouseCommand.Phase),
				static_cast<int32>(OldIntent),
				*GetNameSafe(OldTarget),
				*NewGroundGoal.ToCompactString(),
				BoolText(bSpawnMoveFx));
		}
		CancelMouseOwnedCombat();
		CancelMouseOwnedInteraction();
		if (bIntentChanged || bTargetChanged)
		{
			StartMouseGroundMove(NewGroundGoal, bSpawnMoveFx);
		}
		else
		{
			UpdateMouseGroundMove(NewGroundGoal);
		}
		break;
	case EAeyerjiMouseIntent::BasicAttack:
		if (bIntentChanged || bTargetChanged)
		{
			const APawn* ControlledPawn = GetPawn();
			const float Dist2D = (ControlledPawn && NewTarget) ? FVector::Dist2D(ControlledPawn->GetActorLocation(), NewTarget->GetActorLocation()) : -1.f;
			UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Intent -> BasicAttack. Owner=%d Phase=%d OldIntent=%d OldTarget=%s Target=%s Dist2D=%.1f"),
				static_cast<int32>(MouseCommand.Owner),
				static_cast<int32>(MouseCommand.Phase),
				static_cast<int32>(OldIntent),
				*GetNameSafe(OldTarget),
				*GetNameSafe(NewTarget),
				Dist2D);
		}
		CancelMouseOwnedMovement();
		CancelMouseOwnedInteraction();
		if (!IsMouseCommandTargetInBasicAttackRange(NewTarget))
		{
			EnsureMouseActorChase(NewTarget);
		}
		break;
	case EAeyerjiMouseIntent::Interaction:
	case EAeyerjiMouseIntent::SuppressedUntilRelease:
		CancelMouseOwnedMovement();
		CancelMouseOwnedCombat();
		break;
	case EAeyerjiMouseIntent::None:
	default:
		CancelMouseCommandCompletely();
		break;
	}
}

void AAeyerjiPlayerController::BeginMouseCommand(const EAeyerjiMouseButton Button)
{
	if (Button == EAeyerjiMouseButton::Left && WasSameKeyInteractionHandledRecently())
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Input] Suppressed duplicate left-click path after same-key interaction handling."));
		ClearMouseCommandData();
		MouseCommand.Owner = Button;
		MouseCommand.Phase = EAeyerjiMousePhase::Held;
		MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
		bAttackClickHeld = true;
		return;
	}

	CancelMouseCommandCompletely();

	MouseCommand.Owner = Button;
	MouseCommand.Phase = EAeyerjiMousePhase::Held;
	MouseCommand.Intent = EAeyerjiMouseIntent::None;
	MouseCommand.CommandSerial = NextMouseCommandSerial++;
	bAttackClickHeld = Button == EAeyerjiMouseButton::Left;
	bMoveClickHeld = Button == EAeyerjiMouseButton::Right;

	if (IsGameplayInputSuppressedByModalUI())
	{
		MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
		return;
	}

	if (IsAbilityCastInputLocked())
	{
		if (Button == EAeyerjiMouseButton::Right)
		{
			FHitResult GroundHit;
			if (TryGetGroundHit(GroundHit))
			{
				MouseCommand.Intent = EAeyerjiMouseIntent::GroundMove;
				MouseCommand.GroundGoal = GroundHit.ImpactPoint;
				bMouseCommandPausedByAbilityCast = true;
				QueueMovementCommand(GroundHit.ImpactPoint, /*bSpawnCursorFX=*/true, /*bIsContinuous=*/false);
				UE_LOG(LogAeyerji, Log, TEXT("[MoveQueue] Queued move click during ability cast. Goal=%s Held=%s"),
					*GroundHit.ImpactPoint.ToCompactString(),
					BoolText(MouseCommand.Phase == EAeyerjiMousePhase::Held));
				return;
			}
		}

		MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
		return;
	}

	if (Button == EAeyerjiMouseButton::Left)
	{
		EnsureTargetingManagerInitialized();
		if (TargetingManager && TargetingManager->IsTargeting() && TargetingManager->HandleClick(BuildTargetingClickContext()))
		{
			MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
			return;
		}

		FHitResult TeleporterHit;
		AAeyerjiLinkedTeleporter* LinkedTeleporter = nullptr;
		uint8 LinkedTeleporterEndpointIndex = 0;
		if (TryGetLinkedTeleporterHit(TeleporterHit, LinkedTeleporter, LinkedTeleporterEndpointIndex))
		{
			MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
			MarkSameKeyInteractionHandled();
			UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Contextual left click found linked teleporter Target=%s Endpoint=%d"),
				*GetNameSafe(LinkedTeleporter),
				static_cast<int32>(LinkedTeleporterEndpointIndex));
			HandleLinkedTeleporterUnderCursor(LinkedTeleporter, LinkedTeleporterEndpointIndex, TeleporterHit);
			return;
		}

		FHitResult InteractableHit;
		AActor* InteractableActor = nullptr;
		if (TryGetInteractableHit(InteractableHit, InteractableActor))
		{
			MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
			MarkSameKeyInteractionHandled();
			UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Contextual left click found interactable Target=%s HitActor=%s Component=%s Impact=%s"),
				*GetNameSafe(InteractableActor),
				*GetNameSafe(InteractableHit.GetActor()),
				*GetNameSafe(InteractableHit.GetComponent()),
				*InteractableHit.ImpactPoint.ToCompactString());
			HandleInteractableUnderCursor(InteractableActor, InteractableHit);
			return;
		}
	}

	FHitResult HostileHit;
	AActor* HostileTarget = nullptr;
	if (Button == EAeyerjiMouseButton::Left && TryResolveDirectHostileUnderCursor(HostileHit, HostileTarget))
	{
		if (TryConsumePawnHit(HostileHit))
		{
			MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
			return;
		}

		TransitionMouseIntent(EAeyerjiMouseIntent::BasicAttack, HostileTarget, FVector::ZeroVector, /*bSpawnMoveFx=*/false);
		UpdateMouseCommand(0.f);
		return;
	}

	FHitResult GroundHit;
	if (TryGetGroundHit(GroundHit))
	{
		TransitionMouseIntent(EAeyerjiMouseIntent::GroundMove, nullptr, GroundHit.ImpactPoint, /*bSpawnMoveFx=*/true);
		return;
	}

	MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
}

void AAeyerjiPlayerController::ReleaseMouseCommand(const EAeyerjiMouseButton Button)
{
	if (MouseCommand.Owner == Button)
	{
		RecoveryBlockedCommandSerial = 0;
		bMouseCommandRecoveryPending = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(MouseCommandRecoveryTimerHandle);
		}
	}
	if (Button == EAeyerjiMouseButton::Left)
	{
		bAttackClickHeld = false;
	}
	else if (Button == EAeyerjiMouseButton::Right)
	{
		bMoveClickHeld = false;
	}

	if (MouseCommand.Owner != Button)
	{
		return;
	}

	if (MouseCommand.Intent == EAeyerjiMouseIntent::BasicAttack && !MouseCommand.bAttackCommitted && MouseCommand.TargetActor.IsValid())
	{
		MouseCommand.Phase = EAeyerjiMousePhase::ReleasedPendingAttack;
		return;
	}

	if (MouseCommand.Intent == EAeyerjiMouseIntent::GroundMove)
	{
		CancelMouseOwnedMovement();
	}

	ClearMouseCommandData();
}

void AAeyerjiPlayerController::UpdateMouseCommand(const float DeltaSeconds)
{
	if (MouseCommand.Owner == EAeyerjiMouseButton::None)
	{
		return;
	}

	if (MouseCommand.Phase == EAeyerjiMousePhase::Held && !IsMouseButtonPhysicallyDown(MouseCommand.Owner))
	{
		ReleaseMouseCommand(MouseCommand.Owner);
	}

	if (MouseCommand.Owner == EAeyerjiMouseButton::None)
	{
		return;
	}

	if (MouseCommand.Intent == EAeyerjiMouseIntent::SuppressedUntilRelease || MouseCommand.Intent == EAeyerjiMouseIntent::Interaction)
	{
		return;
	}

	if (IsAbilityCastInputLocked())
	{
		if (!bMouseCommandPausedByAbilityCast)
		{
			bMouseCommandPausedByAbilityCast = true;
			MouseCommand.IssuedMoveTarget = nullptr;

			UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MouseCommand] Command execution paused by ability cast. Owner=%d Intent=%d Target=%s"),
				static_cast<int32>(MouseCommand.Owner),
				static_cast<int32>(MouseCommand.Intent),
				*GetNameSafe(MouseCommand.TargetActor.Get()));
		}

		if (MouseCommand.Intent == EAeyerjiMouseIntent::GroundMove && MouseCommand.Phase == EAeyerjiMousePhase::Held)
		{
			FHitResult GroundHit;
			if (TryGetGroundHit(GroundHit))
			{
				const float GoalDelta = FVector::Dist2D(MouseCommand.GroundGoal, GroundHit.ImpactPoint);
				MouseCommand.GroundGoal = GroundHit.ImpactPoint;
				QueueMovementCommand(GroundHit.ImpactPoint, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);

				if (GoalDelta >= CursorFollowRepathDistance)
				{
					UE_LOG(LogAeyerji, Log, TEXT("[MoveQueue] Updated queued held move during ability cast. Goal=%s Delta=%.1f"),
						*GroundHit.ImpactPoint.ToCompactString(),
						GoalDelta);
				}
			}
		}

		return;
	}

	if (bMouseCommandPausedByAbilityCast)
	{
		bMouseCommandPausedByAbilityCast = false;
		MouseCommand.IssuedMoveTarget = nullptr;
		MouseCommand.LastAttackAttemptTime = -1.0;

		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MouseCommand] Command execution resumed after ability cast. Owner=%d Intent=%d Target=%s"),
			static_cast<int32>(MouseCommand.Owner),
			static_cast<int32>(MouseCommand.Intent),
			*GetNameSafe(MouseCommand.TargetActor.Get()));
	}

	if (MouseCommand.Phase == EAeyerjiMousePhase::Held)
	{
		FHitResult HostileHit;
		AActor* HostileTarget = nullptr;
		if (MouseCommand.Owner == EAeyerjiMouseButton::Left && TryResolveDirectHostileUnderCursor(HostileHit, HostileTarget))
		{
			TransitionMouseIntent(EAeyerjiMouseIntent::BasicAttack, HostileTarget, FVector::ZeroVector, /*bSpawnMoveFx=*/false);
		}
		else
		{
			FHitResult GroundHit;
			if (TryGetGroundHit(GroundHit))
			{
				TransitionMouseIntent(EAeyerjiMouseIntent::GroundMove, nullptr, GroundHit.ImpactPoint, /*bSpawnMoveFx=*/false);
			}
		}
	}

	if (MouseCommand.Intent == EAeyerjiMouseIntent::GroundMove)
	{
		if (MouseCommand.Phase == EAeyerjiMousePhase::Held)
		{
			UpdateMouseGroundMove(MouseCommand.GroundGoal);
		}
		return;
	}

	if (MouseCommand.Intent != EAeyerjiMouseIntent::BasicAttack)
	{
		return;
	}

	AActor* TargetActor = MouseCommand.TargetActor.Get();
	if (!IsAttackableActor(TargetActor))
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Target invalidated. Owner=%d Phase=%d Held=%s Target=%s"),
			static_cast<int32>(MouseCommand.Owner),
			static_cast<int32>(MouseCommand.Phase),
			BoolText(MouseCommand.Phase == EAeyerjiMousePhase::Held),
			*GetNameSafe(TargetActor));

		if (MouseCommand.Phase == EAeyerjiMousePhase::Held)
		{
			FHitResult HostileHit;
			AActor* NewTarget = nullptr;
			if (TryResolveDirectHostileUnderCursor(HostileHit, NewTarget))
			{
				TransitionMouseIntent(EAeyerjiMouseIntent::BasicAttack, NewTarget, FVector::ZeroVector, /*bSpawnMoveFx=*/false);
				return;
			}

			FHitResult GroundHit;
			if (TryGetGroundHit(GroundHit))
			{
				TransitionMouseIntent(EAeyerjiMouseIntent::GroundMove, nullptr, GroundHit.ImpactPoint, /*bSpawnMoveFx=*/false);
				return;
			}
		}

		ClearMouseCommandData();
		return;
	}

	if (!IsMouseCommandTargetInBasicAttackRange(TargetActor))
	{
		const APawn* ControlledPawn = GetPawn();
		const float AttackRange = UCharacterStatsLibrary::GetAttackRangeFromActorASC(ControlledPawn, 150.f);
		const float StopRange = FMath::Max(0.f, AttackRange * PrimaryAttackMoveStopAtPercentOfRange + PrimaryAttackMoveStopExtraBufferCm);
		const float Dist2D = ControlledPawn ? FVector::Dist2D(ControlledPawn->GetActorLocation(), TargetActor->GetActorLocation()) : -1.f;
		if (const UWorld* World = GetWorld())
		{
			const double Now = World->GetTimeSeconds();
			if (LastMouseAttackRangeLogTime < 0.0 || (Now - LastMouseAttackRangeLogTime) >= 0.25)
			{
				LastMouseAttackRangeLogTime = Now;
				UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Out of range, chasing. Target=%s Dist2D=%.1f AttackRange=%.1f StopRange=%.1f Phase=%d Committed=%s"),
					*GetNameSafe(TargetActor),
					Dist2D,
					AttackRange,
					StopRange,
					static_cast<int32>(MouseCommand.Phase),
					BoolText(MouseCommand.bAttackCommitted));
			}
		}
		EnsureMouseActorChase(TargetActor);
		return;
	}

	if (!MouseCommand.bAttackCommitted)
	{
		const APawn* ControlledPawn = GetPawn();
		const float AttackRange = UCharacterStatsLibrary::GetAttackRangeFromActorASC(ControlledPawn, 150.f);
		const float StopRange = FMath::Max(0.f, AttackRange * PrimaryAttackMoveStopAtPercentOfRange + PrimaryAttackMoveStopExtraBufferCm);
		const float Dist2D = ControlledPawn ? FVector::Dist2D(ControlledPawn->GetActorLocation(), TargetActor->GetActorLocation()) : -1.f;
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] In range, aborting movement before attack. Target=%s Dist2D=%.1f AttackRange=%.1f StopRange=%.1f Phase=%d"),
			*GetNameSafe(TargetActor),
			Dist2D,
			AttackRange,
			StopRange,
			static_cast<int32>(MouseCommand.Phase));
		AbortMovement_Both();
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (MouseCommand.LastAttackAttemptTime >= 0.0 && (Now - MouseCommand.LastAttackAttemptTime) < BasicAttackRetryInterval)
	{
		return;
	}

	if (IsAbilityCastInputLocked())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Attack attempt blocked by ability cast lock. Target=%s Phase=%d"),
			*GetNameSafe(TargetActor),
			static_cast<int32>(MouseCommand.Phase));
		return;
	}

	MouseCommand.LastAttackAttemptTime = Now;
	if (ActivatePrimaryAttackAbility(TargetActor))
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] ActivatePrimaryAttackAbility returned true. Target=%s Phase=%d Held=%s"),
			*GetNameSafe(TargetActor),
			static_cast<int32>(MouseCommand.Phase),
			BoolText(MouseCommand.Phase == EAeyerjiMousePhase::Held));
		MouseCommand.bAttackCommitted = true;
		if (MouseCommand.Phase == EAeyerjiMousePhase::ReleasedPendingAttack)
		{
			ClearMouseCommandData();
		}
	}
	else
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] ActivatePrimaryAttackAbility returned false. Target=%s Phase=%d"),
			*GetNameSafe(TargetActor),
			static_cast<int32>(MouseCommand.Phase));
	}
}

void AAeyerjiPlayerController::BindMouseCommandRecoveryDelegates()
{
	if (!IsLocalController())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetControlledAbilitySystem();
	if (!ASC || MouseCommandRecoveryASC.Get() == ASC)
	{
		return;
	}

	UnbindMouseCommandRecoveryDelegates();
	MouseCommandRecoveryASC = ASC;
	ObservedAbilityEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &AAeyerjiPlayerController::HandleObservedAbilityEnded);
	CastingTagChangedHandle = ASC->RegisterGameplayTagEvent(
		AeyerjiTags::State_Ability_Casting,
		EGameplayTagEventType::NewOrRemoved).AddUObject(this, &AAeyerjiPlayerController::HandleCastingTagChanged);
}

void AAeyerjiPlayerController::UnbindMouseCommandRecoveryDelegates()
{
	if (UAbilitySystemComponent* ASC = MouseCommandRecoveryASC.Get())
	{
		if (ObservedAbilityEndedHandle.IsValid())
		{
			ASC->OnAbilityEnded.Remove(ObservedAbilityEndedHandle);
		}
		if (CastingTagChangedHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(
				AeyerjiTags::State_Ability_Casting,
				EGameplayTagEventType::NewOrRemoved).Remove(CastingTagChangedHandle);
		}
	}
	ObservedAbilityEndedHandle.Reset();
	CastingTagChangedHandle.Reset();
	MouseCommandRecoveryASC.Reset();
}

void AAeyerjiPlayerController::HandleObservedAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (!bMouseCommandPausedByAbilityCast && !bMouseCommandRecoveryPending)
	{
		return;
	}

	LocalAbilityCastInputLockEndTime = -1.0;
	if (EndedData.bWasCancelled)
	{
		UE_LOG(LogAeyerji, VeryVerbose,
			TEXT("[MouseCommand] Ability cancellation blocked command recovery. Serial=%u Intent=%d"),
			MouseCommand.CommandSerial, static_cast<int32>(MouseCommand.Intent));
		CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/true);
		return;
	}

	ScheduleMouseCommandRecovery();
}

void AAeyerjiPlayerController::HandleCastingTagChanged(const FGameplayTag Tag, const int32 NewCount)
{
	static_cast<void>(Tag);
	if (NewCount > 0 || (!bMouseCommandPausedByAbilityCast && !bMouseCommandRecoveryPending))
	{
		return;
	}

	LocalAbilityCastInputLockEndTime = -1.0;
	ScheduleMouseCommandRecovery();
}

void AAeyerjiPlayerController::ScheduleMouseCommandRecovery()
{
	if (!IsLocalController() || MouseCommand.Owner == EAeyerjiMouseButton::None
		|| RecoveryBlockedCommandSerial == MouseCommand.CommandSerial)
	{
		return;
	}

	bMouseCommandRecoveryPending = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MouseCommandRecoveryTimerHandle);
		World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiPlayerController::RecoverMouseCommandAfterAbility);
	}
}

void AAeyerjiPlayerController::RecoverMouseCommandAfterAbility()
{
	bMouseCommandRecoveryPending = false;
	if (!IsLocalController() || MouseCommand.Owner == EAeyerjiMouseButton::None
		|| MouseCommand.Phase != EAeyerjiMousePhase::Held
		|| !IsMouseButtonPhysicallyDown(MouseCommand.Owner)
		|| RecoveryBlockedCommandSerial == MouseCommand.CommandSerial)
	{
		return;
	}
	const bool bModalUI = IsGameplayInputSuppressedByModalUI();
	const bool bPawnDead = IsControlledPawnDead();
	const bool bInteractionIntent = MouseCommand.Intent == EAeyerjiMouseIntent::Interaction
		|| MouseCommand.Intent == EAeyerjiMouseIntent::SuppressedUntilRelease;
	if (!AeyerjiRiftRules::ShouldRecoverHeldCommand(
		/*bButtonStillHeld=*/true,
		/*bAbilityCancelled=*/false,
		bPawnDead,
		bModalUI,
		bInteractionIntent,
		/*bTargetOrGroundValid=*/true))
	{
		CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/true);
		return;
	}
	if (IsAbilityCastInputLocked())
	{
		ScheduleMouseCommandRecovery();
		return;
	}
	const EAeyerjiMouseIntent PausedIntent = MouseCommand.Intent;
	AActor* PausedTarget = MouseCommand.TargetActor.Get();
	bMouseCommandPausedByAbilityCast = false;
	MouseCommand.IssuedMoveTarget = nullptr;
	MouseCommand.LastAttackAttemptTime = -1.0;
	MouseCommand.bAttackCommitted = false;

	if (MouseCommand.Owner == EAeyerjiMouseButton::Left)
	{
		FHitResult HostileHit;
		AActor* CurrentHostile = nullptr;
		if (TryResolveDirectHostileUnderCursor(HostileHit, CurrentHostile))
		{
			TransitionMouseIntent(EAeyerjiMouseIntent::BasicAttack, CurrentHostile, FVector::ZeroVector, false);
			return;
		}
		if (PausedIntent == EAeyerjiMouseIntent::BasicAttack && !IsAttackableActor(PausedTarget))
		{
			CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/true);
			return;
		}
	}

	FHitResult GroundHit;
	if (TryGetGroundHit(GroundHit))
	{
		TransitionMouseIntent(EAeyerjiMouseIntent::GroundMove, nullptr, GroundHit.ImpactPoint, false);
		return;
	}

	CancelMouseCommandRecovery(/*bSuppressCurrentCommandUntilRelease=*/true);
}

void AAeyerjiPlayerController::CancelMouseCommandRecovery(const bool bSuppressCurrentCommandUntilRelease)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MouseCommandRecoveryTimerHandle);
	}
	bMouseCommandRecoveryPending = false;
	bMouseCommandPausedByAbilityCast = false;
	LocalAbilityCastInputLockEndTime = -1.0;
	CancelMouseOwnedMovement();
	CancelMouseOwnedCombat();
	ClearQueuedMovementCommand();

	if (bSuppressCurrentCommandUntilRelease && MouseCommand.Owner != EAeyerjiMouseButton::None)
	{
		RecoveryBlockedCommandSerial = MouseCommand.CommandSerial;
		MouseCommand.Intent = EAeyerjiMouseIntent::SuppressedUntilRelease;
		MouseCommand.TargetActor.Reset();
		MouseCommand.IssuedMoveTarget.Reset();
		MouseCommand.bAttackCommitted = false;
	}
}

bool AAeyerjiPlayerController::IsAbilityCastInputLocked() const
{
	if (const UWorld* World = GetWorld())
	{
		if (LocalAbilityCastInputLockEndTime > World->GetTimeSeconds())
		{
			return true;
		}
	}

	APawn* ControlledPawn = GetPawn();
	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(ControlledPawn);
	const UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	return ASC && ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting);
}

bool AAeyerjiPlayerController::IsInteractClickMappedToAttackClick() const
{
	return InteractClickPhysicalKey.IsValid() && InteractClickPhysicalKey == AttackClickPhysicalKey;
}

bool AAeyerjiPlayerController::WasSameKeyInteractionHandledRecently() const
{
	if (!IsInteractClickMappedToAttackClick() || LastSameKeyInteractionHandledTime < 0.0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();
	return (Now - LastSameKeyInteractionHandledTime) <= 0.05;
}

void AAeyerjiPlayerController::MarkSameKeyInteractionHandled()
{
	if (!IsInteractClickMappedToAttackClick())
	{
		return;
	}

	const UWorld* World = GetWorld();
	LastSameKeyInteractionHandledTime = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();
}

void AAeyerjiPlayerController::OnAttackClickPressed(const FInputActionValue&)
{
	BeginMouseCommand(EAeyerjiMouseButton::Left);
}

void AAeyerjiPlayerController::OnAttackClickHeld(const FInputActionValue&)
{
	// Held mouse commands are updated from Tick so Enhanced Input trigger cadence cannot own movement.
}

void AAeyerjiPlayerController::OnAttackClickReleased(const FInputActionValue&)
{
	const bool bPhysicalButtonStillDown = IsInputKeyDown(AttackClickPhysicalKey);
	if (bPhysicalButtonStillDown && bAttackClickHeld)
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MouseCommand] Ignoring premature left release/completed while physical key is still down. Key=%s"),
			*AttackClickPhysicalKey.ToString());
		return;
	}

	ReleaseMouseCommand(EAeyerjiMouseButton::Left);
}

void AAeyerjiPlayerController::OnInteractClickPressed(const FInputActionValue&)
{
	if (WasSameKeyInteractionHandledRecently())
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Input] Ignored duplicate same-key interaction press."));
		return;
	}

	if (IsGameplayInputSuppressedByModalUI())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Press ignored: gameplay input suppressed."));
		return;
	}

	if (IsAbilityCastInputLocked())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Press ignored: ability cast lock active."));
		return;
	}

	UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Input] IA_Interact press received."));

	FHitResult TeleporterHit;
	AAeyerjiLinkedTeleporter* LinkedTeleporter = nullptr;
	uint8 LinkedTeleporterEndpointIndex = 0;
	if (TryGetLinkedTeleporterHit(TeleporterHit, LinkedTeleporter, LinkedTeleporterEndpointIndex))
	{
		ClearAttackInputIntent();
		if (HandleLinkedTeleporterUnderCursor(LinkedTeleporter, LinkedTeleporterEndpointIndex, TeleporterHit))
		{
			MarkSameKeyInteractionHandled();
			UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Handled linked teleporter Target=%s Endpoint=%d."),
				*GetNameSafe(LinkedTeleporter),
				static_cast<int32>(LinkedTeleporterEndpointIndex));
			return;
		}
	}

	FHitResult InteractableHit;
	AActor* InteractableActor = nullptr;
	if (TryGetInteractableHit(InteractableHit, InteractableActor))
	{
		ClearAttackInputIntent();
		if (HandleInteractableUnderCursor(InteractableActor, InteractableHit))
		{
			MarkSameKeyInteractionHandled();
			UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Handled interactable Target=%s."),
				*GetNameSafe(InteractableActor));
			return;
		}
	}

	UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Input] Press found no teleporter or interactable target; no movement fallback will run."));
}

bool AAeyerjiPlayerController::ActivatePrimaryAttackAbility(AActor* ExplicitTarget)
{
	if (IsAbilityCastInputLocked())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Activate blocked: ability cast lock active. ExplicitTarget=%s"),
			*GetNameSafe(ExplicitTarget));
		return false;
	}

	UAbilitySystemComponent* ASC = GetControlledAbilitySystem();
	if (!ASC)
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Activate blocked: no controlled ASC. ExplicitTarget=%s Pawn=%s"),
			*GetNameSafe(ExplicitTarget),
			*GetNameSafe(GetPawn()));
		return false;
	}

	if (!ExplicitTarget)
	{
		FGameplayTagContainer TagSearch;
		if (!BuildPrimaryAttackTagSearch(ASC, TagSearch))
		{
			return false;
		}

		return ASC->TryActivateAbilitiesByTag(TagSearch, /*bAllowRemoteActivation=*/true);
	}

	if (!IsAttackableActor(ExplicitTarget))
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Activate blocked: explicit target not attackable. ExplicitTarget=%s"),
			*GetNameSafe(ExplicitTarget));
		return false;
	}

	const bool bLocalTriggered = TriggerPrimaryAttackAbility(ASC, ExplicitTarget);
	UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Activate explicit target requested. Target=%s LocalTriggered=%s Authority=%s ASC=%s"),
		*GetNameSafe(ExplicitTarget),
		BoolText(bLocalTriggered),
		BoolText(HasAuthority()),
		*GetNameSafe(ASC));
	if (!HasAuthority())
	{
		Server_ActivatePrimaryAttackOnActor(ExplicitTarget);
		return true;
	}

	return bLocalTriggered;
}

FGameplayAbilitySpecHandle AAeyerjiPlayerController::FindPrimaryAttackAbilityHandle(UAbilitySystemComponent* ASC) const
{
	if (!ASC)
	{
		return FGameplayAbilitySpecHandle();
	}

	FGameplayTagContainer TagSearch;
	if (!BuildPrimaryAttackTagSearch(ASC, TagSearch))
	{
		return FGameplayAbilitySpecHandle();
	}

	FScopedAbilityListLock AbilityListLock(*ASC);
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasAll(TagSearch))
		{
			return Spec.Handle;
		}
	}

	return FGameplayAbilitySpecHandle();
}

bool AAeyerjiPlayerController::TriggerPrimaryAttackAbility(UAbilitySystemComponent* ASC, AActor* ExplicitTarget)
{
	if (!ASC || !ExplicitTarget || !ASC->AbilityActorInfo.IsValid())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] TriggerAbilityFromGameplayEvent blocked: ASC=%s Target=%s AbilityActorInfoValid=%s"),
			*GetNameSafe(ASC),
			*GetNameSafe(ExplicitTarget),
			BoolText(ASC && ASC->AbilityActorInfo.IsValid()));
		return false;
	}

	const FGameplayAbilitySpecHandle AbilityHandle = FindPrimaryAttackAbilityHandle(ASC);
	if (!AbilityHandle.IsValid())
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] TriggerAbilityFromGameplayEvent blocked: primary attack handle invalid. ASC=%s Target=%s"),
			*GetNameSafe(ASC),
			*GetNameSafe(ExplicitTarget));
		return false;
	}

	APawn* ControlledPawn = GetPawn();
	FGameplayEventData EventData;
	EventData.EventTag = AeyerjiTags::Event_External_Target;
	EventData.Instigator = ControlledPawn;
	EventData.Target = ExplicitTarget;
	EventData.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(ExplicitTarget);

	const bool bTriggered = ASC->TriggerAbilityFromGameplayEvent(AbilityHandle, ASC->AbilityActorInfo.Get(), EventData.EventTag, &EventData, *ASC);
	UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] TriggerAbilityFromGameplayEvent result=%s Handle=%s Target=%s EventTag=%s"),
		BoolText(bTriggered),
		*AbilityHandle.ToString(),
		*GetNameSafe(ExplicitTarget),
		*EventData.EventTag.ToString());
	return bTriggered;
}

void AAeyerjiPlayerController::Server_ActivatePrimaryAttackOnActor_Implementation(AActor* TargetActor)
{
	if (!IsAttackableActor(TargetActor))
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Server activate ignored: target not attackable. Target=%s"),
			*GetNameSafe(TargetActor));
		return;
	}

	UAbilitySystemComponent* ASC = GetControlledAbilitySystem();
	if (!ASC)
	{
		UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Server activate ignored: no ASC. Target=%s Pawn=%s"),
			*GetNameSafe(TargetActor),
			*GetNameSafe(GetPawn()));
		return;
	}

	const bool bTriggered = TriggerPrimaryAttackAbility(ASC, TargetActor);
	UE_LOG(LogAeyerji, Log, TEXT("[MouseAttack] Server activate explicit target result=%s Target=%s ASC=%s"),
		BoolText(bTriggered),
		*GetNameSafe(TargetActor),
		*GetNameSafe(ASC));
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
	BeginMouseCommand(EAeyerjiMouseButton::Right);
}

void AAeyerjiPlayerController::OnMoveClickHeld(const FInputActionValue& /*Val*/)
{
	// Held mouse commands are updated from Tick so Enhanced Input trigger cadence cannot own movement.
}

void AAeyerjiPlayerController::OnMoveClickReleased(const FInputActionValue& /*Val*/)
{
	const bool bPhysicalButtonStillDown = IsInputKeyDown(MoveClickPhysicalKey);
	if (bPhysicalButtonStillDown && bMoveClickHeld)
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MouseCommand] Ignoring premature right release/completed while physical key is still down. Key=%s"),
			*MoveClickPhysicalKey.ToString());
		return;
	}

	ReleaseMouseCommand(EAeyerjiMouseButton::Right);
}

void AAeyerjiPlayerController::OnDropItemPressed(const FInputActionValue& /*Val*/)
{
	if (IsGameplayInputSuppressedByModalUI())
	{
		return;
	}

	if (IsAbilityCastInputLocked())
	{
		return;
	}

	TryDropItemUnderCursor();
}

void AAeyerjiPlayerController::OnCancelActionPressed(const FInputActionValue& /*Val*/)
{
	if (IsGameplayInputSuppressedByModalUI())
	{
		return;
	}

	LocalAbilityCastInputLockEndTime = -1.0;
	Server_CancelActiveAbilityCast();
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

	if (IsAbilityCastInputLocked())
	{
		return;
	}

	if (CachedTarget.IsValid() && !IsAttackableActor(CachedTarget.Get()))
	{
		CachedTarget = nullptr;
		PendingMoveTarget = nullptr;
	}

	const bool bHasTarget = CachedTarget.IsValid();
	const bool bHasAttackTarget = bHasTarget && IsAttackableActor(CachedTarget.Get());

	// Where are we trying to go? If there's a target, use its location; else the ground point.
	const FVector TargetLocation = bHasTarget
		? CachedTarget->GetActorLocation()
		: CachedGoal;

	if (HandleMovementBlockedByAbilities())
	{
		if (bHasTarget)
		{
			QueueMovementCommand(CachedTarget.Get(), bIsContinuous);
		}
		else
		{
			QueueMovementCommand(TargetLocation, bSpawnCursorFX, bIsContinuous);
		}
		return;
	}

	// Distance-squared (cheap) from pawn to the chosen target location.
	const float DistSqToTarget = FVector::DistSquared(MyPawn->GetActorLocation(), TargetLocation);

	// Thresholds precomputed and named for readability.
	const float MinMoveDistSq = FMath::Square(MinMoveDistanceCm);
	const bool  bFarEnough    = (DistSqToTarget >= MinMoveDistSq);

	float AttackMoveStopRange = 0.f;
	bool bWithinPrimaryAttackStopRange = false;
	if (bHasAttackTarget)
	{
		const float StopPercent = FMath::Max(0.f, PrimaryAttackMoveStopAtPercentOfRange);
		const float AttackRange = UCharacterStatsLibrary::GetAttackRangeFromActorASC(MyPawn, /*FallbackRange=*/150.f);
		AttackMoveStopRange = FMath::Max(0.f, AttackRange * StopPercent + FMath::Max(0.f, PrimaryAttackMoveStopExtraBufferCm));
		if (AttackMoveStopRange > 0.f)
		{
			bWithinPrimaryAttackStopRange =
				FVector::DistSquared2D(MyPawn->GetActorLocation(), TargetLocation) <= FMath::Square(AttackMoveStopRange);
		}
	}

	// ---------- Early out if too close ----------
	if (!bFarEnough)
	{
		// We are already close enough; do not spam move/attack.
		if (!bIsContinuous)
		{
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] HandleMoveCommand ignored one-shot: goal too close. Target=%s HasAttackTarget=%s Dist=%.1f MinDist=%.1f"),
				*GetNameSafe(CachedTarget.Get()),
				BoolText(bHasAttackTarget),
				FMath::Sqrt(DistSqToTarget),
				MinMoveDistanceCm);
		}
		return;
	}

	if (bWithinPrimaryAttackStopRange)
	{
		// Once the target is inside attack stop range, cancel any existing chase so hold-attack
		// can continue in place instead of creeping forward every frame.
		const bool bHadPendingTargetMove = PendingMoveTarget.Get() == CachedTarget.Get();
		PendingMoveTarget = nullptr;
		ResetCursorFollowTurnRate();
		bCursorFollowActive = false;

		UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
		const bool bHasActiveMove =
			(PFC && PFC->GetStatus() != EPathFollowingStatus::Idle)
			|| bHadPendingTargetMove;
		if (bHasActiveMove)
		{
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Attack stop range aborting chase. Target=%s Dist2D=%.1f StopRange=%.1f HadPendingTargetMove=%s %s"),
				*GetNameSafe(CachedTarget.Get()),
				FVector::Dist2D(MyPawn->GetActorLocation(), TargetLocation),
				AttackMoveStopRange,
				BoolText(bHadPendingTargetMove),
				*DescribePathFollowing(PFC));
			AbortMovement_Local();
			if (!HasAuthority())
			{
				Server_AbortMovement();
			}
		}
		return;
	}

	// ---------- Command dispatch ----------
	// Prefer actor-targeted move/attack if we have a valid attackable target.

	if (bIsContinuous)
	{
		if (bHasAttackTarget)
		{
			ResetCursorFollowTurnRate();
			bCursorFollowActive = false;
			if (PendingMoveTarget.Get() != CachedTarget.Get())
			{
				PendingMoveTarget = CachedTarget;
				AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] Continuous target move issue. Target=%s Dist=%.1f %s %s"),
					*GetNameSafe(CachedTarget.Get()),
					FMath::Sqrt(DistSqToTarget),
					*DescribePawnMovement(MyPawn),
					*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
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
		if (bHasAttackTarget)
		{
			// Overload: AActor*
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] One-shot actor move issue. Target=%s Dist=%.1f %s %s"),
				*GetNameSafe(CachedTarget.Get()),
				FMath::Sqrt(DistSqToTarget),
				*DescribePawnMovement(MyPawn),
				*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
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
			AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] One-shot location move issue. Goal=%s Dist=%.1f %s %s"),
				*TargetLocation.ToCompactString(),
				FMath::Sqrt(DistSqToTarget),
				*DescribePawnMovement(MyPawn),
				*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
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

bool AAeyerjiPlayerController::ResolveSafeMoveGoal(const FVector& DesiredGoal, FVector& OutGoal) const
{
	OutGoal = DesiredGoal;

	FAeyerjiNavSafetyResolveParams Params;
	Params.ProjectionExtent = NavProjectExtents;
	Params.SearchRadius = 600.f;
	Params.SearchStep = 150.f;
	Params.GroundTraceHeight = 300.f;
	Params.GroundTraceDepth = 500.f;

	FAeyerjiNavSafetyResult Result;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, DesiredGoal, GetPawn(), Params, Result))
	{
		AJ_LOG(this, TEXT("[Move] ResolveSafeMoveGoal failed. Goal=%s Reason=%s"),
			*DesiredGoal.ToCompactString(),
			*Result.FailureReason.ToString());
		return false;
	}

	OutGoal = Result.NavLocation;
	return true;
}

bool AAeyerjiPlayerController::EnsureControlledPawnOnSafeNav(const bool bImmediateRecover) const
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return false;
	}

	if (!HasAuthority())
	{
		return true;
	}

	if (UAeyerjiNavSafetyComponent* NavSafety = ControlledPawn->FindComponentByClass<UAeyerjiNavSafetyComponent>())
	{
		return NavSafety->EnsureOwnerOnSafeNav(bImmediateRecover);
	}

	FAeyerjiNavSafetyResolveParams Params;
	Params.ProjectionExtent = NavProjectExtents;
	FVector SafeLocation = ControlledPawn->GetActorLocation();
	return UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(
		ControlledPawn,
		Params,
		bImmediateRecover,
		SafeLocation);
}

bool AAeyerjiPlayerController::IsAttackableActor(const AActor* Other) const
{
	if (!Other || Other == GetPawn() || Other->IsActorBeingDestroyed())
	{
		return false;
	}

	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Other, /*LookForComponent=*/true))
	{
		if (TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		{
			return false;
		}
	}

	if (Other->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
	{
		return false;
	}

	const IGenericTeamAgentInterface* Me = Cast<IGenericTeamAgentInterface>(GetPawn());
	const IGenericTeamAgentInterface* Rival = Cast<IGenericTeamAgentInterface>(Other);
	if (Me && Rival)
	{
		return Me->GetGenericTeamId() != Rival->GetGenericTeamId();
	}

	return Other->IsA<APawn>();
}

void AAeyerjiPlayerController::ResetForClick()
{
	ClearMouseCommandData();
	CancelFaceActor();
	StopPendingTeleporter();
	StopPendingInteraction();
	bMoveClickHeld = false;
	EnsureLocomotionRotationMode();
	PendingMoveGoal = FVector::ZeroVector;
	PendingMoveTarget = nullptr;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	LastCursorFollowClientDiagTime = -1.0;
	LastCursorFollowServerDiagTime = -1.0;
	ResetCursorFollowHold();
}

void AAeyerjiPlayerController::ResetForMoveOnly()
{
	ClearMouseCommandData();
	CancelFaceActor();
	StopPendingTeleporter();
	StopPendingInteraction();
	ClearAttackInputIntent();
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
	LastCursorFollowClientDiagTime = -1.0;
	LastCursorFollowServerDiagTime = -1.0;
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
	const bool bHadActiveMelee = HasActivePrimaryMeleePhaseTag(ASC);
	if (!bHadMovementLock && !bHadActiveMelee)
	{
		return false;
	}

	UpdatePrimaryMeleeRotationLock();
	return true;
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

bool AAeyerjiPlayerController::ResolveDirectAttackTargetUnderCursor(FHitResult& OutHit) const
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

		if (IsAttackableActor(OutHit.GetActor()))
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiPlayerController::ResolveAttackTargetUnderCursor(FHitResult& OutHit) const
{
	if (ResolveDirectAttackTargetUnderCursor(OutHit))
	{
		return true;
	}

	const float HoverFallbackRadiusPx = TargetSnapScreenRadiusPx;
	if (HoveredEnemy.IsValid()
		&& IsAttackableActor(HoveredEnemy.Get())
		&& IsCursorNearActorScreenLocation(this, HoveredEnemy.Get(), HoverFallbackRadiusPx))
	{
		return BuildSyntheticCursorHit(HoveredEnemy.Get(), HoveredEnemy->GetActorLocation(), OutHit);
	}

	if (!bEnableTargetSnap || TargetSnapScreenRadiusPx <= 0.f)
	{
		return false;
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

	if (!IsAttackableActor(BestEnemy))
	{
		return false;
	}

	return BuildSyntheticCursorHit(BestEnemy, BestEnemy->GetActorLocation(), OutHit);
}

bool AAeyerjiPlayerController::TryGetPawnHit(FHitResult& OutHit) const
{
	return ResolveAttackTargetUnderCursor(OutHit);
}

bool AAeyerjiPlayerController::TryGetLinkedTeleporterHit(FHitResult& OutHit, AAeyerjiLinkedTeleporter*& OutTeleporter, uint8& OutEndpointIndex) const
{
	OutTeleporter = nullptr;
	OutEndpointIndex = 0;

	if (!TraceCursor(ECC_GameTraceChannel1, OutHit, /*bTraceComplex=*/false))
	{
		return false;
	}

	AAeyerjiLinkedTeleporter* Teleporter = Cast<AAeyerjiLinkedTeleporter>(OutHit.GetActor());
	if (!Teleporter)
	{
		return false;
	}

	if (!Teleporter->ResolveEndpointFromComponent(OutHit.GetComponent(), OutEndpointIndex))
	{
		return false;
	}

	OutTeleporter = Teleporter;
	return true;
}

bool AAeyerjiPlayerController::TryGetInteractableHit(FHitResult& OutHit, AActor*& OutInteractable) const
{
	OutInteractable = nullptr;

	if (!TraceCursor(ECC_GameTraceChannel1, OutHit, /*bTraceComplex=*/false))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Trace] Cursor trace missed interact channel."));
		return false;
	}

	const APawn* MyPawn = GetPawn();
	if (MyPawn && OutHit.GetActor() == MyPawn)
	{
		FVector WorldOrigin;
		FVector WorldDir;
		if (DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
		{
			if (UWorld* World = GetWorld())
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(CursorInteractableSkipSelf), /*bTraceComplex=*/false);
				Params.AddIgnoredActor(MyPawn);

				FHitResult AltHit;
				const FVector TraceStart = WorldOrigin;
				const FVector TraceEnd = TraceStart + WorldDir * 100000.f;
				if (World->LineTraceSingleByChannel(AltHit, TraceStart, TraceEnd, ECC_GameTraceChannel1, Params))
				{
					OutHit = AltHit;
					UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Trace] Initial hit was controlled pawn; alternate trace hit Actor=%s Component=%s"),
						*GetNameSafe(OutHit.GetActor()),
						*GetNameSafe(OutHit.GetComponent()));
				}
				else
				{
					UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Trace] Initial hit was controlled pawn; alternate trace found no interactable."));
					return false;
				}
			}
		}
	}

	AActor* HitActor = OutHit.GetActor();
	if (!IsValid(HitActor) || !HitActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		UE_LOG(LogAeyerji, VeryVerbose, TEXT("[Interaction][Trace] Hit actor is not interactable Actor=%s Component=%s Impact=%s"),
			*GetNameSafe(HitActor),
			*GetNameSafe(OutHit.GetComponent()),
			*OutHit.ImpactPoint.ToCompactString());
		return false;
	}

	OutInteractable = HitActor;
	UE_LOG(LogAeyerji, Log, TEXT("[Interaction][Trace] Hit interactable Actor=%s Component=%s Impact=%s; deferring CanInteract to server"),
		*GetNameSafe(OutInteractable),
		*GetNameSafe(OutHit.GetComponent()),
		*OutHit.ImpactPoint.ToCompactString());
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

	const FName ManagerName = MakeUniqueObjectName(this, UAeyerjiTargetingManager::StaticClass(), TEXT("AeyerjiTargetingManager"));
	TargetingManager = NewObject<UAeyerjiTargetingManager>(this, UAeyerjiTargetingManager::StaticClass(), ManagerName);
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

void AAeyerjiPlayerController::ClearTargeting()
{
	if (TargetingManager)
	{
		TargetingManager->ClearTargeting();
	}
}

void AAeyerjiPlayerController::ClearAttackInputIntent()
{
	bAttackClickHeld = false;
	CachedTarget = nullptr;
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

bool AAeyerjiPlayerController::HandleLinkedTeleporterUnderCursor(AAeyerjiLinkedTeleporter* Teleporter, const uint8 EndpointIndex, const FHitResult& TeleporterHit)
{
	static_cast<void>(TeleporterHit);

	if (!Teleporter)
	{
		return false;
	}

	if (!Teleporter->IsEndpointEnabledForUse(EndpointIndex))
	{
		AJ_LOG(this, TEXT("[PC] Linked teleporter %s endpoint %d is disabled"),
			*GetNameSafe(Teleporter),
			static_cast<int32>(EndpointIndex));
		return true;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return true;
	}

	if (Teleporter->IsControllerOnCooldown(this))
	{
		AJ_LOG(this, TEXT("[PC] Linked teleporter %s endpoint %d is on cooldown"),
			*GetNameSafe(Teleporter),
			static_cast<int32>(EndpointIndex));
		return true;
	}

	if (Teleporter->IsPawnInInteractionRange(ControlledPawn, EndpointIndex))
	{
		AJ_LOG(this, TEXT("[PC] Requesting linked teleporter use %s endpoint %d"),
			*GetNameSafe(Teleporter),
			static_cast<int32>(EndpointIndex));
		AbortMovement_Both();
		Server_RequestLinkedTeleporterUse(Teleporter, EndpointIndex);
		return true;
	}

	FVector Goal;
	if (ComputeTeleporterGoal(Teleporter, EndpointIndex, Goal))
	{
		IssueMoveRPC(Goal);
		StartPendingTeleporter(Teleporter, EndpointIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PC] Cannot find navigable teleporter goal for %s endpoint %d"),
			*GetNameSafe(Teleporter),
			static_cast<int32>(EndpointIndex));
	}

	return true;
}

bool AAeyerjiPlayerController::HandleInteractableUnderCursor(AActor* InteractableActor, const FHitResult& InteractableHit)
{
	static_cast<void>(InteractableHit);

	if (!IsValid(InteractableActor) || !InteractableActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		AJ_LOG(this, TEXT("[Interaction] Handle rejected: invalid target Target=%s HitActor=%s Component=%s"),
			*GetNameSafe(InteractableActor),
			*GetNameSafe(InteractableHit.GetActor()),
			*GetNameSafe(InteractableHit.GetComponent()));
		return false;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		AJ_LOG(this, TEXT("[Interaction] Handle consumed but controller has no pawn Target=%s"),
			*GetNameSafe(InteractableActor));
		return true;
	}

	const FVector InteractionLocation = IAeyerjiInteractable::Execute_GetInteractionLocation(InteractableActor);
	const float InteractionRadius = IAeyerjiInteractable::Execute_GetInteractionRadius(InteractableActor);
	const float Distance2D = FVector::Dist2D(ControlledPawn->GetActorLocation(), InteractionLocation);
	if (InteractionRadius <= 0.f || Distance2D <= InteractionRadius)
	{
		AJ_LOG(this, TEXT("[Interaction] Target in range; requesting server interaction Target=%s Pawn=%s Distance=%.1f Radius=%.1f Unlimited=%d"),
			*GetNameSafe(InteractableActor),
			*GetNameSafe(ControlledPawn),
			Distance2D,
			InteractionRadius,
			InteractionRadius <= 0.f ? 1 : 0);
		AbortMovement_Both();
		Server_RequestInteractableUse(InteractableActor);
		return true;
	}

	FVector Goal;
	if (ComputeInteractionGoal(InteractableActor, Goal))
	{
		AJ_LOG(this, TEXT("[Interaction] Target out of range; moving toward interaction goal Target=%s Pawn=%s Distance=%.1f Radius=%.1f Goal=%s"),
			*GetNameSafe(InteractableActor),
			*GetNameSafe(ControlledPawn),
			Distance2D,
			InteractionRadius,
			*Goal.ToCompactString());
		IssueMoveRPC(Goal);
		StartPendingInteraction(InteractableActor);
	}
	else
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Interaction] Cannot find navigable interaction goal Target=%s Pawn=%s Distance=%.1f Radius=%.1f"),
			*GetNameSafe(InteractableActor),
			*GetNameSafe(ControlledPawn),
			Distance2D,
			InteractionRadius);
	}

	return true;
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

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);
		return;
	}

	EnsureLocomotionRotationMode();
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World ? World->GetDeltaSeconds() : (1.f / 60.f);
	FVector SmoothedGoal = Goal;
	if (bCursorFollowHasSmoothedGoal && CursorFollowGoalInterpSpeed > 0.f)
	{
		const float SnapDistance = FMath::Max(0.f, CursorFollowGoalSnapDistance);
		const bool bShouldSnap = SnapDistance > 0.f
			&& FVector::DistSquared2D(Goal, CursorFollowSmoothedGoal) > FMath::Square(SnapDistance);
		if (!bShouldSnap)
		{
			SmoothedGoal = FMath::VInterpTo(CursorFollowSmoothedGoal, Goal, DeltaSeconds, CursorFollowGoalInterpSpeed);
		}
	}

	FVector SafeSmoothedGoal = SmoothedGoal;
	if (!ResolveSafeMoveGoal(SmoothedGoal, SafeSmoothedGoal))
	{
		return;
	}
	SmoothedGoal = SafeSmoothedGoal;

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
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bGoalMoved = LastCursorFollowRepathTime < 0.0
		|| FVector::DistSquared2D(SmoothedGoal, LastCursorFollowRepathGoal) >= FMath::Square(CursorFollowRepathDistance);
	const bool bCanRepath = LastCursorFollowRepathTime < 0.0
		|| CursorFollowRepathInterval <= 0.f
		|| (Now - LastCursorFollowRepathTime) >= CursorFollowRepathInterval;
	const bool bShouldReissueMove = (bShouldStartMove || bGoalMoved) && bCanRepath;
	const float RepathGoalDelta = LastCursorFollowRepathTime < 0.0
		? -1.f
		: FVector::Dist2D(SmoothedGoal, LastCursorFollowRepathGoal);
	const bool bShouldLogSample = LastCursorFollowClientDiagTime < 0.0
		|| (Now - LastCursorFollowClientDiagTime) >= 0.25
		|| bShouldReissueMove;
	if (bShouldLogSample)
	{
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] CursorFollow client goal update. RawGoal=%s Goal=%s Delta=%.1f Active=%s StartMove=%s GoalMoved=%s CanRepath=%s Reissue=%s RepathInterval=%.3f RepathDistance=%.1f InterpSpeed=%.1f %s %s"),
			*Goal.ToCompactString(),
			*SmoothedGoal.ToCompactString(),
			RepathGoalDelta,
			BoolText(bCursorFollowActive),
			BoolText(bShouldStartMove),
			BoolText(bGoalMoved),
			BoolText(bCanRepath),
			BoolText(bShouldReissueMove),
			CursorFollowRepathInterval,
			CursorFollowRepathDistance,
			CursorFollowGoalInterpSpeed,
			*DescribePawnMovement(GetPawn()),
			*DescribePathFollowing(PFC));
		LastCursorFollowClientDiagTime = Now;
	}
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
				Path->SetGoalActorObservation(*FollowActor, FMath::Max(1.f, CursorFollowPathObservationDistance));
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
	UpdatePrimaryMeleeRotationLock();
	if (bPrimaryMeleeRotationLockActive)
	{
		return;
	}

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

	// Keep cursor-follow proxy visuals disabled during gameplay.
	if (CursorFollowDebugMesh.IsValid())
	{
		CursorFollowDebugMesh->SetHiddenInGame(true);
		CursorFollowDebugMesh->SetVisibility(false, true);
	}

	FollowActor->SetActorHiddenInGame(true);
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

	UE_LOG(LogAeyerji, VeryVerbose, TEXT("[MoveHold] Cursor-follow hold primed. StartGoal=%s Delay=%.3f Distance=%.1f"),
		*CursorFollowHoldStartGoal.ToCompactString(),
		CursorFollowHoldStartDelay,
		CursorFollowHoldStartDistance);
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
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] CursorFollow hold activated. Goal=%s StartGoal=%s DelayMet=%s DistanceMet=%s Delay=%.3f Distance=%.1f"),
			*Goal.ToCompactString(),
			*CursorFollowHoldStartGoal.ToCompactString(),
			BoolText(bDelayMet),
			BoolText(bDistanceMet),
			CursorFollowHoldStartDelay,
			FVector::Dist2D(Goal, CursorFollowHoldStartGoal));
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
	}

	FHitResult EnemyHit;
	if (ResolveAttackTargetUnderCursor(EnemyHit))
	{
		if (AEnemyParentNative* EnemyCandidate = Cast<AEnemyParentNative>(EnemyHit.GetActor()))
		{
			EnemyComponent = EnemyHit.GetComponent();
			NewEnemy = EnemyCandidate;
		}
	}

	if (!NewLoot && HoveredLoot.IsValid() && bHasInteractHit && InteractHit.GetActor() == HoveredLoot.Get())
	{
		NewLoot = HoveredLoot.Get();
		LootComponent = InteractHit.GetComponent();
	}

	if (!NewLoot)
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

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/false);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		return;
	}
     
	FVector SafeGoal = Goal;
	if (!ResolveSafeMoveGoal(Goal, SafeGoal))
	{
		return;
	}

    EnsureLocomotionRotationMode();
    
	// Rate limit RPC calls to prevent flooding the network
	const double Now = FPlatformTime::Seconds();
	// Always run client-side prediction immediately for responsiveness
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, SafeGoal);
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] IssueMoveRPC location local SimpleMove. Goal=%s Dist2D=%.1f HasAuthority=%s ServerCmdAge=%.3f %s %s"),
		*SafeGoal.ToCompactString(),
		GetPawn() ? FVector::Dist2D(GetPawn()->GetActorLocation(), SafeGoal) : -1.f,
		BoolText(HasAuthority()),
		LastServerCmdTs > 0.f ? Now - LastServerCmdTs : -1.0,
		*DescribePawnMovement(GetPawn()),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));

    // Then send to server for authority
    if (!HasAuthority())
    {
        ServerMoveToLocation(SafeGoal);
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

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Target, /*bIsContinuous=*/false);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		return;
	}
	
	EnsureLocomotionRotationMode();
	
	// Rate limit RPC calls
	const double Now = FPlatformTime::Seconds();

	// Always do local prediction for responsiveness
	UAIBlueprintHelperLibrary::SimpleMoveToActor(this, Target);
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] IssueMoveRPC actor local SimpleMove. Target=%s TargetLoc=%s Dist2D=%.1f HasAuthority=%s ServerCmdAge=%.3f %s %s"),
		*GetNameSafe(Target),
		*Target->GetActorLocation().ToCompactString(),
		GetPawn() ? FVector::Dist2D(GetPawn()->GetActorLocation(), Target->GetActorLocation()) : -1.f,
		BoolText(HasAuthority()),
		LastServerCmdTs > 0.f ? Now - LastServerCmdTs : -1.0,
		*DescribePawnMovement(GetPawn()),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));

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

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/false);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation rejected: pawn could not recover to nav."));
		return;
	}

	FVector SafeGoal = Goal;
	if (!ResolveSafeMoveGoal(Goal, SafeGoal))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation rejected: goal could not resolve to nav. Goal=%s"),
			*Goal.ToCompactString());
		return;
	}

	// Only accept move commands that are a meaningful distance away
	if (FVector::DistSquared(SafeGoal, ControlledPawn->GetActorLocation()) < FMath::Square(20.f))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToLocation ignored: goal too close."));
		return;
	}

	EnsureLocomotionRotationMode();

	// Use the AI subsystem to handle pathfinding and movement
    // Execute server-side movement with immediate force
    UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, SafeGoal);
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] ServerMoveToLocation accepted. Goal=%s Dist2D=%.1f %s %s"),
		*SafeGoal.ToCompactString(),
		FVector::Dist2D(ControlledPawn->GetActorLocation(), SafeGoal),
		*DescribePawnMovement(ControlledPawn),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));

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

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Target, /*bIsContinuous=*/false);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor rejected: pawn could not recover to nav."));
		return;
	}

	// Only accept move commands that are a meaningful distance away
	if (FVector::DistSquared(Target->GetActorLocation(), ControlledPawn->GetActorLocation()) < FMath::Square(20.f))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("[Move] ServerMoveToActor ignored: target too close."));
		return;
	}

	EnsureLocomotionRotationMode();

	// Use the AI subsystem to handle pathfinding and movement
	UAIBlueprintHelperLibrary::SimpleMoveToActor(this, Target);
	AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] ServerMoveToActor accepted. Target=%s TargetLoc=%s Dist2D=%.1f Acceptance=%.1f %s %s"),
		*GetNameSafe(Target),
		*Target->GetActorLocation().ToCompactString(),
		FVector::Dist2D(ControlledPawn->GetActorLocation(), Target->GetActorLocation()),
		AcceptanceRadius,
		*DescribePawnMovement(ControlledPawn),
		*DescribePathFollowing(FindComponentByClass<UPathFollowingComponent>()));
}

void AAeyerjiPlayerController::Server_UpdateCursorFollowGoal_Implementation(const FVector& Goal)
{
	if (!GetPawn() || IsControlledPawnDead())
	{
		return;
	}

	if (HandleMovementBlockedByAbilities())
	{
		QueueMovementCommand(Goal, /*bSpawnCursorFX=*/false, /*bIsContinuous=*/true);
		return;
	}

	if (!EnsureControlledPawnOnSafeNav(/*bImmediateRecover=*/true))
	{
		return;
	}

	EnsureLocomotionRotationMode();
	const UWorld* World = GetWorld();
	FVector SmoothedGoal = Goal;
	if (!ResolveSafeMoveGoal(Goal, SmoothedGoal))
	{
		return;
	}

	CursorFollowSmoothedGoal = SmoothedGoal;
	bCursorFollowHasSmoothedGoal = true;

	AActor* FollowActor = GetOrCreateCursorFollowActor();
	if (!FollowActor)
	{
		return;
	}

	FollowActor->SetActorLocation(SmoothedGoal);

	UPathFollowingComponent* PFC = FindComponentByClass<UPathFollowingComponent>();
	const bool bShouldStartMove = !bCursorFollowActive || (PFC && PFC->GetStatus() == EPathFollowingStatus::Idle);
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bGoalMoved = LastCursorFollowRepathTime < 0.0
		|| FVector::DistSquared2D(SmoothedGoal, LastCursorFollowRepathGoal) >= FMath::Square(CursorFollowRepathDistance);
	const bool bCanRepath = LastCursorFollowRepathTime < 0.0
		|| CursorFollowRepathInterval <= 0.f
		|| (Now - LastCursorFollowRepathTime) >= CursorFollowRepathInterval;
	const bool bShouldReissueMove = (bShouldStartMove || bGoalMoved) && bCanRepath;
	const float RepathGoalDelta = LastCursorFollowRepathTime < 0.0
		? -1.f
		: FVector::Dist2D(SmoothedGoal, LastCursorFollowRepathGoal);
	const bool bShouldLogSample = LastCursorFollowServerDiagTime < 0.0
		|| (Now - LastCursorFollowServerDiagTime) >= 0.25
		|| bShouldReissueMove;
	if (bShouldLogSample)
	{
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] CursorFollow server goal update. ClientGoal=%s Goal=%s Delta=%.1f Active=%s StartMove=%s GoalMoved=%s CanRepath=%s Reissue=%s RepathInterval=%.3f RepathDistance=%.1f InterpSpeed=%.1f ServerMirror=true %s %s"),
			*Goal.ToCompactString(),
			*SmoothedGoal.ToCompactString(),
			RepathGoalDelta,
			BoolText(bCursorFollowActive),
			BoolText(bShouldStartMove),
			BoolText(bGoalMoved),
			BoolText(bCanRepath),
			BoolText(bShouldReissueMove),
			CursorFollowRepathInterval,
			CursorFollowRepathDistance,
			CursorFollowGoalInterpSpeed,
			*DescribePawnMovement(GetPawn()),
			*DescribePathFollowing(PFC));
		LastCursorFollowServerDiagTime = Now;
	}
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
				Path->SetGoalActorObservation(*FollowActor, FMath::Max(1.f, CursorFollowPathObservationDistance));
			}
		}
	}
}

void AAeyerjiPlayerController::Server_ResetCursorFollowTurnRate_Implementation()
{
	ResetCursorFollowTurnRate();
	bCursorFollowActive = false;
	bCursorFollowHasSmoothedGoal = false;
	CursorFollowSmoothedGoal = FVector::ZeroVector;
	LastCursorFollowRepathTime = -1.0;
	LastCursorFollowRepathGoal = FVector::ZeroVector;
	LastCursorFollowServerDiagTime = -1.0;
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
	AbortMovement_Local();

	if (AbilityUsesExternalTargetEvent(AbilitySlot.Class))
	{
		FGameplayAbilityTargetingLocationInfo SrcLoc; SrcLoc.LocationType = EGameplayAbilityTargetingLocationType::ActorTransform; SrcLoc.SourceActor = P;
		FGameplayAbilityTargetingLocationInfo DstLoc; DstLoc.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform; DstLoc.LiteralTransform.SetLocation(Target);
		FGameplayAbilityTargetDataHandle TDH = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromLocations(SrcLoc, DstLoc);

		FGameplayEventData Ev; Ev.EventTag = AeyerjiTags::Event_External_Target; Ev.Instigator = P; Ev.InstigatorTags = AbilitySlot.Tag; Ev.TargetData = TDH;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
		AJ_LOG(this, TEXT("Server_ActivateAbilityAtLocation: sent Event.External.Target (Tag=%s Target=%s Class=%s)"),
			*AbilitySlot.Tag.ToString(),
			*Target.ToString(),
			*GetNameSafe(AbilitySlot.Class));
		return;
	}

	TryActivateAbilitySlotDirectly(ASC, AbilitySlot, this, TEXT("Server_ActivateAbilityAtLocation"));
}

void AAeyerjiPlayerController::Server_ActivateAbilityOnActor_Implementation(const FAeyerjiAbilitySlot& AbilitySlot, AActor* TargetActor)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: no ASC")); return; }
	AbortMovement_Local();

	if (!TargetActor)
	{
		return;
	}

	if (AbilityUsesExternalTargetEvent(AbilitySlot.Class))
	{
		FGameplayAbilityTargetDataHandle TDH = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(TargetActor);

		FGameplayEventData Ev; Ev.EventTag = AeyerjiTags::Event_External_Target; Ev.Instigator = P; Ev.InstigatorTags = AbilitySlot.Tag; Ev.TargetData = TDH;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
		AJ_LOG(this, TEXT("Server_ActivateAbilityOnActor: sent Event.External.Target (Tag=%s Target=%s Class=%s)"),
			*AbilitySlot.Tag.ToString(),
			*GetNameSafe(TargetActor),
			*GetNameSafe(AbilitySlot.Class));
		return;
	}

	TryActivateAbilitySlotDirectly(ASC, AbilitySlot, this, TEXT("Server_ActivateAbilityOnActor"));
}

void AAeyerjiPlayerController::Server_ActivateAbilityInstant_Implementation(const FAeyerjiAbilitySlot& AbilitySlot)
{
	APawn* P = GetPawn();
	if (!P) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: no pawn")); return; }
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P); if (!ASI) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: pawn lacks ASI")); return; }
	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) { AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: no ASC")); return; }
	AbortMovement_Local();

	if (AbilityUsesExternalTargetEvent(AbilitySlot.Class))
	{
		FGameplayEventData Ev;
		Ev.EventTag = AeyerjiTags::Event_External_Target;
		Ev.Instigator = P;
		Ev.InstigatorTags = AbilitySlot.Tag;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(P, Ev.EventTag, Ev);
		AJ_LOG(this, TEXT("Server_ActivateAbilityInstant: sent Event.External.Target (Tag=%s Class=%s)"),
		       *AbilitySlot.Tag.ToString(),
		       *GetNameSafe(AbilitySlot.Class));
		return;
	}

	TryActivateAbilitySlotDirectly(ASC, AbilitySlot, this, TEXT("Server_ActivateAbilityInstant"));
}

void AAeyerjiPlayerController::Server_CancelActiveAbilityCast_Implementation()
{
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(P);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (!ASC || !ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting))
	{
		return;
	}

	ASC->CancelAbilities(nullptr, nullptr, nullptr);
	AJ_LOG(this, TEXT("Server_CancelActiveAbilityCast: cancelled active cast."));
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
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] StartFaceActorAndNotify pausing move loop. FaceTarget=%s AcceptAngle=%.1f TurnRate=%.1f Timeout=%.2f MoveLoopTarget=%s Mode=%s"),
			*GetNameSafe(Target),
			AcceptAngleDeg,
			MaxTurnRateDegPerSec,
			TimeoutSec,
			*GetNameSafe(MoveLoopTarget.Get()),
			MoveLoopModeText(MoveLoopMode));
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
	PopFacingRotationMode();
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

void AAeyerjiPlayerController::EnsureViewportConsole()
{
#if ALLOW_CONSOLE
	UWorld* World = GetWorld();
	if (!World || !IsLocalController())
	{
		return;
	}

	UGameViewportClient* ViewportClient = World->GetGameViewport();
	if (!ViewportClient || !GEngine || !GEngine->ConsoleClass)
	{
		return;
	}

	if (!ViewportClient->ViewportConsole)
	{
		ViewportClient->ViewportConsole = NewObject<UConsole>(ViewportClient, GEngine->ConsoleClass);
		if (ViewportClient->ViewportConsole)
		{
			GLog->AddOutputDevice(ViewportClient->ViewportConsole);
		}
	}

	if (ViewportClient->ViewportConsole)
	{
		ViewportClient->ViewportConsole->ConsoleTargetPlayer = GetLocalPlayer();
	}
#endif
}

void AAeyerjiPlayerController::PrintDisplayDebugMessage(const FString& Message)
{
	AJ_LOG(this, TEXT("%s"), *Message);

	if (GEngine && IsLocalController())
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Cyan, Message);
	}

#if ALLOW_CONSOLE
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			if (ViewportClient->ViewportConsole)
			{
				ViewportClient->ViewportConsole->OutputText(Message);
			}
		}
	}
#endif
}

UAbilitySystemComponent* AAeyerjiPlayerController::GetCheatTargetAbilitySystemComponent() const
{
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ControlledPawn))
		{
			return ASC;
		}
	}

	if (APlayerState* CurrentPlayerState = PlayerState)
	{
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CurrentPlayerState);
	}

	return nullptr;
}

void AAeyerjiPlayerController::ApplyCheatAttackDamage(const float DamageValue)
{
	UAbilitySystemComponent* ASC = GetCheatTargetAbilitySystemComponent();
	if (!ASC)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetDamage failed - AbilitySystemComponent unavailable."));
		return;
	}

	const float ClampedDamageValue = FMath::Max(0.f, DamageValue);
	ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetAttackDamageAttribute(), ClampedDamageValue);

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied AttackDamage %.2f."),
		ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute())));
}

void AAeyerjiPlayerController::ApplyCheatHP(const float HPValue)
{
	UAbilitySystemComponent* ASC = GetCheatTargetAbilitySystemComponent();
	if (!ASC)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetHP failed - AbilitySystemComponent unavailable."));
		return;
	}

	const float ClampedHPValue = FMath::Max(1.f, HPValue);
	ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), ClampedHPValue);
	ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), ClampedHPValue);

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied HP %.2f / %.2f."),
		ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()),
		ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute())));
}

void AAeyerjiPlayerController::AJ_DisplayInfo()
{
	EnsureViewportConsole();

	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_DisplayInfo failed - GameUserSettings unavailable."));
		return;
	}

	float CurrentScaleNormalized = 0.f;
	float CurrentScaleValue = 0.f;
	float MinScaleValue = 0.f;
	float MaxScaleValue = 0.f;
	UserSettings->GetResolutionScaleInformationEx(CurrentScaleNormalized, CurrentScaleValue, MinScaleValue, MaxScaleValue);

	const FIntPoint DesktopResolution = UserSettings->GetDesktopResolution();
	const FIntPoint ScreenResolution = UserSettings->GetScreenResolution();
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	GetViewportSize(ViewportWidth, ViewportHeight);
	const int32 bHighDPIEnabled = FPlatformApplicationMisc::IsHighDPIAwarenessEnabled() ? 1 : 0;
	const int32 bFixedFPS = GEngine && GEngine->bUseFixedFrameRate ? 1 : 0;
	const float FixedFPS = (GEngine && GEngine->bUseFixedFrameRate) ? GEngine->FixedFrameRate : 0.f;
	const FString Message = FString::Printf(
		TEXT("DisplayInfo Desktop=%dx%d Viewport=%dx%d Screen=%dx%d Mode=%s HighDPI=%d ResScale=%.2f Range=[%.2f..%.2f] OverallQuality=%d DynamicRes=%d VSync=%d FixedFPS=%d FixedRate=%.2f"),
		DesktopResolution.X,
		DesktopResolution.Y,
		ViewportWidth,
		ViewportHeight,
		ScreenResolution.X,
		ScreenResolution.Y,
		GetWindowModeLabel(UserSettings->GetFullscreenMode()),
		bHighDPIEnabled,
		CurrentScaleValue,
		MinScaleValue,
		MaxScaleValue,
		UserSettings->GetOverallScalabilityLevel(),
		UserSettings->IsDynamicResolutionEnabled() ? 1 : 0,
		UserSettings->IsVSyncEnabled() ? 1 : 0,
		bFixedFPS,
		FixedFPS);

	PrintDisplayDebugMessage(Message);
}

void AAeyerjiPlayerController::AJ_SetResolution(const int32 Width, const int32 Height, const int32 WindowMode)
{
	EnsureViewportConsole();

	if (Width <= 0 || Height <= 0)
	{
		PrintDisplayDebugMessage(TEXT("Usage: AJ_SetResolution <Width> <Height> [WindowMode 0=Fullscreen 1=WindowedFullscreen 2=Windowed]"));
		return;
	}

	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetResolution failed - GameUserSettings unavailable."));
		return;
	}

	const EWindowMode::Type NewWindowMode = ResolveWindowModeFromIndex(WindowMode);
	UserSettings->SetFullscreenMode(NewWindowMode);
	UserSettings->SetScreenResolution(FIntPoint(Width, Height));
	UserSettings->SetDynamicResolutionEnabled(false);
	UserSettings->ApplySettings(false);
	UserSettings->ConfirmVideoMode();
	UserSettings->SaveSettings();

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied resolution %dx%d with mode %s."),
		Width,
		Height,
		GetWindowModeLabel(NewWindowMode)));
	AJ_DisplayInfo();
}

void AAeyerjiPlayerController::AJ_UseDesktopResolution()
{
	EnsureViewportConsole();

	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_UseDesktopResolution failed - GameUserSettings unavailable."));
		return;
	}

	const FIntPoint DesktopResolution = UserSettings->GetDesktopResolution();
	if (DesktopResolution.X <= 0 || DesktopResolution.Y <= 0)
	{
		PrintDisplayDebugMessage(TEXT("AJ_UseDesktopResolution failed - desktop resolution is invalid."));
		return;
	}

	UserSettings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	UserSettings->SetScreenResolution(DesktopResolution);
	UserSettings->SetResolutionScaleValueEx(100.f);
	UserSettings->SetDynamicResolutionEnabled(false);
	UserSettings->ApplySettings(false);
	UserSettings->ConfirmVideoMode();
	UserSettings->SaveSettings();

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied desktop resolution %dx%d in WindowedFullscreen with resolution scale 100."),
		DesktopResolution.X,
		DesktopResolution.Y));
	AJ_DisplayInfo();
}

void AAeyerjiPlayerController::AJ_SetResolutionScale(const float ScalePercent)
{
	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetResolutionScale failed - GameUserSettings unavailable."));
		return;
	}

	float CurrentScaleNormalized = 0.f;
	float CurrentScaleValue = 0.f;
	float MinScaleValue = 0.f;
	float MaxScaleValue = 0.f;
	UserSettings->GetResolutionScaleInformationEx(CurrentScaleNormalized, CurrentScaleValue, MinScaleValue, MaxScaleValue);

	const float ClampedScale = FMath::Clamp(ScalePercent, MinScaleValue, MaxScaleValue);
	UserSettings->SetResolutionScaleValueEx(ClampedScale);
	UserSettings->SetDynamicResolutionEnabled(false);
	UserSettings->ApplySettings(false);
	UserSettings->SaveSettings();

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied resolution scale %.2f (requested %.2f, allowed range %.2f..%.2f)."),
		ClampedScale,
		ScalePercent,
		MinScaleValue,
		MaxScaleValue));
	AJ_DisplayInfo();
}

void AAeyerjiPlayerController::AJ_SetOverallQuality(const int32 QualityLevel)
{
	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetOverallQuality failed - GameUserSettings unavailable."));
		return;
	}

	const int32 ClampedQualityLevel = FMath::Clamp(QualityLevel, 0, 4);
	UserSettings->SetOverallScalabilityLevel(ClampedQualityLevel);
	UserSettings->ApplySettings(false);
	UserSettings->SaveSettings();

	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Applied overall quality level %d."),
		ClampedQualityLevel));
	AJ_DisplayInfo();
}

void AAeyerjiPlayerController::AJ_SetFPSLimit(const float FPSLimit)
{
	UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!UserSettings)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetFPSLimit failed - GameUserSettings unavailable."));
		return;
	}

	const float ClampedFPSLimit = FMath::Max(0.f, FPSLimit);
	UserSettings->SetFrameRateLimit(ClampedFPSLimit);
	UserSettings->ApplySettings(false);
	UserSettings->SaveSettings();

	if (ClampedFPSLimit <= 0.f)
	{
		PrintDisplayDebugMessage(TEXT("Applied FPS limit 0.00 (uncapped)."));
	}
	else
	{
		PrintDisplayDebugMessage(FString::Printf(
			TEXT("Applied FPS limit %.2f."),
			ClampedFPSLimit));
	}

	if (GEngine && GEngine->bUseFixedFrameRate)
	{
		PrintDisplayDebugMessage(FString::Printf(
			TEXT("Warning: fixed framerate is still enabled at %.2f, so the runtime cap will stay there until AJ_SetFixedFPS 0."),
			GEngine->FixedFrameRate));
	}
}

void AAeyerjiPlayerController::AJ_SetFixedFPS(const float FixedFPS)
{
	if (!GEngine)
	{
		PrintDisplayDebugMessage(TEXT("AJ_SetFixedFPS failed - Engine unavailable."));
		return;
	}

	const float ClampedFixedFPS = FMath::Max(0.f, FixedFPS);
	GEngine->bUseFixedFrameRate = ClampedFixedFPS > 0.f;
	GEngine->FixedFrameRate = ClampedFixedFPS;

	if (GEngine->bUseFixedFrameRate)
	{
		PrintDisplayDebugMessage(FString::Printf(
			TEXT("Applied fixed framerate %.2f."),
			ClampedFixedFPS));
	}
	else
	{
		PrintDisplayDebugMessage(TEXT("Disabled fixed framerate."));
	}

	AJ_DisplayInfo();
}

void AAeyerjiPlayerController::AJ_SetDamage(const float DamageValue)
{
	if (HasAuthority())
	{
		ApplyCheatAttackDamage(DamageValue);
		return;
	}

	ServerAJ_SetDamage(DamageValue);
	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Requested AttackDamage %.2f on the server."),
		FMath::Max(0.f, DamageValue)));
}

void AAeyerjiPlayerController::AJ_SetHP(const float HPValue)
{
	if (HasAuthority())
	{
		ApplyCheatHP(HPValue);
		return;
	}

	ServerAJ_SetHP(HPValue);
	PrintDisplayDebugMessage(FString::Printf(
		TEXT("Requested HP %.2f on the server."),
		FMath::Max(1.f, HPValue)));
}

void AAeyerjiPlayerController::AJ_OpenConsole()
{
#if ALLOW_CONSOLE
	EnsureViewportConsole();

	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			if (ViewportClient->ViewportConsole)
			{
				ViewportClient->ViewportConsole->FakeGotoState(FName(TEXT("Typing")));
				PrintDisplayDebugMessage(TEXT("Console opened. Try AJ_DisplayInfo, AJ_SetFPSLimit 60, AJ_SetFixedFPS 0, AJ_SetDamage 500, AJ_SetHP 5000, AJ_UseDesktopResolution, AJ_SetResolution 1920 1080 1, AJ_SetResolutionScale 100, or AJ_SetOverallQuality 3."));
				return;
			}
		}
	}

	PrintDisplayDebugMessage(TEXT("AJ_OpenConsole failed - viewport console unavailable."));
#else
	PrintDisplayDebugMessage(TEXT("AJ_OpenConsole is unavailable in this build."));
#endif
}

void AAeyerjiPlayerController::ServerAJ_SetDamage_Implementation(const float DamageValue)
{
	ApplyCheatAttackDamage(DamageValue);
}

void AAeyerjiPlayerController::ServerAJ_SetHP_Implementation(const float HPValue)
{
	ApplyCheatHP(HPValue);
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
