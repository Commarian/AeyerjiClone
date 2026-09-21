#include "Testing/AeyerjiCombatBalanceTestHarness.h"
#include "Testing/AeyerjiCombatTestIsolation.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "Aeyerji/AeyerjiGameInstance.h"
#include "AeyerjiGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/BoxComponent.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "DrawDebugHelpers.h"
#include "Enemy/EnemyParentNative.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameplayEffectExtension.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Player/PlayerParentNative.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Progression/AeyerjiXPLibrary.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "VisualLogger/VisualLogger.h"

DEFINE_LOG_CATEGORY(LogAeyerjiCombatTest);

const FName AAeyerjiCombatBalanceTestHarness::TestEnemyActorTag(TEXT("AeyerjiCombatTestEnemy"));

namespace
{
	constexpr float PopulationSampleIntervalSeconds = 0.25f;
	constexpr float LocalHUDIntervalSeconds = 0.25f;
	// The 750 authored base receives the canonical level-one derived-stat contribution at runtime.
	constexpr float CanonicalPlayerHP = 760.f;
	constexpr float CanonicalPlayerAttackDamage = 25.f;
	constexpr float CanonicalAttributeTolerance = 0.05f;
	constexpr float GoldenAngleRadians = 2.39996322972865332f;
	constexpr uint64 CombatTestHUDMessageKey = 0xA3E7C0DEull;
	constexpr int32 MaximumStoredPopulationSamples = 24000;
	constexpr int32 MaximumStoredDamageEvents = 8192;
	constexpr int32 MaximumStoredHealingEvents = 8192;
	constexpr int32 MaximumStoredProgressionEvents = 8192;
	constexpr int32 MaximumStoredItemEvents = 4096;
	constexpr int32 MaximumStoredHitchEvents = 4096;
	constexpr int32 MaximumStoredKillRewardEvents = 8192;
	constexpr float AuthorityHitchThresholdSeconds = 0.05f;
	constexpr float KillRewardMatchWindowSeconds = 1.f;
	constexpr int32 MaximumPlacementAttemptsPerEnemy = 12;

	const TCHAR* CombatTestSpawnerClassPath =
		TEXT("/Game/Levels/Director/BP_AeyerjiSpawnerGroup.BP_AeyerjiSpawnerGroup_C");
	const TCHAR* GruntClassPath =
		TEXT("/Game/Enemy/Map_1_Creeps/M_Grunt/M_Grunt.M_Grunt_C");
	const TCHAR* BulwarkClassPath =
		TEXT("/Game/Enemy/Map_1_Creeps/M_Bulwark/M_Bulwark.M_Bulwark_C");
	const TCHAR* ArcherClassPath =
		TEXT("/Game/Enemy/Map_1_Creeps/R_Archer/R_Archer.R_Archer_C");
	const TCHAR* SupportClassPath =
		TEXT("/Game/Enemy/Map_1_Creeps/R_Support/R_Support.R_Support_C");
	const TCHAR* GruntEliteClassPath =
		TEXT("/Game/Enemy/Map_1_Creeps/M_Grunt/M_GruntElite.M_GruntElite_C");

	bool bLocalCombatTestHUDEnabled = true;

	int32 ResolveLiveEnemyLevel(const AEnemyParentNative* Enemy)
	{
		if (const UAbilitySystemComponent* ASC = Enemy ? Enemy->GetAbilitySystemComponent() : nullptr)
		{
			return UAeyerjiDifficultySettings::FloatToGameplayLevel(
				ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()));
		}
		return Enemy ? FMath::Max(1, Enemy->GetScaledLevel()) : 1;
	}

	FName CanonicalizeComposition(const FString& Input)
	{
		if (Input.Equals(TEXT("BombardierMixed"), ESearchCase::IgnoreCase))
		{
			return TEXT("BombardierMixed");
		}
		if (Input.Equals(TEXT("Grunt"), ESearchCase::IgnoreCase))
		{
			return TEXT("Grunt");
		}
		if (Input.Equals(TEXT("Bulwark"), ESearchCase::IgnoreCase))
		{
			return TEXT("Bulwark");
		}
		if (Input.Equals(TEXT("Archer"), ESearchCase::IgnoreCase))
		{
			return TEXT("Archer");
		}
		if (Input.Equals(TEXT("Support"), ESearchCase::IgnoreCase))
		{
			return TEXT("Support");
		}
		if (Input.Equals(TEXT("Ranged"), ESearchCase::IgnoreCase))
		{
			return TEXT("Ranged");
		}
		if (Input.Equals(TEXT("Mixed"), ESearchCase::IgnoreCase))
		{
			return TEXT("Mixed");
		}
		if (Input.Equals(TEXT("MixedElite"), ESearchCase::IgnoreCase)
			|| Input.Equals(TEXT("EliteMixed"), ESearchCase::IgnoreCase))
		{
			return TEXT("MixedElite");
		}
		if (Input.Equals(TEXT("Elite"), ESearchCase::IgnoreCase)
			|| Input.Equals(TEXT("GruntElite"), ESearchCase::IgnoreCase))
		{
			return TEXT("Elite");
		}

		return NAME_None;
	}

	TSubclassOf<APawn> LoadPawnClass(const TCHAR* ClassPath)
	{
		return StaticLoadClass(APawn::StaticClass(), nullptr, ClassPath);
	}

	FString EscapeCSV(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	FString SanitizeMarkerLabel(const FString& Input)
	{
		FString Result = Input.Left(80);
		Result.ReplaceInline(TEXT("\n"), TEXT(" "));
		Result.ReplaceInline(TEXT("\r"), TEXT(" "));
		Result.TrimStartAndEndInline();
		return Result.IsEmpty() ? TEXT("Manual") : Result;
	}
}

AAeyerjiCombatBalanceTestHarness::AAeyerjiCombatBalanceTestHarness()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(5.f);
	SetMinNetUpdateFrequency(2.f);
}

void AAeyerjiCombatBalanceTestHarness::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		EnemyXPAwardedHandle = UAeyerjiXPLibrary::OnEnemyXPAwarded().AddUObject(
			this,
			&AAeyerjiCombatBalanceTestHarness::HandleEnemyXPAwarded);
	}
}

AAeyerjiCombatBalanceTestHarness* AAeyerjiCombatBalanceTestHarness::FindForWorld(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AAeyerjiCombatBalanceTestHarness> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}

	return nullptr;
}

bool AAeyerjiCombatBalanceTestHarness::ResolvePreset(
	const FString& PresetName,
	FAeyerjiCombatTestRequest& OutRequest,
	FString& OutError)
{
	OutError.Reset();
	OutRequest = FAeyerjiCombatTestRequest();

	if (PresetName.Equals(TEXT("Sanity1"), ESearchCase::IgnoreCase)
		|| PresetName.Equals(TEXT("One"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Sanity1");
		OutRequest.CompositionName = TEXT("Grunt");
		OutRequest.EnemyCount = 1;
		OutRequest.MinimumSpawnRadius = 900.f;
		OutRequest.MaximumSpawnRadius = 1000.f;
	}
	else if (PresetName.Equals(TEXT("Floor8"), ESearchCase::IgnoreCase)
		|| PresetName.Equals(TEXT("Pressure8"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Floor8");
		OutRequest.CompositionName = TEXT("Mixed");
		OutRequest.EnemyCount = 8;
		OutRequest.MinimumSpawnRadius = 1000.f;
		OutRequest.MaximumSpawnRadius = 1800.f;
	}
	else if (PresetName.Equals(TEXT("Region12"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Region12");
		OutRequest.CompositionName = TEXT("Mixed");
		OutRequest.EnemyCount = 12;
		OutRequest.MinimumSpawnRadius = 1100.f;
		OutRequest.MaximumSpawnRadius = 2200.f;
	}
	else if (PresetName.Equals(TEXT("Bombardier12"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Bombardier12");
		OutRequest.CompositionName = TEXT("BombardierMixed");
		OutRequest.EnemyCount = 12;
		OutRequest.MinimumSpawnRadius = 1100.f;
		OutRequest.MaximumSpawnRadius = 2200.f;
	}
	else if (PresetName.Equals(TEXT("Ranged12"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Ranged12");
		OutRequest.CompositionName = TEXT("Ranged");
		OutRequest.EnemyCount = 12;
		OutRequest.MinimumSpawnRadius = 1400.f;
		OutRequest.MaximumSpawnRadius = 2600.f;
	}
	else if (PresetName.Equals(TEXT("Grunt20"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Grunt20");
		OutRequest.CompositionName = TEXT("Grunt");
		OutRequest.EnemyCount = 20;
		OutRequest.MinimumSpawnRadius = 1100.f;
		OutRequest.MaximumSpawnRadius = 2400.f;
	}
	else if (PresetName.Equals(TEXT("Dense24"), ESearchCase::IgnoreCase)
		|| PresetName.Equals(TEXT("Mixed24"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Dense24");
		OutRequest.CompositionName = TEXT("Mixed");
		OutRequest.EnemyCount = 24;
		OutRequest.MinimumSpawnRadius = 1200.f;
		OutRequest.MaximumSpawnRadius = 2600.f;
	}
	else if (PresetName.Equals(TEXT("Elite24"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Elite24");
		OutRequest.CompositionName = TEXT("MixedElite");
		OutRequest.EnemyCount = 24;
		OutRequest.MinimumSpawnRadius = 1200.f;
		OutRequest.MaximumSpawnRadius = 2600.f;
	}
	else if (PresetName.Equals(TEXT("Overwhelmed36"), ESearchCase::IgnoreCase)
		|| PresetName.Equals(TEXT("Mixed36"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Overwhelmed36");
		OutRequest.CompositionName = TEXT("Mixed");
		OutRequest.EnemyCount = 36;
		OutRequest.MinimumSpawnRadius = 1300.f;
		OutRequest.MaximumSpawnRadius = 2900.f;
	}
	else if (PresetName.Equals(TEXT("Cap48"), ESearchCase::IgnoreCase)
		|| PresetName.Equals(TEXT("Mixed48"), ESearchCase::IgnoreCase))
	{
		OutRequest.ScenarioName = TEXT("Cap48");
		OutRequest.CompositionName = TEXT("Mixed");
		OutRequest.EnemyCount = 48;
		OutRequest.MinimumSpawnRadius = 1400.f;
		OutRequest.MaximumSpawnRadius = 3200.f;
	}
	else
	{
		OutError = FString::Printf(
			TEXT("Unknown combat-test preset '%s'. Valid presets: %s"),
			*PresetName,
			*GetPresetList());
		return false;
	}

	return true;
}

bool AAeyerjiCombatBalanceTestHarness::SanitizeRequest(
	FAeyerjiCombatTestRequest& InOutRequest,
	FString& OutError)
{
	OutError.Reset();
	const FName CanonicalComposition = CanonicalizeComposition(InOutRequest.CompositionName.ToString());
	if (CanonicalComposition.IsNone())
	{
		OutError = FString::Printf(
			TEXT("Unknown composition '%s'. Valid compositions: Grunt, Bulwark, Archer, Support, Ranged, Mixed, MixedElite, Elite."),
			*InOutRequest.CompositionName.ToString());
		return false;
	}

	if (InOutRequest.EnemyCount < 1 || InOutRequest.EnemyCount > MaximumTestEnemyCount)
	{
		OutError = FString::Printf(
			TEXT("Enemy count must be between 1 and %d; received %d."),
			MaximumTestEnemyCount,
			InOutRequest.EnemyCount);
		return false;
	}

	if (!FMath::IsFinite(InOutRequest.MinimumSpawnRadius)
		|| !FMath::IsFinite(InOutRequest.MaximumSpawnRadius)
		|| InOutRequest.MinimumSpawnRadius < 300.f
		|| InOutRequest.MaximumSpawnRadius < InOutRequest.MinimumSpawnRadius
		|| InOutRequest.MaximumSpawnRadius > 10000.f)
	{
		OutError = TEXT("Spawn radii must be finite, ordered, and inside [300, 10000] cm.");
		return false;
	}

	if (!FMath::IsFinite(InOutRequest.SpawnInterval)
		|| InOutRequest.SpawnInterval < 0.f
		|| InOutRequest.SpawnInterval > 2.f)
	{
		OutError = TEXT("Spawn interval must be finite and inside [0, 2] seconds.");
		return false;
	}

	if (!FMath::IsFinite(InOutRequest.AutoEngageDelay)
		|| InOutRequest.AutoEngageDelay < -1.f
		|| InOutRequest.AutoEngageDelay > 60.f)
	{
		OutError = TEXT("Auto-engage delay must be -1 for manual engagement or inside [0, 60] seconds.");
		return false;
	}

	InOutRequest.CompositionName = CanonicalComposition;
	InOutRequest.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(InOutRequest.EnemyLevel);
	InOutRequest.WorldTier = FMath::Clamp(
		InOutRequest.WorldTier,
		0,
		UAeyerjiDifficultySettings::WorldTierMax);
	if (InOutRequest.ScenarioName.IsNone())
	{
		InOutRequest.ScenarioName = FName(*FString::Printf(
			TEXT("Custom%s%d"),
			*InOutRequest.CompositionName.ToString(),
			InOutRequest.EnemyCount));
	}

	return true;
}

bool AAeyerjiCombatBalanceTestHarness::StartPreset(
	UWorld* World,
	AAeyerjiPlayerController* RequestingController,
	const FString& PresetName,
	const int32 InEnemyLevel,
	const int32 InWorldTier,
	const int32 Seed,
	const float AutoEngageDelay,
	FString& OutMessage)
{
	FAeyerjiCombatTestRequest Request;
	if (!ResolvePreset(PresetName, Request, OutMessage))
	{
		return false;
	}

	Request.EnemyLevel = InEnemyLevel;
	Request.WorldTier = InWorldTier;
	Request.Seed = Seed;
	Request.AutoEngageDelay = AutoEngageDelay;
	return StartCustom(World, RequestingController, Request, OutMessage);
}

bool AAeyerjiCombatBalanceTestHarness::StartCustom(
	UWorld* World,
	AAeyerjiPlayerController* RequestingController,
	FAeyerjiCombatTestRequest Request,
	FString& OutMessage)
{
#if UE_BUILD_SHIPPING
	OutMessage = TEXT("Combat-balance test commands are unavailable in Shipping builds.");
	return false;
#else
	if (!World || !IsValid(RequestingController) || !RequestingController->HasAuthority())
	{
		OutMessage = TEXT("Combat test must be requested by an authoritative Aeyerji player controller.");
		return false;
	}

	// Headless runtime verification boots NeonMap in its intentional front-end spectator phase.
	// This explicit non-shipping flag creates the normal default pawn without weakening world-flow
	// gating for interactive PIE, standalone play, or shipped builds.
	if ((!IsValid(RequestingController->GetPawn()) || RequestingController->GetPawn()->IsA<ASpectatorPawn>())
		&& FParse::Param(FCommandLine::Get(), TEXT("AeyerjiCombatTestAutoSpawn")))
	{
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			GameMode->RestartPlayer(RequestingController);
		}
	}

	if (!IsValid(RequestingController->GetPawn()) || RequestingController->GetPawn()->IsA<ASpectatorPawn>())
	{
		OutMessage = TEXT("Combat-test setup requires a possessed gameplay pawn. Start a run first; headless smoke tests may use -AeyerjiCombatTestAutoSpawn.");
		return false;
	}

	if (!SanitizeRequest(Request, OutMessage))
	{
		return false;
	}

	if (AAeyerjiCombatBalanceTestHarness* Existing = FindForWorld(World))
	{
		Existing->StopTest(true);
		Existing->Destroy();
	}

	// Recover only actors bearing the reserved test tag if a previous PIE session or hot reload
	// removed its harness before cleanup. Production encounter actors are never touched here.
	for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
	{
		AEnemyParentNative* Enemy = *It;
		if (IsValid(Enemy) && Enemy->ActorHasTag(TestEnemyActorTag))
		{
			Enemy->Destroy();
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = RequestingController;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAeyerjiCombatBalanceTestHarness* Harness = World->SpawnActor<AAeyerjiCombatBalanceTestHarness>(
		StaticClass(),
		RequestingController->GetPawn() ? RequestingController->GetPawn()->GetActorLocation() : FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Harness)
	{
		OutMessage = TEXT("Failed to create the transient combat-test harness.");
		return false;
	}

	if (!Harness->StartTest(Request, RequestingController, OutMessage))
	{
		// A failed setup is not a player-requested stop. Preserve a useful SetupFailed report only
		// after the scenario was initialized, then clean up without emitting ManualStop.
		if (!Harness->ScenarioName.IsNone())
		{
			Harness->CompleteTest(TEXT("SetupFailed"));
		}
		Harness->CleanupSpawnedActors();
		Harness->Destroy();
		return false;
	}

	OutMessage = FString::Printf(
		TEXT("Prepared %s: %d %s enemies at level %d / world tier %d / seed %d. %s"),
		*Request.ScenarioName.ToString(),
		Request.EnemyCount,
		*Request.CompositionName.ToString(),
		Request.EnemyLevel,
		Request.WorldTier,
		Request.Seed,
		Request.AutoEngageDelay < 0.f
			? TEXT("Use AJ_CombatTestEngage when Rewind recording is ready.")
			: *FString::Printf(TEXT("Auto-engage follows %.1f seconds after assembly."), Request.AutoEngageDelay));
	return true;
#endif
}

bool AAeyerjiCombatBalanceTestHarness::StartProductionObservation(
	UWorld* World,
	AAeyerjiPlayerController* RequestingController,
	const FString& RunLabel,
	FString& OutMessage)
{
#if UE_BUILD_SHIPPING
	OutMessage = TEXT("Balance recording is unavailable in Shipping builds.");
	return false;
#else
	if (!World || !IsValid(RequestingController) || !RequestingController->HasAuthority()
		|| !IsValid(RequestingController->GetPawn()) || RequestingController->GetPawn()->IsA<ASpectatorPawn>())
	{
		OutMessage = TEXT("Production balance recording requires authority and a possessed gameplay pawn.");
		return false;
	}
	if (AeyerjiCombatTestIsolation::IsWorldSpawningSuppressed())
	{
		OutMessage = TEXT("Production balance recording requires normal world spawning. Disable combat-test isolation first.");
		return false;
	}

	if (AAeyerjiCombatBalanceTestHarness* Existing = FindForWorld(World))
	{
		Existing->StopTest(true);
		Existing->Destroy();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = RequestingController;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAeyerjiCombatBalanceTestHarness* Recorder = World->SpawnActor<AAeyerjiCombatBalanceTestHarness>(
		StaticClass(),
		RequestingController->GetPawn()->GetActorLocation(),
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Recorder)
	{
		OutMessage = TEXT("Failed to create the production Rift balance recorder.");
		return false;
	}

	if (!Recorder->StartProductionObservationInternal(RequestingController, RunLabel, OutMessage))
	{
		Recorder->Destroy();
		return false;
	}

	OutMessage = FString::Printf(
		TEXT("Started production Rift balance run '%s'. Normal spawning remains enabled; use AJ_BalanceRunStop to save reports."),
		*Recorder->ScenarioName.ToString());
	return true;
#endif
}

float AAeyerjiCombatBalanceTestHarness::CalculateCountRate(const int32 Count, const float ElapsedSeconds)
{
	return Count > 0 && FMath::IsFinite(ElapsedSeconds) && ElapsedSeconds > UE_SMALL_NUMBER
		? static_cast<float>(Count) / ElapsedSeconds
		: 0.f;
}

float AAeyerjiCombatBalanceTestHarness::CalculateNetPressure(
	const float ArrivalRatePerSecond,
	const float KillsPerTenSeconds)
{
	const float SafeArrivalRate = FMath::IsFinite(ArrivalRatePerSecond) ? FMath::Max(0.f, ArrivalRatePerSecond) : 0.f;
	const float SafeKillsPerTenSeconds = FMath::IsFinite(KillsPerTenSeconds) ? FMath::Max(0.f, KillsPerTenSeconds) : 0.f;
	return SafeArrivalRate - SafeKillsPerTenSeconds / 10.f;
}

float AAeyerjiCombatBalanceTestHarness::CalculateHitchOverageMilliseconds(
	const float FrameDeltaSeconds,
	const float ThresholdSeconds)
{
	if (!FMath::IsFinite(FrameDeltaSeconds) || !FMath::IsFinite(ThresholdSeconds))
	{
		return 0.f;
	}

	return FMath::Max(0.f, FrameDeltaSeconds - FMath::Max(0.f, ThresholdSeconds)) * 1000.f;
}

bool AAeyerjiCombatBalanceTestHarness::StartProductionObservationInternal(
	AAeyerjiPlayerController* RequestingController,
	const FString& RunLabel,
	FString& OutError)
{
	if (!HasAuthority() || !IsValid(RequestingController) || !IsValid(RequestingController->GetPawn()))
	{
		OutError = TEXT("Production observation requires an authoritative possessed player.");
		return false;
	}

	bObservingProductionRift = true;
	TestedController = RequestingController;
	TestedPawn = RequestingController->GetPawn();
	FString SafeLabel = FPaths::MakeValidFileName(RunLabel.TrimStartAndEnd());
	if (SafeLabel.IsEmpty())
	{
		SafeLabel = TEXT("L1_Naked_Baseline");
	}
	ScenarioName = FName(*SafeLabel.Left(NAME_SIZE - 1));
	CompositionName = TEXT("ProductionRift");
	TestCenter = TestedPawn->GetActorLocation();
	SetActorLocation(TestCenter);
	RequestedEnemyCount = 0;
	FailedSpawnCount = 0;
	TestSeed = 0;

	if (!CapturePlayerBaseline(OutError))
	{
		CompletionReason = TEXT("SetupFailed");
		return false;
	}
	EnemyLevel = UAeyerjiDifficultySettings::FloatToGameplayLevel(
		TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()));
	if (const UAeyerjiGameInstance* GameInstance = Cast<UAeyerjiGameInstance>(GetGameInstance()))
	{
		WorldTier = GameInstance->GetWorldTier();
	}

	ResetBombardmentTelemetry();
	ResetTargetingTelemetry();
	DamageEvents.Reset();
	HealingEvents.Reset();
	PopulationSamples.Reset();
	HitchEvents.Reset();
	KillRewardEvents.Reset();
	ProgressionEvents.Reset();
	ItemEvents.Reset();
	MarkerEvents.Reset();
	ArrivedThreateningEnemies.Reset();
	DamageByArchetype.Reset();
	HealingBySource.Reset();
	LastObservedEnemyLevels.Reset();
	SpawnedEnemies.Reset();
	EnemyKills = 0;
	PlayerKillsPerTenSeconds = 0.f;
	ThreateningArrivalCount = 0;
	ThreateningArrivalsPerSecond = 0.f;
	ThreateningEnemyCount = 0;
	UniqueHittersOneSecond = 0;
	TotalIncomingDamage = 0.f;
	AverageIncomingDPS = 0.f;
	TotalHealing = 0.f;
	AverageHealingPerSecond = 0.f;
	PlayerDeathCount = 0;
	PlayerRespawnCount = 0;
	ItemsAddedDuringRun = 0;
	ItemsEquippedDuringRun = 0;
	PeakDamageHalfSecond = 0.f;
	PeakDamageOneSecond = 0.f;
	PeakDamageThreeSeconds = 0.f;
	TimeToFirstDamage = -1.f;
	TimeToFirstKill = -1.f;
	ClearTime = -1.f;
	bFirstDamageMarked = false;
	bFirstKillMarked = false;
	bCleanupPerformed = false;
	bReportWritten = false;
	bAwaitingPlayerRespawn = false;
	bHasProgressionSnapshot = false;
	LastPlayerDeathElapsedTime = -1.f;
	LatestAuthorityFrameDeltaMs = 0.f;
	LastPopulationSampleElapsedSeconds = -1.f;
	MaximumAuthorityFrameDeltaMs = 0.f;
	TotalAuthorityHitchOverageMs = 0.f;
	AuthorityHitchCount = 0;
	ReportBasePath.Reset();
	CompletionReason = NAME_None;
	TestState = EAeyerjiCombatTestState::Running;
	CombatStartWorldTime = GetWorld()->GetTimeSeconds();
	NextSampleWorldTime = CombatStartWorldTime;
	ElapsedCombatSeconds = 0.f;
	RefreshObservedProductionEnemies();
	EmitMarker(TEXT("ProductionRunStart"));
	UpdateAuthoritativeSnapshot(true);
	ForceNetUpdate();
	return true;
}

bool AAeyerjiCombatBalanceTestHarness::StartTest(
	const FAeyerjiCombatTestRequest& Request,
	AAeyerjiPlayerController* RequestingController,
	FString& OutError)
{
	if (!HasAuthority()
		|| !IsValid(RequestingController)
		|| !IsValid(RequestingController->GetPawn())
		|| RequestingController->GetPawn()->IsA<ASpectatorPawn>())
	{
		OutError = TEXT("Combat-test setup requires authority and a possessed player pawn.");
		return false;
	}

	ActiveRequest = Request;
	if (!SanitizeRequest(ActiveRequest, OutError))
	{
		return false;
	}

	TestedController = RequestingController;
	TestedPawn = RequestingController->GetPawn();
	ScenarioName = ActiveRequest.ScenarioName;
	CompositionName = ActiveRequest.CompositionName;
	EnemyLevel = ActiveRequest.EnemyLevel;
	WorldTier = ActiveRequest.WorldTier;
	TestSeed = ActiveRequest.Seed;
	RequestedEnemyCount = ActiveRequest.EnemyCount;
	MinimumSpawnRadius = ActiveRequest.MinimumSpawnRadius;
	MaximumSpawnRadius = ActiveRequest.MaximumSpawnRadius;
	TestCenter = TestedPawn->GetActorLocation();
	SetActorLocation(TestCenter);

	if (!BuildSpawnRoster(OutError))
	{
		CompletionReason = TEXT("SetupFailed");
		return false;
	}

	UClass* SpawnerClass = StaticLoadClass(
		AAeyerjiSpawnerGroup::StaticClass(),
		nullptr,
		CombatTestSpawnerClassPath);
	if (!SpawnerClass)
	{
		OutError = FString::Printf(TEXT("Could not load combat-test spawner class %s."), CombatTestSpawnerClassPath);
		CompletionReason = TEXT("SetupFailed");
		return false;
	}

	FActorSpawnParameters SpawnerParameters;
	SpawnerParameters.Owner = this;
	SpawnerParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TestSpawner = GetWorld()->SpawnActor<AAeyerjiSpawnerGroup>(
		SpawnerClass,
		TestCenter,
		FRotator::ZeroRotator,
		SpawnerParameters);
	if (!TestSpawner)
	{
		OutError = TEXT("Failed to create the production Blueprint spawner used by combat tests.");
		CompletionReason = TEXT("SetupFailed");
		return false;
	}

	if (TestSpawner->ActivationVolume)
	{
		TestSpawner->ActivationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	TestSpawner->bPermanentRiftPursuit = ActiveRequest.bApplyAggro;
	TestSpawner->AggroSettings.bEnableAggro = ActiveRequest.bApplyAggro;
	TestSpawner->SetCombatTestScalingOverrides(EnemyLevel, WorldTier);

	if (!CapturePlayerBaseline(OutError))
	{
		CompletionReason = TEXT("SetupFailed");
		return false;
	}

	TestState = EAeyerjiCombatTestState::Spawning;
	NextSpawnIndex = 0;
	NextSpawnWorldTime = GetWorld()->GetTimeSeconds();
	CompletionReason = NAME_None;
	EmitMarker(TEXT("SpawnBegin"));
	if (!bCanonicalPlayerBaseline)
	{
		UE_LOG(LogAeyerjiCombatTest, Warning,
			TEXT("[CombatTest] Non-canonical player baseline for %s: %s. Equipment/effects are not removed automatically."),
			*ScenarioName.ToString(),
			*BaselineDescription);
	}
	ForceNetUpdate();
	return true;
}

bool AAeyerjiCombatBalanceTestHarness::CapturePlayerBaseline(FString& OutError)
{
	BindPlayerHealth();
	BindPlayerInventory();
	if (!TestedAbilitySystem.IsValid())
	{
		OutError = TEXT("The tested player has no AbilitySystemComponent; damage metrics cannot be collected.");
		return false;
	}

	const float PlayerLevel = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute());
	PlayerHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	PlayerMaxHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	PlayerAttackDamage = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
	LowestPlayerHealth = PlayerHealth;
	bCanonicalPlayerBaseline = FMath::IsNearlyEqual(PlayerLevel, 1.f, CanonicalAttributeTolerance)
		&& FMath::IsNearlyEqual(PlayerMaxHealth, CanonicalPlayerHP, CanonicalAttributeTolerance)
		&& FMath::IsNearlyEqual(PlayerHealth, PlayerMaxHealth, CanonicalAttributeTolerance)
		&& FMath::IsNearlyEqual(PlayerAttackDamage, CanonicalPlayerAttackDamage, CanonicalAttributeTolerance);
	BaselineDescription = FString::Printf(
		TEXT("PlayerLevel=%.2f HP=%.2f/%.2f AttackDamage=%.2f CanonicalLevel1Naked=%s"),
		PlayerLevel,
		PlayerHealth,
		PlayerMaxHealth,
		PlayerAttackDamage,
		bCanonicalPlayerBaseline ? TEXT("PASS") : TEXT("WARNING"));
	if (!bCanonicalPlayerBaseline)
	{
		UE_LOG(LogAeyerjiCombatTest, Warning,
			TEXT("[CombatTest] Baseline warning for %s: %s. Equipment/effects are recorded but never changed automatically."),
			*ScenarioName.ToString(),
			*BaselineDescription);
	}
	return true;
}

bool AAeyerjiCombatBalanceTestHarness::BuildSpawnRoster(FString& OutError)
{
	PendingEnemySets.Reset();
	PendingEnemySets.Reserve(ActiveRequest.EnemyCount);

	TSubclassOf<APawn> GruntClass = LoadPawnClass(GruntClassPath);
	TSubclassOf<APawn> BulwarkClass = LoadPawnClass(BulwarkClassPath);
	TSubclassOf<APawn> ArcherClass = LoadPawnClass(ArcherClassPath);
	TSubclassOf<APawn> SupportClass = LoadPawnClass(SupportClassPath);
	TSubclassOf<APawn> GruntEliteClass = LoadPawnClass(GruntEliteClassPath);
	if (!GruntClass || !BulwarkClass || !ArcherClass || !SupportClass || !GruntEliteClass)
	{
		OutError = TEXT("One or more canonical combat-test enemy Blueprint classes failed to load. See LogAeyerjiCombatTest.");
		UE_LOG(LogAeyerjiCombatTest, Error,
			TEXT("[CombatTest] Class load failure Grunt=%d Bulwark=%d Archer=%d Support=%d GruntElite=%d"),
			GruntClass ? 1 : 0,
			BulwarkClass ? 1 : 0,
			ArcherClass ? 1 : 0,
			SupportClass ? 1 : 0,
			GruntEliteClass ? 1 : 0);
		return false;
	}

	auto AppendSet = [this](const TSubclassOf<APawn> EnemyClass, const bool bElite)
	{
		FEnemySet& Set = PendingEnemySets.AddDefaulted_GetRef();
		Set.EnemyClass = EnemyClass;
		Set.Count = 1;
		Set.SpawnInterval = 0.f;
		Set.bIsElite = bElite;
		Set.bIsMiniBoss = false;
		Set.bIsBoss = false;
	};

	const FString Composition = ActiveRequest.CompositionName.ToString();
	if (Composition == TEXT("BombardierMixed"))
	{
		const TSubclassOf<APawn> BombardierClass = LoadPawnClass(
			TEXT("/Game/Enemy/Map_1_Creeps/R_Bombardier/R_Bombardier.R_Bombardier_C"));
		if (!BombardierClass)
		{
			OutError = TEXT("Bombardier12 requires the R_Bombardier character Blueprint; follow Source/Aeyerji/Docs/Bombardier.md.");
			return false;
		}
		// One specialist per test; at count 12 this yields 7 Grunts, 2 Bulwarks and 2 Archers.
		AppendSet(BombardierClass, false);
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount - 1; ++Index)
		{
			AppendSet(Index % 11 < 7 ? GruntClass : Index % 11 < 9 ? BulwarkClass : ArcherClass, false);
		}
	}
	else if (Composition == TEXT("Grunt"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(GruntClass, false);
		}
	}
	else if (Composition == TEXT("Bulwark"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(BulwarkClass, false);
		}
	}
	else if (Composition == TEXT("Archer"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(ArcherClass, false);
		}
	}
	else if (Composition == TEXT("Support"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(SupportClass, false);
		}
	}
	else if (Composition == TEXT("Elite"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(GruntEliteClass, true);
		}
	}
	else if (Composition == TEXT("Ranged"))
	{
		for (int32 Index = 0; Index < ActiveRequest.EnemyCount; ++Index)
		{
			AppendSet(Index % 3 == 2 ? SupportClass : ArcherClass, false);
		}
	}
	else
	{
		int32 Remaining = ActiveRequest.EnemyCount;
		if (Composition == TEXT("MixedElite") && Remaining > 0)
		{
			AppendSet(GruntEliteClass, true);
			--Remaining;
		}

		// The repeating six-slot recipe is intentionally simple and reviewable:
		// 50% standard melee plus equal durable, ranged, and support representation.
		const TSubclassOf<APawn> MixedPattern[] =
		{
			GruntClass,
			GruntClass,
			GruntClass,
			BulwarkClass,
			ArcherClass,
			SupportClass
		};
		for (int32 Index = 0; Index < Remaining; ++Index)
		{
			AppendSet(MixedPattern[Index % UE_ARRAY_COUNT(MixedPattern)], false);
		}
	}

	FRandomStream ShuffleStream(ActiveRequest.Seed);
	for (int32 Index = PendingEnemySets.Num() - 1; Index > 0; --Index)
	{
		PendingEnemySets.Swap(Index, ShuffleStream.RandRange(0, Index));
	}

	return PendingEnemySets.Num() == ActiveRequest.EnemyCount;
}

FVector AAeyerjiCombatBalanceTestHarness::ResolveSpawnLocation(
	const int32 SpawnIndex,
	const int32 PlacementAttempt) const
{
	const int32 SafeCount = FMath::Max(1, RequestedEnemyCount);
	FRandomStream PhaseStream(TestSeed);
	const float Phase = PhaseStream.FRandRange(0.f, 2.f * PI);
	// Retry points remain deterministic but rotate and move through the same annulus. This lets
	// the production spawner retain its per-pawn NavSafety rejection without silently reducing
	// a population preset just because its first low-discrepancy point lands off this NavMesh.
	const float BaseNormalizedIndex =
		(static_cast<float>(SpawnIndex) + 0.5f) / static_cast<float>(SafeCount);
	const float NormalizedIndex = FMath::Frac(
		BaseNormalizedIndex + static_cast<float>(PlacementAttempt) * 0.61803398875f);
	const float RadiusSquared = FMath::Lerp(
		FMath::Square(MinimumSpawnRadius),
		FMath::Square(MaximumSpawnRadius),
		NormalizedIndex);
	const float Radius = FMath::Sqrt(FMath::Max(0.f, RadiusSquared));
	const int32 SequenceIndex = SpawnIndex + PlacementAttempt * SafeCount;
	const float Angle = Phase + static_cast<float>(SequenceIndex) * GoldenAngleRadians;
	const FVector Desired = TestCenter + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);

	if (const UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Projected;
		if (Navigation->ProjectPointToNavigation(Desired, Projected, FVector(400.f, 400.f, 1200.f)))
		{
			return Projected.Location;
		}
	}

	return Desired;
}

void AAeyerjiCombatBalanceTestHarness::SpawnNextEnemy()
{
	if (!HasAuthority() || !IsValid(TestSpawner) || !PendingEnemySets.IsValidIndex(NextSpawnIndex))
	{
		return;
	}

	const FEnemySet& EnemySet = PendingEnemySets[NextSpawnIndex];
	const FVector FacingTarget = IsValid(TestedPawn) ? TestedPawn->GetActorLocation() : TestCenter;
	FVector SpawnLocation = FVector::ZeroVector;
	APawn* SpawnedPawn = nullptr;
	int32 PlacementAttemptsUsed = 0;
	for (int32 PlacementAttempt = 0;
		PlacementAttempt < MaximumPlacementAttemptsPerEnemy && !IsValid(SpawnedPawn);
		++PlacementAttempt)
	{
		SpawnLocation = ResolveSpawnLocation(NextSpawnIndex, PlacementAttempt);
		const FRotator SpawnRotation = (FacingTarget - SpawnLocation).Rotation();
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
		PlacementAttemptsUsed = PlacementAttempt + 1;
		SpawnedPawn = TestSpawner->SpawnRegisteredEnemyFromSet(
			EnemySet,
			SpawnTransform,
			this,
			TestedPawn,
			/*bApplyEliteSettings=*/true,
			/*bApplyAggro=*/ActiveRequest.bApplyAggro,
			/*bAutoActivate=*/true,
			/*bAutoActivateOnlyIfNoWaves=*/true,
			TestedPawn,
			TestedController,
			/*bSkipRandomEliteResolution=*/true);
	}

	if (!IsValid(SpawnedPawn))
	{
		++FailedSpawnCount;
		UE_LOG(LogAeyerjiCombatTest, Warning,
			TEXT("[CombatTest] Spawn rejected Scenario=%s Index=%d Class=%s Attempts=%d LastLocation=%s"),
			*ScenarioName.ToString(),
			NextSpawnIndex,
			*GetNameSafe(EnemySet.EnemyClass),
			PlacementAttemptsUsed,
			*SpawnLocation.ToCompactString());
		return;
	}

	SpawnedPawn->Tags.AddUnique(TestEnemyActorTag);
	SpawnedPawn->Tags.AddUnique(FName(*FString::Printf(TEXT("AJCT_%s"), *ScenarioName.ToString())));
	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
	{
		Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiCombatBalanceTestHarness::HandleEnemyDied);
		Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiCombatBalanceTestHarness::HandleEnemyDied);
		// Reuse the replicated production gameplay lock to assemble a deterministic full pack.
		// The harness releases every enemy together in Engage(), avoiding spawn-order bias.
		Enemy->BeginEncounterReveal(EAeyerjiEnemyRevealStyle::GroundEmergence, 60.f);
		if (Enemy->GetScaledLevel() != EnemyLevel)
		{
			UE_LOG(LogAeyerjiCombatTest, Error,
				TEXT("[CombatTest] Enemy level mismatch Scenario=%s Pawn=%s Expected=%d Actual=%d"),
				*ScenarioName.ToString(),
				*GetNameSafe(Enemy),
				EnemyLevel,
				Enemy->GetScaledLevel());
		}
	}
	else
	{
		UE_LOG(LogAeyerjiCombatTest, Warning,
			TEXT("[CombatTest] Spawned pawn %s is not AEnemyParentNative; death and active-state metrics will be incomplete."),
			*GetNameSafe(SpawnedPawn));
	}

	SpawnedEnemies.Add(SpawnedPawn);
	++SpawnedEnemyCount;
	SpawnedPawn->ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::RefreshObservedProductionEnemies()
{
	if (!HasAuthority() || !bObservingProductionRift || !GetWorld())
	{
		return;
	}

	for (TActorIterator<AEnemyParentNative> It(GetWorld()); It; ++It)
	{
		AEnemyParentNative* Enemy = *It;
		if (!IsValid(Enemy) || !Enemy->IsAlive(AeyerjiTags::State_Dead) || SpawnedEnemies.Contains(Enemy))
		{
			continue;
		}

		SpawnedEnemies.Add(Enemy);
		Enemy->OnEnemyDied.RemoveDynamic(this, &ThisClass::HandleEnemyDied);
		Enemy->OnEnemyDied.AddDynamic(this, &ThisClass::HandleEnemyDied);
		++SpawnedEnemyCount;
	}
}

void AAeyerjiCombatBalanceTestHarness::RefreshObservedProductionPlayer()
{
	if (!HasAuthority() || !bObservingProductionRift || !IsValid(TestedController))
	{
		return;
	}

	APawn* CurrentPawn = TestedController->GetPawn();
	if (!IsValid(CurrentPawn) || CurrentPawn->IsA<ASpectatorPawn>() || CurrentPawn == TestedPawn)
	{
		return;
	}

	UnbindPlayerHealth();
	UnbindPlayerInventory();
	TestedPawn = CurrentPawn;
	BindPlayerHealth();
	BindPlayerInventory();
	if (!TestedAbilitySystem.IsValid())
	{
		return;
	}

	PlayerHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	PlayerMaxHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	PlayerAttackDamage = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
	UpdateProgressionSnapshot(true);
	if (bAwaitingPlayerRespawn)
	{
		++PlayerRespawnCount;
		bAwaitingPlayerRespawn = false;
		EmitMarker(FString::Printf(TEXT("PlayerRespawn_%d"), PlayerRespawnCount));
	}
	ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::FinishSpawning()
{
	if (!HasAuthority() || TestState != EAeyerjiCombatTestState::Spawning)
	{
		return;
	}

	TestState = EAeyerjiCombatTestState::Ready;
	ReadyWorldTime = GetWorld()->GetTimeSeconds();
	UpdateAuthoritativeSnapshot(false);
	EmitMarker(TEXT("Ready"));
	ForceNetUpdate();

	if (ActiveRequest.AutoEngageDelay <= 0.f && ActiveRequest.AutoEngageDelay >= 0.f)
	{
		FString Ignored;
		Engage(Ignored);
	}
}

bool AAeyerjiCombatBalanceTestHarness::Engage(FString& OutMessage)
{
#if UE_BUILD_SHIPPING
	OutMessage = TEXT("Combat-balance test commands are unavailable in Shipping builds.");
	return false;
#else
	if (!HasAuthority())
	{
		OutMessage = TEXT("Only authority can engage a combat test.");
		return false;
	}

	if (TestState == EAeyerjiCombatTestState::Spawning)
	{
		OutMessage = FString::Printf(
			TEXT("Combat test is still assembling (%d/%d attempts complete)."),
			NextSpawnIndex,
			RequestedEnemyCount);
		return false;
	}
	if (TestState == EAeyerjiCombatTestState::Running)
	{
		OutMessage = TEXT("Combat test is already running.");
		return true;
	}
	if (TestState == EAeyerjiCombatTestState::Completed)
	{
		OutMessage = TEXT("Combat test has completed; start a new preset to run it again.");
		return false;
	}

	ResetBombardmentTelemetry();
	ResetTargetingTelemetry();
	DamageEvents.Reset();
	HealingEvents.Reset();
	PopulationSamples.Reset();
	HitchEvents.Reset();
	KillRewardEvents.Reset();
	ProgressionEvents.Reset();
	ItemEvents.Reset();
	EnemyKills = 0;
	PlayerKillsPerTenSeconds = 0.f;
	ThreateningArrivalCount = 0;
	ThreateningArrivalsPerSecond = 0.f;
	ThreateningEnemyCount = 0;
	UniqueHittersOneSecond = 0;
	ArrivedThreateningEnemies.Reset();
	DamageByArchetype.Reset();
	HealingBySource.Reset();
	LastObservedEnemyLevels.Reset();
	TotalIncomingDamage = 0.f;
	AverageIncomingDPS = 0.f;
	TotalHealing = 0.f;
	AverageHealingPerSecond = 0.f;
	PlayerDeathCount = 0;
	PlayerRespawnCount = 0;
	ItemsAddedDuringRun = 0;
	ItemsEquippedDuringRun = 0;
	PeakDamageHalfSecond = 0.f;
	PeakDamageOneSecond = 0.f;
	PeakDamageThreeSeconds = 0.f;
	TimeToFirstDamage = -1.f;
	TimeToFirstKill = -1.f;
	ClearTime = -1.f;
	bFirstDamageMarked = false;
	bFirstKillMarked = false;
	bReportWritten = false;
	bAwaitingPlayerRespawn = false;
	bHasProgressionSnapshot = false;
	LastPlayerDeathElapsedTime = -1.f;
	LatestAuthorityFrameDeltaMs = 0.f;
	LastPopulationSampleElapsedSeconds = -1.f;
	MaximumAuthorityFrameDeltaMs = 0.f;
	TotalAuthorityHitchOverageMs = 0.f;
	AuthorityHitchCount = 0;
	ReportBasePath.Reset();
	CompletionReason = NAME_None;
	TestState = EAeyerjiCombatTestState::Running;
	CombatStartWorldTime = GetWorld()->GetTimeSeconds();
	NextSampleWorldTime = CombatStartWorldTime;
	ElapsedCombatSeconds = 0.f;
	if (TestedAbilitySystem.IsValid())
	{
		PlayerHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	}
	LowestPlayerHealth = PlayerHealth;

	// Place the trace boundary before release because CompleteEncounterReveal can synchronously
	// resume an already-prepared attack and apply damage in this same frame.
	EmitMarker(TEXT("Engage"));
	for (APawn* SpawnedPawn : SpawnedEnemies)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
		{
			Enemy->CompleteEncounterReveal();
		}
	}

	UpdateAuthoritativeSnapshot(true);
	ForceNetUpdate();
	OutMessage = FString::Printf(
		TEXT("Engaged %s with %d/%d enemies. Rewind/trace marker: AJCombatTest/Engage."),
		*ScenarioName.ToString(),
		SpawnedEnemyCount,
		RequestedEnemyCount);
	return true;
#endif
}

void AAeyerjiCombatBalanceTestHarness::RecordAuthorityFrame(const float DeltaSeconds)
{
	LatestAuthorityFrameDeltaMs = FMath::IsFinite(DeltaSeconds)
		? FMath::Max(0.f, DeltaSeconds) * 1000.f
		: 0.f;
	MaximumAuthorityFrameDeltaMs = FMath::Max(MaximumAuthorityFrameDeltaMs, LatestAuthorityFrameDeltaMs);

	const float OverageMs = CalculateHitchOverageMilliseconds(DeltaSeconds, AuthorityHitchThresholdSeconds);
	if (OverageMs <= 0.f)
	{
		return;
	}

	++AuthorityHitchCount;
	TotalAuthorityHitchOverageMs += OverageMs;
	if (HitchEvents.Num() >= MaximumStoredHitchEvents)
	{
		return;
	}

	FHitchEvent& Event = HitchEvents.AddDefaulted_GetRef();
	Event.TimeSeconds = GetWorld() && CombatStartWorldTime > 0.0
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: ElapsedCombatSeconds;
	Event.AuthorityFrameDeltaMs = LatestAuthorityFrameDeltaMs;
	Event.ThresholdOverageMs = OverageMs;
	Event.Alive = AliveEnemyCount;
	Event.CombatActive = CombatActiveEnemyCount;
	Event.Threatening = ThreateningEnemyCount;
	Event.Kills = EnemyKills;
	Event.PlayerLevel = CurrentPlayerLevel;
	Event.InventoryItems = InventoryItemCount;
	Event.EquippedItems = EquippedItemCount;
}

void AAeyerjiCombatBalanceTestHarness::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocalHUD(DeltaSeconds);

	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	const double WorldTime = GetWorld()->GetTimeSeconds();
	if (TestState == EAeyerjiCombatTestState::Running)
	{
		RecordAuthorityFrame(DeltaSeconds);
	}
	if (TestState == EAeyerjiCombatTestState::Spawning)
	{
		int32 SpawnAttemptsThisTick = 0;
		while (NextSpawnIndex < PendingEnemySets.Num()
			&& WorldTime + UE_DOUBLE_SMALL_NUMBER >= NextSpawnWorldTime
			&& SpawnAttemptsThisTick < MaximumTestEnemyCount)
		{
			SpawnNextEnemy();
			++NextSpawnIndex;
			++SpawnAttemptsThisTick;
			NextSpawnWorldTime += static_cast<double>(ActiveRequest.SpawnInterval);
			if (ActiveRequest.SpawnInterval > 0.f)
			{
				break;
			}
		}

		if (NextSpawnIndex >= PendingEnemySets.Num())
		{
			FinishSpawning();
		}
	}
	else if (TestState == EAeyerjiCombatTestState::Ready
		&& ActiveRequest.AutoEngageDelay >= 0.f
		&& WorldTime >= ReadyWorldTime + static_cast<double>(ActiveRequest.AutoEngageDelay))
	{
		FString Ignored;
		Engage(Ignored);
	}
	else if (TestState == EAeyerjiCombatTestState::Running
		&& WorldTime >= NextSampleWorldTime)
	{
		NextSampleWorldTime = WorldTime + PopulationSampleIntervalSeconds;
		RefreshObservedProductionPlayer();
		RefreshObservedProductionEnemies();
		UpdateAuthoritativeSnapshot(true);
	}
}

void AAeyerjiCombatBalanceTestHarness::UpdateAuthoritativeSnapshot(const bool bStoreSample)
{
	if (!HasAuthority())
	{
		return;
	}

	AliveEnemyCount = 0;
	CombatActiveEnemyCount = 0;
	EnemiesWithin1000 = 0;
	EnemiesWithin2000 = 0;
	EnemiesWithin4000 = 0;
	EnemiesWithin8000 = 0;
	ThreateningEnemyCount = 0;
	ActiveEnemyLevelMin = 0;
	ActiveEnemyLevelMax = 0;
	ActiveEnemyLevelAverage = 0.f;
	int32 ActiveEnemyLevelTotal = 0;
	int32 ActiveEnemyLevelCount = 0;
	ActiveEnemySpawnLevelMin = 0;
	ActiveEnemySpawnLevelMax = 0;
	ActiveEnemySpawnLevelAverage = 0.f;
	int32 ActiveEnemySpawnLevelTotal = 0;
	const FVector PlayerLocation = IsValid(TestedPawn) ? TestedPawn->GetActorLocation() : TestCenter;

	for (APawn* SpawnedPawn : SpawnedEnemies)
	{
		const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn);
		if (!IsValid(Enemy))
		{
			continue;
		}
		if (!Enemy->IsAlive(AeyerjiTags::State_Dead))
		{
			if (bObservingProductionRift)
			{
				ArrivedThreateningEnemies.Remove(SpawnedPawn);
			}
			continue;
		}

		const bool bCombatActive = Enemy->IsEncounterCombatActive();
		if (bObservingProductionRift && !bCombatActive)
		{
			// A pooled pawn may be reused by a later wave. Clearing its arrival state here
			// makes the next checkout a new pressure arrival without counting range parking.
			ArrivedThreateningEnemies.Remove(SpawnedPawn);
			continue;
		}

		++AliveEnemyCount;
		if (bCombatActive)
		{
			++CombatActiveEnemyCount;
			const int32 LiveLevel = ResolveLiveEnemyLevel(Enemy);
			const int32 SpawnLevel = FMath::Max(1, Enemy->GetScaledLevel());
			ActiveEnemyLevelMin = ActiveEnemyLevelCount == 0 ? LiveLevel : FMath::Min(ActiveEnemyLevelMin, LiveLevel);
			ActiveEnemyLevelMax = FMath::Max(ActiveEnemyLevelMax, LiveLevel);
			ActiveEnemyLevelTotal += LiveLevel;
			ActiveEnemySpawnLevelMin = ActiveEnemyLevelCount == 0 ? SpawnLevel : FMath::Min(ActiveEnemySpawnLevelMin, SpawnLevel);
			ActiveEnemySpawnLevelMax = FMath::Max(ActiveEnemySpawnLevelMax, SpawnLevel);
			ActiveEnemySpawnLevelTotal += SpawnLevel;

			const TWeakObjectPtr<AEnemyParentNative> EnemyKey(const_cast<AEnemyParentNative*>(Enemy));
			if (int32* PreviousLevel = LastObservedEnemyLevels.Find(EnemyKey))
			{
				if (*PreviousLevel != LiveLevel && TestState == EAeyerjiCombatTestState::Running)
				{
					const float SincePlayerDeath = LastPlayerDeathElapsedTime >= 0.f
						? FMath::Max(0.f, ElapsedCombatSeconds - LastPlayerDeathElapsedTime)
						: -1.f;
					EmitMarker(FString::Printf(
						TEXT("EnemyLevelChanged Enemy=%s Old=%d New=%d SpawnLevel=%d PlayerDeaths=%d SincePlayerDeath=%.3f"),
						*GetNameSafe(Enemy), *PreviousLevel, LiveLevel, SpawnLevel, PlayerDeathCount, SincePlayerDeath));
				}
				*PreviousLevel = LiveLevel;
			}
			else
			{
				LastObservedEnemyLevels.Add(EnemyKey, LiveLevel);
			}
			++ActiveEnemyLevelCount;
		}

		const float Distance = FVector::Dist2D(PlayerLocation, Enemy->GetActorLocation());
		EnemiesWithin1000 += Distance <= 1000.f ? 1 : 0;
		EnemiesWithin2000 += Distance <= 2000.f ? 1 : 0;
		EnemiesWithin4000 += Distance <= 4000.f ? 1 : 0;
		EnemiesWithin8000 += Distance <= 8000.f ? 1 : 0;
		if (bCombatActive && Distance <= 2000.f)
		{
			++ThreateningEnemyCount;
			if (!ArrivedThreateningEnemies.Contains(SpawnedPawn))
			{
				ArrivedThreateningEnemies.Add(SpawnedPawn);
				++ThreateningArrivalCount;
			}
		}
	}
	ActiveEnemyLevelAverage = ActiveEnemyLevelCount > 0
		? static_cast<float>(ActiveEnemyLevelTotal) / static_cast<float>(ActiveEnemyLevelCount)
		: 0.f;
	ActiveEnemySpawnLevelAverage = ActiveEnemyLevelCount > 0
		? static_cast<float>(ActiveEnemySpawnLevelTotal) / static_cast<float>(ActiveEnemyLevelCount)
		: 0.f;
	UpdateProgressionSnapshot(TestState == EAeyerjiCombatTestState::Running);

	if (TestedAbilitySystem.IsValid())
	{
		PlayerHealth = TestedAbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
		LowestPlayerHealth = FMath::Min(LowestPlayerHealth, PlayerHealth);
	}

	if (TestState == EAeyerjiCombatTestState::Running && GetWorld())
	{
		ElapsedCombatSeconds = FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime));
		AverageIncomingDPS = ElapsedCombatSeconds > UE_SMALL_NUMBER
			? TotalIncomingDamage / ElapsedCombatSeconds
			: 0.f;
		AverageHealingPerSecond = ElapsedCombatSeconds > UE_SMALL_NUMBER
			? TotalHealing / ElapsedCombatSeconds
			: 0.f;
		PlayerKillsPerTenSeconds = CalculateCountRate(EnemyKills, ElapsedCombatSeconds) * 10.f;
		ThreateningArrivalsPerSecond = CalculateCountRate(ThreateningArrivalCount, ElapsedCombatSeconds);

		TSet<TWeakObjectPtr<AActor>> RecentHitters;
		const float EarliestRecentHit = ElapsedCombatSeconds - 1.f;
		for (int32 Index = DamageEvents.Num() - 1; Index >= 0; --Index)
		{
			const FDamageEvent& Event = DamageEvents[Index];
			if (Event.TimeSeconds + UE_SMALL_NUMBER < EarliestRecentHit)
			{
				break;
			}
			if (Event.SourceActor.IsValid())
			{
				RecentHitters.Add(Event.SourceActor);
			}
		}
		UniqueHittersOneSecond = RecentHitters.Num();
	}

	if (bStoreSample && PopulationSamples.Num() < MaximumStoredPopulationSamples)
	{
		FPopulationSample& Sample = PopulationSamples.AddDefaulted_GetRef();
		Sample.TimeSeconds = ElapsedCombatSeconds;
		Sample.Alive = AliveEnemyCount;
		Sample.CombatActive = CombatActiveEnemyCount;
		Sample.Within1000 = EnemiesWithin1000;
		Sample.Within2000 = EnemiesWithin2000;
		Sample.Within4000 = EnemiesWithin4000;
		Sample.Within8000 = EnemiesWithin8000;
		Sample.Threatening = ThreateningEnemyCount;
		Sample.UniqueHittersOneSecond = UniqueHittersOneSecond;
		Sample.ThreateningArrivals = ThreateningArrivalCount;
		Sample.PlayerHP = PlayerHealth;
		Sample.TotalDamage = TotalIncomingDamage;
		Sample.TotalHealing = TotalHealing;
		Sample.Kills = EnemyKills;
		Sample.PlayerDeaths = PlayerDeathCount;
		Sample.PlayerLevel = CurrentPlayerLevel;
		Sample.PlayerXP = CurrentPlayerXP;
		Sample.PlayerXPMax = CurrentPlayerXPMax;
		Sample.PlayerHPMax = PlayerMaxHealth;
		Sample.PlayerAttackDamage = PlayerAttackDamage;
		Sample.PlayerArmor = CurrentPlayerArmor;
		Sample.PlayerDodgeChance = CurrentPlayerDodgeChance;
		Sample.PlayerHPRegen = CurrentPlayerHPRegen;
		Sample.PlayerRunSpeed = CurrentPlayerRunSpeed;
		Sample.EnemyLevelMin = ActiveEnemyLevelMin;
		Sample.EnemyLevelMax = ActiveEnemyLevelMax;
		Sample.EnemyLevelAverage = ActiveEnemyLevelAverage;
		Sample.EnemySpawnLevelMin = ActiveEnemySpawnLevelMin;
		Sample.EnemySpawnLevelMax = ActiveEnemySpawnLevelMax;
		Sample.EnemySpawnLevelAverage = ActiveEnemySpawnLevelAverage;
		Sample.InventoryItems = InventoryItemCount;
		Sample.EquippedItems = EquippedItemCount;
		Sample.AuthorityFrameDeltaMs = LatestAuthorityFrameDeltaMs;
		Sample.BombardmentActiveZones = ActiveBombardments.Num();
		Sample.BombardmentPeakActiveZones = BombardmentPeakActiveZones;
		Sample.BombardmentStarts = BombardmentStarts;
		Sample.BombardmentImpacts = BombardmentImpacts;
		Sample.BombardmentCancellations = BombardmentCancellations;
		Sample.BombardmentDamageApplications = BombardmentDamageApplications;
		Sample.SampleIntervalMs = LastPopulationSampleElapsedSeconds >= 0.f
			? FMath::Max(0.f, ElapsedCombatSeconds - LastPopulationSampleElapsedSeconds) * 1000.f
			: 0.f;
		LastPopulationSampleElapsedSeconds = ElapsedCombatSeconds;
	}

	UE_VLOG(this, LogAeyerjiCombatTest, Verbose,
		TEXT("Scenario=%s State=%s Time=%.2f Alive=%d Active=%d Near1k=%d Near2k=%d Near4k=%d HP=%.1f Damage=%.1f DPS=%.1f Kills=%d"),
		*ScenarioName.ToString(),
		*GetStateString(),
		ElapsedCombatSeconds,
		AliveEnemyCount,
		CombatActiveEnemyCount,
		EnemiesWithin1000,
		EnemiesWithin2000,
		EnemiesWithin4000,
		PlayerHealth,
		TotalIncomingDamage,
		AverageIncomingDPS,
		EnemyKills);

	ForceNetUpdate();
	if (TestState == EAeyerjiCombatTestState::Running)
	{
		if (!bObservingProductionRift && PlayerHealth <= 0.f)
		{
			CompleteTest(TEXT("PlayerDead"));
		}
		else if (!bObservingProductionRift && NextSpawnIndex >= RequestedEnemyCount && AliveEnemyCount <= 0)
		{
			CompleteTest(TEXT("Cleared"));
		}
	}
}

void AAeyerjiCombatBalanceTestHarness::BindPlayerHealth()
{
	UnbindPlayerHealth();
	if (!IsValid(TestedPawn))
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TestedPawn, /*LookForComponent=*/true);
	if (!AbilitySystem)
	{
		return;
	}

	TestedAbilitySystem = AbilitySystem;
	HealthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPAttribute())
		.AddUObject(this, &AAeyerjiCombatBalanceTestHarness::HandlePlayerHealthChanged);
	UAeyerjiAttributeSet* AttributeSet = const_cast<UAeyerjiAttributeSet*>(AbilitySystem->GetSet<UAeyerjiAttributeSet>());
	if (AttributeSet)
	{
		AttributeSet->OnDamageTaken.RemoveDynamic(this, &ThisClass::HandlePlayerDamageTaken);
		AttributeSet->OnDamageTaken.AddDynamic(this, &ThisClass::HandlePlayerDamageTaken);
		AttributeSet->OnHealingReceived.RemoveDynamic(this, &ThisClass::HandlePlayerHealingReceived);
		AttributeSet->OnHealingReceived.AddDynamic(this, &ThisClass::HandlePlayerHealingReceived);
		TestedAttributeSet = AttributeSet;
	}
}

void AAeyerjiCombatBalanceTestHarness::UnbindPlayerHealth()
{
	if (UAbilitySystemComponent* AbilitySystem = TestedAbilitySystem.Get(); AbilitySystem && HealthChangedHandle.IsValid())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPAttribute())
			.Remove(HealthChangedHandle);
	}
	HealthChangedHandle.Reset();
	if (UAeyerjiAttributeSet* AttributeSet = TestedAttributeSet.Get())
	{
		AttributeSet->OnDamageTaken.RemoveDynamic(this, &ThisClass::HandlePlayerDamageTaken);
		AttributeSet->OnHealingReceived.RemoveDynamic(this, &ThisClass::HandlePlayerHealingReceived);
	}
	TestedAttributeSet.Reset();
	TestedAbilitySystem.Reset();
}

void AAeyerjiCombatBalanceTestHarness::BindPlayerInventory()
{
	UnbindPlayerInventory();
	if (!IsValid(TestedPawn))
	{
		return;
	}

	UAeyerjiInventoryComponent* Inventory = nullptr;
	if (APlayerParentNative* Player = Cast<APlayerParentNative>(TestedPawn))
	{
		Inventory = Player->GetInventoryComponent();
	}
	if (!Inventory)
	{
		Inventory = TestedPawn->FindComponentByClass<UAeyerjiInventoryComponent>();
	}
	if (!Inventory)
	{
		return;
	}

	Inventory->OnInventoryItemStateChanged.RemoveDynamic(this, &ThisClass::HandleInventoryItemStateChanged);
	Inventory->OnInventoryItemStateChanged.AddDynamic(this, &ThisClass::HandleInventoryItemStateChanged);
	TestedInventory = Inventory;
	InventoryItemCount = Inventory->Items.Num();
	EquippedItemCount = Inventory->EquippedItems.Num();
}

void AAeyerjiCombatBalanceTestHarness::UnbindPlayerInventory()
{
	if (UAeyerjiInventoryComponent* Inventory = TestedInventory.Get())
	{
		Inventory->OnInventoryItemStateChanged.RemoveDynamic(this, &ThisClass::HandleInventoryItemStateChanged);
	}
	TestedInventory.Reset();
}

void AAeyerjiCombatBalanceTestHarness::UpdateProgressionSnapshot(const bool bRecordChanges)
{
	UAbilitySystemComponent* AbilitySystem = TestedAbilitySystem.Get();
	if (!AbilitySystem)
	{
		return;
	}

	const int32 NewLevel = UAeyerjiDifficultySettings::FloatToGameplayLevel(
		AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()));
	const float NewXP = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetXPAttribute());
	const float NewXPMax = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetXPMaxAttribute());
	const float NewHPMax = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	const float NewAttackDamage = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
	const float NewArmor = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetArmorAttribute());
	const float NewDodgeChance = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetDodgeChanceAttribute());
	const float NewHPRegen = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetHPRegenAttribute());
	const float NewRunSpeed = AbilitySystem->GetNumericAttribute(UAeyerjiAttributeSet::GetRunSpeedAttribute());
	if (UAeyerjiInventoryComponent* Inventory = TestedInventory.Get())
	{
		InventoryItemCount = Inventory->Items.Num();
		EquippedItemCount = Inventory->EquippedItems.Num();
	}

	const bool bLevelChanged = bHasProgressionSnapshot && NewLevel != LastProgressionLevel;
	const bool bXPChanged = bHasProgressionSnapshot
		&& (!FMath::IsNearlyEqual(NewXP, LastProgressionXP) || !FMath::IsNearlyEqual(NewXPMax, LastProgressionXPMax));
	const bool bStatsChanged = bHasProgressionSnapshot
		&& (!FMath::IsNearlyEqual(NewHPMax, LastProgressionHPMax)
			|| !FMath::IsNearlyEqual(NewAttackDamage, LastProgressionAttackDamage)
			|| !FMath::IsNearlyEqual(NewArmor, LastProgressionArmor)
			|| !FMath::IsNearlyEqual(NewDodgeChance, LastProgressionDodgeChance)
			|| !FMath::IsNearlyEqual(NewHPRegen, LastProgressionHPRegen)
			|| !FMath::IsNearlyEqual(NewRunSpeed, LastProgressionRunSpeed));

	CurrentPlayerLevel = NewLevel;
	CurrentPlayerXP = NewXP;
	CurrentPlayerXPMax = NewXPMax;
	PlayerMaxHealth = NewHPMax;
	PlayerAttackDamage = NewAttackDamage;
	CurrentPlayerArmor = NewArmor;
	CurrentPlayerDodgeChance = NewDodgeChance;
	CurrentPlayerHPRegen = NewHPRegen;
	CurrentPlayerRunSpeed = NewRunSpeed;

	if (!bHasProgressionSnapshot)
	{
		bHasProgressionSnapshot = true;
		if (bRecordChanges)
		{
			RecordProgressionEvent(TEXT("Initial"));
		}
	}
	else if (bRecordChanges)
	{
		if (bLevelChanged)
		{
			EmitMarker(FString::Printf(TEXT("LevelChanged_%d_to_%d"), LastProgressionLevel, NewLevel));
			RecordProgressionEvent(TEXT("LevelChanged"));
		}
		else if (bXPChanged)
		{
			RecordProgressionEvent(TEXT("XPChanged"));
		}
		if (bStatsChanged)
		{
			RecordProgressionEvent(TEXT("StatsChanged"));
		}
	}

	LastProgressionLevel = NewLevel;
	LastProgressionXP = NewXP;
	LastProgressionXPMax = NewXPMax;
	LastProgressionHPMax = NewHPMax;
	LastProgressionAttackDamage = NewAttackDamage;
	LastProgressionArmor = NewArmor;
	LastProgressionDodgeChance = NewDodgeChance;
	LastProgressionHPRegen = NewHPRegen;
	LastProgressionRunSpeed = NewRunSpeed;
}

void AAeyerjiCombatBalanceTestHarness::RecordProgressionEvent(const FString& Event)
{
	if (ProgressionEvents.Num() >= MaximumStoredProgressionEvents)
	{
		return;
	}
	FProgressionEvent& Entry = ProgressionEvents.AddDefaulted_GetRef();
	Entry.TimeSeconds = GetWorld()
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: ElapsedCombatSeconds;
	Entry.Event = Event;
	Entry.PlayerLevel = CurrentPlayerLevel;
	Entry.PlayerXP = CurrentPlayerXP;
	Entry.PlayerXPMax = CurrentPlayerXPMax;
	Entry.PlayerHPMax = PlayerMaxHealth;
	Entry.PlayerAttackDamage = PlayerAttackDamage;
	Entry.PlayerArmor = CurrentPlayerArmor;
	Entry.PlayerDodgeChance = CurrentPlayerDodgeChance;
	Entry.PlayerHPRegen = CurrentPlayerHPRegen;
	Entry.PlayerRunSpeed = CurrentPlayerRunSpeed;
	Entry.EnemyLevelMin = ActiveEnemyLevelMin;
	Entry.EnemyLevelMax = ActiveEnemyLevelMax;
	Entry.EnemyLevelAverage = ActiveEnemyLevelAverage;
	Entry.EnemySpawnLevelMin = ActiveEnemySpawnLevelMin;
	Entry.EnemySpawnLevelMax = ActiveEnemySpawnLevelMax;
	Entry.EnemySpawnLevelAverage = ActiveEnemySpawnLevelAverage;
	Entry.Kills = EnemyKills;
	Entry.InventoryItems = InventoryItemCount;
	Entry.EquippedItems = EquippedItemCount;
}

void AAeyerjiCombatBalanceTestHarness::HandlePlayerHealthChanged(const FOnAttributeChangeData& Data)
{
	PlayerHealth = Data.NewValue;
	LowestPlayerHealth = FMath::Min(LowestPlayerHealth, Data.NewValue);
	if (HasAuthority()
		&& bObservingProductionRift
		&& TestState == EAeyerjiCombatTestState::Running
		&& Data.OldValue > UE_SMALL_NUMBER
		&& Data.NewValue <= UE_SMALL_NUMBER)
	{
		++PlayerDeathCount;
		bAwaitingPlayerRespawn = true;
		LastPlayerDeathElapsedTime = ElapsedCombatSeconds;
		EmitMarker(FString::Printf(TEXT("PlayerDeath_%d"), PlayerDeathCount));
		ForceNetUpdate();
	}
}

void AAeyerjiCombatBalanceTestHarness::HandlePlayerDamageTaken(
	AActor* Victim,
	AActor* DamageInstigator,
	const float DamageTaken,
	const FGameplayTag DamageType)
{
	if (!HasAuthority()
		|| TestState != EAeyerjiCombatTestState::Running
		|| Victim != TestedPawn
		|| !FMath::IsFinite(DamageTaken)
		|| DamageTaken <= UE_SMALL_NUMBER)
	{
		return;
	}

	TotalIncomingDamage += DamageTaken;
	if (DamageEvents.Num() < MaximumStoredDamageEvents)
	{
		FDamageEvent& Event = DamageEvents.AddDefaulted_GetRef();
		Event.TimeSeconds = GetWorld()
			? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
			: ElapsedCombatSeconds;
		Event.Amount = DamageTaken;
		Event.SourceActor = DamageInstigator;
		Event.Source = IsValid(DamageInstigator)
			? FString::Printf(TEXT("%s:%s"), *GetNameSafe(DamageInstigator->GetClass()), *GetNameSafe(DamageInstigator))
			: TEXT("Unknown");
		Event.Archetype = ResolveDamageArchetype(DamageInstigator);
		Event.DamageType = DamageType.IsValid() ? DamageType.ToString() : TEXT("Unknown");
		DamageByArchetype.FindOrAdd(Event.Archetype) += DamageTaken;
		PeakDamageHalfSecond = FMath::Max(PeakDamageHalfSecond, CalculateRollingDamage(0.5f, Event.TimeSeconds));
		PeakDamageOneSecond = FMath::Max(PeakDamageOneSecond, CalculateRollingDamage(1.f, Event.TimeSeconds));
		PeakDamageThreeSeconds = FMath::Max(PeakDamageThreeSeconds, CalculateRollingDamage(3.f, Event.TimeSeconds));
		if (!bFirstDamageMarked)
		{
			bFirstDamageMarked = true;
			TimeToFirstDamage = Event.TimeSeconds;
			EmitMarker(TEXT("FirstDamage"));
		}
	}

	ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::HandlePlayerHealingReceived(
	AActor* Recipient,
	AActor* HealingInstigator,
	const float HealingReceived,
	const FName HealingSourceType,
	UObject* SourceObject)
{
	if (!HasAuthority()
		|| TestState != EAeyerjiCombatTestState::Running
		|| Recipient != TestedPawn
		|| !FMath::IsFinite(HealingReceived)
		|| HealingReceived <= UE_SMALL_NUMBER)
	{
		return;
	}

	TotalHealing += HealingReceived;
	const FString SourceType = HealingSourceType.IsNone()
		? UAeyerjiAttributeSet::HealingSourceGameplayEffect.ToString()
		: HealingSourceType.ToString();
	HealingBySource.FindOrAdd(SourceType) += HealingReceived;

	if (HealingEvents.Num() < MaximumStoredHealingEvents)
	{
		FHealingEvent& Event = HealingEvents.AddDefaulted_GetRef();
		Event.TimeSeconds = GetWorld()
			? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
			: ElapsedCombatSeconds;
		Event.Amount = HealingReceived;
		Event.SourceType = SourceType;
		Event.Instigator = IsValid(HealingInstigator)
			? FString::Printf(TEXT("%s:%s"), *GetNameSafe(HealingInstigator->GetClass()), *GetNameSafe(HealingInstigator))
			: TEXT("Unknown");
		Event.SourceObject = IsValid(SourceObject)
			? FString::Printf(TEXT("%s:%s"), *GetNameSafe(SourceObject->GetClass()), *GetNameSafe(SourceObject))
			: TEXT("Unknown");
	}

	ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::HandleInventoryItemStateChanged(const FInventoryItemChangeEvent& EventData)
{
	if (!HasAuthority() || TestState != EAeyerjiCombatTestState::Running || ItemEvents.Num() >= MaximumStoredItemEvents)
	{
		return;
	}

	UpdateProgressionSnapshot(true);
	const UEnum* ChangeEnum = StaticEnum<EInventoryItemStateChange>();
	const UEnum* RarityEnum = StaticEnum<EItemRarity>();
	const UEnum* SlotEnum = StaticEnum<EEquipmentSlot>();
	const FString Change = ChangeEnum
		? ChangeEnum->GetNameStringByValue(static_cast<int64>(EventData.Change))
		: TEXT("Unknown");
	const UAeyerjiItemInstance* Item = EventData.Item;
	const FString Definition = Item && Item->Definition
		? Item->Definition->GetDefinitionKey().ToString()
		: TEXT("Unknown");

	FItemEvent& Entry = ItemEvents.AddDefaulted_GetRef();
	Entry.TimeSeconds = GetWorld()
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: ElapsedCombatSeconds;
	Entry.Change = Change;
	Entry.Definition = Definition;
	Entry.ItemLevel = Item ? Item->ItemLevel : 0;
	Entry.Rarity = Item && RarityEnum
		? RarityEnum->GetNameStringByValue(static_cast<int64>(Item->Rarity))
		: TEXT("Unknown");
	Entry.Slot = SlotEnum
		? SlotEnum->GetNameStringByValue(static_cast<int64>(EventData.Slot))
		: TEXT("Unknown");
	Entry.SlotIndex = EventData.SlotIndex;
	Entry.PlayerLevel = CurrentPlayerLevel;

	ItemsAddedDuringRun += EventData.Change == EInventoryItemStateChange::Added ? 1 : 0;
	ItemsEquippedDuringRun += EventData.Change == EInventoryItemStateChange::Equipped ? 1 : 0;
	EmitMarker(FString::Printf(TEXT("Item_%s_%s_L%d"), *Change, *Definition, Entry.ItemLevel));
	ForceNetUpdate();
}

FString AAeyerjiCombatBalanceTestHarness::ResolveDamageArchetype(const AActor* SourceActor) const
{
	if (!IsValid(SourceActor))
	{
		return TEXT("Unknown");
	}

	const APawn* SourcePawn = Cast<APawn>(SourceActor);
	if (!SourcePawn)
	{
		SourcePawn = SourceActor->GetInstigator();
	}
	if (IsValid(SourcePawn))
	{
		if (const UAeyerjiEnemyArchetypeComponent* Archetype = SourcePawn->FindComponentByClass<UAeyerjiEnemyArchetypeComponent>())
		{
			const FGameplayTag ArchetypeTag = Archetype->GetArchetypeTag();
			if (ArchetypeTag.IsValid())
			{
				return ArchetypeTag.ToString();
			}
		}
		return GetNameSafe(SourcePawn->GetClass());
	}
	return GetNameSafe(SourceActor->GetClass());
}

float AAeyerjiCombatBalanceTestHarness::CalculateRollingDamage(
	const float WindowSeconds,
	const float EventTime) const
{
	float DamageInWindow = 0.f;
	const float EarliestTime = EventTime - WindowSeconds;
	for (int32 Index = DamageEvents.Num() - 1; Index >= 0; --Index)
	{
		const FDamageEvent& Event = DamageEvents[Index];
		if (Event.TimeSeconds + UE_SMALL_NUMBER < EarliestTime)
		{
			break;
		}
		DamageInWindow += Event.Amount;
	}
	return DamageInWindow;
}

int32 AAeyerjiCombatBalanceTestHarness::FindRecentKillRewardEvent(
	const AActor* EnemyActor,
	const float EventTime) const
{
	if (!IsValid(EnemyActor))
	{
		return INDEX_NONE;
	}

	for (int32 Index = KillRewardEvents.Num() - 1; Index >= 0; --Index)
	{
		const FKillRewardEvent& Event = KillRewardEvents[Index];
		if (EventTime - Event.TimeSeconds > KillRewardMatchWindowSeconds)
		{
			break;
		}
		if (Event.EnemyActor.Get() == EnemyActor
			&& (Event.KillIndex == 0 || Event.AwardedXP <= UE_SMALL_NUMBER))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void AAeyerjiCombatBalanceTestHarness::HandleEnemyXPAwarded(const FAeyerjiEnemyXPAward& Award)
{
	if (!HasAuthority() || TestState != EAeyerjiCombatTestState::Running || !IsValid(Award.RecipientPawn))
	{
		return;
	}

	const APawn* CurrentControllerPawn = IsValid(TestedController) ? TestedController->GetPawn() : nullptr;
	if (Award.RecipientPawn != TestedPawn && Award.RecipientPawn != CurrentControllerPawn)
	{
		return;
	}

	const float EventTime = GetWorld()
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: ElapsedCombatSeconds;
	int32 EventIndex = FindRecentKillRewardEvent(Award.EnemyActor, EventTime);
	if (EventIndex == INDEX_NONE)
	{
		if (KillRewardEvents.Num() >= MaximumStoredKillRewardEvents)
		{
			return;
		}
		EventIndex = KillRewardEvents.AddDefaulted();
	}

	FKillRewardEvent& Event = KillRewardEvents[EventIndex];
	Event.TimeSeconds = EventTime;
	Event.Enemy = GetNameSafe(Award.EnemyActor);
	Event.Archetype = ResolveDamageArchetype(Award.EnemyActor);
	Event.EnemyLevel = FMath::Max(1, Award.EnemyLevel);
	Event.BaseXP = FMath::Max(0.f, Award.BaseXP);
	Event.ScaledXPBeforeRoleMultiplier = FMath::Max(0.f, Award.ScaledXPBeforeRoleMultiplier);
	Event.RoleMultiplier = FMath::Max(0.f, Award.RoleMultiplier);
	Event.KillerMultiplier = FMath::Max(0.f, Award.KillerMultiplier);
	Event.AwardedXP = FMath::Max(0.f, Award.AwardedXP);
	Event.bRecipientWasKiller = Award.bRecipientWasKiller;
	Event.EnemyActor = const_cast<AActor*>(Award.EnemyActor);

	if (const UAbilitySystemComponent* RecipientASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Award.RecipientPawn))
	{
		Event.PlayerLevelAfter = UAeyerjiDifficultySettings::FloatToGameplayLevel(
			RecipientASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()));
		Event.PlayerXPAfter = RecipientASC->GetNumericAttribute(UAeyerjiAttributeSet::GetXPAttribute());
		Event.PlayerXPMaxAfter = RecipientASC->GetNumericAttribute(UAeyerjiAttributeSet::GetXPMaxAttribute());
	}
}

void AAeyerjiCombatBalanceTestHarness::HandleEnemyDied(AActor* DeadEnemy)
{
	if (!HasAuthority() || TestState != EAeyerjiCombatTestState::Running)
	{
		return;
	}

	if (bObservingProductionRift)
	{
		ArrivedThreateningEnemies.Remove(Cast<APawn>(DeadEnemy));
	}
	++EnemyKills;
	const float EventTime = GetWorld()
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: ElapsedCombatSeconds;
	int32 EventIndex = FindRecentKillRewardEvent(DeadEnemy, EventTime);
	if (EventIndex == INDEX_NONE && KillRewardEvents.Num() < MaximumStoredKillRewardEvents)
	{
		EventIndex = KillRewardEvents.AddDefaulted();
	}
	if (KillRewardEvents.IsValidIndex(EventIndex))
	{
		FKillRewardEvent& Event = KillRewardEvents[EventIndex];
		Event.TimeSeconds = EventTime;
		Event.KillIndex = EnemyKills;
		Event.Enemy = GetNameSafe(DeadEnemy);
		Event.Archetype = ResolveDamageArchetype(DeadEnemy);
		Event.EnemyActor = DeadEnemy;
		if (const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(DeadEnemy))
		{
			Event.EnemyLevel = ResolveLiveEnemyLevel(Enemy);
		}
	}
	if (!bFirstKillMarked)
	{
		bFirstKillMarked = true;
		TimeToFirstKill = GetWorld()
			? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
			: ElapsedCombatSeconds;
		EmitMarker(TEXT("FirstKill"));
	}

	UE_VLOG(this, LogAeyerjiCombatTest, Log,
		TEXT("EnemyDied Scenario=%s Enemy=%s Kills=%d"),
		*ScenarioName.ToString(),
		*GetNameSafe(DeadEnemy),
		EnemyKills);
	ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::CompleteTest(const FName Reason)
{
	if (!HasAuthority() || TestState == EAeyerjiCombatTestState::Completed)
	{
		return;
	}

	TestState = EAeyerjiCombatTestState::Completed;
	CompletionReason = Reason;
	if (GetWorld() && CombatStartWorldTime > 0.0)
	{
		ElapsedCombatSeconds = FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime));
	}
	if (Reason == TEXT("Cleared"))
	{
		ClearTime = ElapsedCombatSeconds;
	}
	EmitMarker(Reason.ToString());
	WriteReports();
	UE_LOG(LogAeyerjiCombatTest, Display, TEXT("%s"), *BuildSummary());
	ForceNetUpdate();
}

void AAeyerjiCombatBalanceTestHarness::AddManualMarker(const FString& Label)
{
	if (!HasAuthority())
	{
		return;
	}
	EmitMarker(FString::Printf(TEXT("Manual/%s"), *SanitizeMarkerLabel(Label)));
	WriteReports();
}

void AAeyerjiCombatBalanceTestHarness::EmitMarker(const FString& Label)
{
	const FString SafeLabel = SanitizeMarkerLabel(Label);
	const float RelativeTime = CombatStartWorldTime > 0.0 && GetWorld()
		? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - CombatStartWorldTime))
		: 0.f;
	FMarkerEvent& Marker = MarkerEvents.AddDefaulted_GetRef();
	Marker.TimeSeconds = RelativeTime;
	Marker.Label = SafeLabel;

	TRACE_BOOKMARK(
		TEXT("AJCombatTest/%s Scenario=%s L=%d WT=%d Seed=%d Time=%.2f"),
		*SafeLabel,
		*ScenarioName.ToString(),
		EnemyLevel,
		WorldTier,
		TestSeed,
		RelativeTime);
	UE_LOG(LogAeyerjiCombatTest, Display,
		TEXT("[CombatTest][Marker] %s Scenario=%s Level=%d WorldTier=%d Seed=%d Time=%.2f"),
		*SafeLabel,
		*ScenarioName.ToString(),
		EnemyLevel,
		WorldTier,
		TestSeed,
		RelativeTime);
	UE_VLOG(this, LogAeyerjiCombatTest, Log,
		TEXT("Marker=%s Scenario=%s Time=%.2f"),
		*SafeLabel,
		*ScenarioName.ToString(),
		RelativeTime);
}

void AAeyerjiCombatBalanceTestHarness::WriteReports()
{
	if (!HasAuthority() || ScenarioName.IsNone())
	{
		return;
	}

	if (ReportBasePath.IsEmpty())
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CombatTests"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString BaseName = bObservingProductionRift
			? FPaths::MakeValidFileName(FString::Printf(
				TEXT("AJBR_%s_%s"),
				*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
				*ScenarioName.ToString()))
			: FPaths::MakeValidFileName(FString::Printf(
				TEXT("AJCT_%s_%s_L%02d_WT%03d_N%02d_S%d"),
				*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
				*ScenarioName.ToString(),
				EnemyLevel,
				WorldTier,
				RequestedEnemyCount,
				TestSeed));
		ReportBasePath = FPaths::Combine(Directory, BaseName);
	}

	float TotalRecordedAwardedXP = 0.f;
	float TrashRecordedAwardedXP = 0.f;
	for (const FKillRewardEvent& Event : KillRewardEvents)
	{
		TotalRecordedAwardedXP += Event.AwardedXP;
		if (Event.RoleMultiplier < 1.f - UE_SMALL_NUMBER)
		{
			TrashRecordedAwardedXP += Event.AwardedXP;
		}
	}

	FString Summary;
	Summary += BuildBombardmentSummary();
	Summary += BuildTargetingSummary();
	Summary += FString::Printf(TEXT("Scenario=%s\n"), *ScenarioName.ToString());
	Summary += FString::Printf(TEXT("Mode=%s\n"), bObservingProductionRift ? TEXT("ProductionRiftObservation") : TEXT("ControlledCombatTest"));
	Summary += FString::Printf(TEXT("Composition=%s\n"), *CompositionName.ToString());
	Summary += FString::Printf(TEXT("State=%s\n"), *GetStateString());
	Summary += FString::Printf(TEXT("CompletionReason=%s\n"), *CompletionReason.ToString());
	Summary += FString::Printf(TEXT("EnemyLevel=%d\nWorldTier=%d\nSeed=%d\n"), EnemyLevel, WorldTier, TestSeed);
	if (bObservingProductionRift)
	{
		Summary += FString::Printf(TEXT("PoolActorsObserved=%d\n"), SpawnedEnemyCount);
	}
	else
	{
		Summary += FString::Printf(TEXT("Requested=%d\nSpawned=%d\nFailedSpawns=%d\n"), RequestedEnemyCount, SpawnedEnemyCount, FailedSpawnCount);
	}
	Summary += FString::Printf(TEXT("ElapsedSeconds=%.3f\nKills=%d\nKillsPer10Seconds=%.3f\nAlive=%d\nCombatActive=%d\n"), ElapsedCombatSeconds, EnemyKills, PlayerKillsPerTenSeconds, AliveEnemyCount, CombatActiveEnemyCount);
	Summary += FString::Printf(TEXT("PlayerDeaths=%d\nPlayerRespawns=%d\n"), PlayerDeathCount, PlayerRespawnCount);
	Summary += FString::Printf(TEXT("ThreateningNow=%d\nThreateningArrivals=%d\nThreateningArrivalsPerSecond=%.3f\nUniqueHitters1s=%d\n"), ThreateningEnemyCount, ThreateningArrivalCount, ThreateningArrivalsPerSecond, UniqueHittersOneSecond);
	Summary += FString::Printf(TEXT("NetPressurePerSecond=%.3f\n"), CalculateNetPressure(ThreateningArrivalsPerSecond, PlayerKillsPerTenSeconds));
	Summary += FString::Printf(TEXT("PlayerHP=%.3f\nPlayerHPMax=%.3f\nLowestPlayerHP=%.3f\nPlayerAttackDamage=%.3f\n"), PlayerHealth, PlayerMaxHealth, LowestPlayerHealth, PlayerAttackDamage);
	Summary += FString::Printf(TEXT("PlayerLevel=%d\nPlayerXP=%.3f\nPlayerXPMax=%.3f\nPlayerArmor=%.3f\nPlayerDodgeChance=%.3f\nPlayerHPRegen=%.3f\nPlayerRunSpeed=%.3f\n"), CurrentPlayerLevel, CurrentPlayerXP, CurrentPlayerXPMax, CurrentPlayerArmor, CurrentPlayerDodgeChance, CurrentPlayerHPRegen, CurrentPlayerRunSpeed);
	Summary += FString::Printf(TEXT("ActiveEnemyLevelMin=%d\nActiveEnemyLevelMax=%d\nActiveEnemyLevelAverage=%.3f\n"), ActiveEnemyLevelMin, ActiveEnemyLevelMax, ActiveEnemyLevelAverage);
	Summary += FString::Printf(TEXT("ActiveEnemySpawnLevelMin=%d\nActiveEnemySpawnLevelMax=%d\nActiveEnemySpawnLevelAverage=%.3f\n"), ActiveEnemySpawnLevelMin, ActiveEnemySpawnLevelMax, ActiveEnemySpawnLevelAverage);
	Summary += FString::Printf(TEXT("InventoryItems=%d\nEquippedItems=%d\nItemsAddedDuringRun=%d\nItemsEquippedDuringRun=%d\n"), InventoryItemCount, EquippedItemCount, ItemsAddedDuringRun, ItemsEquippedDuringRun);
	Summary += FString::Printf(TEXT("CanonicalLevel1Naked=%s\nBaseline=%s\n"), bCanonicalPlayerBaseline ? TEXT("true") : TEXT("false"), *BaselineDescription);
	Summary += FString::Printf(TEXT("TotalIncomingDamage=%.3f\nAverageIncomingDPS=%.3f\n"), TotalIncomingDamage, AverageIncomingDPS);
	Summary += FString::Printf(TEXT("TotalHealing=%.3f\nAverageHealingPerSecond=%.3f\n"), TotalHealing, AverageHealingPerSecond);
	Summary += FString::Printf(TEXT("PeakDamage0.5s=%.3f\nPeakDamage1s=%.3f\nPeakDamage3s=%.3f\n"), PeakDamageHalfSecond, PeakDamageOneSecond, PeakDamageThreeSeconds);
	Summary += FString::Printf(TEXT("TimeToFirstDamage=%.3f\nTimeToFirstKill=%.3f\nClearTime=%.3f\n"), TimeToFirstDamage, TimeToFirstKill, ClearTime);
	Summary += FString::Printf(
		TEXT("AuthorityHitchThresholdMs=%.3f\nAuthorityHitchCount=%d\nStoredHitchEvents=%d\nMaximumAuthorityFrameDeltaMs=%.3f\nTotalAuthorityHitchOverageMs=%.3f\n"),
		AuthorityHitchThresholdSeconds * 1000.f,
		AuthorityHitchCount,
		HitchEvents.Num(),
		MaximumAuthorityFrameDeltaMs,
		TotalAuthorityHitchOverageMs);
	Summary += FString::Printf(
		TEXT("KillRewardEvents=%d\nTotalRecordedAwardedXP=%.3f\nTrashRecordedAwardedXP=%.3f\n"),
		KillRewardEvents.Num(),
		TotalRecordedAwardedXP,
		TrashRecordedAwardedXP);
	Summary += FString::Printf(TEXT("Samples=%d\nDamageEvents=%d\nHealingEvents=%d\nProgressionEvents=%d\nItemEvents=%d\nMarkers=%d\n"), PopulationSamples.Num(), DamageEvents.Num(), HealingEvents.Num(), ProgressionEvents.Num(), ItemEvents.Num(), MarkerEvents.Num());
	TArray<FString> ArchetypeKeys;
	DamageByArchetype.GetKeys(ArchetypeKeys);
	ArchetypeKeys.Sort();
	for (const FString& Archetype : ArchetypeKeys)
	{
		const float Damage = DamageByArchetype.FindRef(Archetype);
		const float Percent = TotalIncomingDamage > UE_SMALL_NUMBER ? Damage * 100.f / TotalIncomingDamage : 0.f;
		Summary += FString::Printf(TEXT("DamageByArchetype.%s=%.3f (%.1f%%)\n"), *Archetype, Damage, Percent);
	}
	TArray<FString> HealingSourceKeys;
	HealingBySource.GetKeys(HealingSourceKeys);
	HealingSourceKeys.Sort();
	for (const FString& SourceType : HealingSourceKeys)
	{
		const float Healing = HealingBySource.FindRef(SourceType);
		const float Percent = TotalHealing > UE_SMALL_NUMBER ? Healing * 100.f / TotalHealing : 0.f;
		Summary += FString::Printf(TEXT("HealingBySource.%s=%.3f (%.1f%%)\n"), *SourceType, Healing, Percent);
	}

	FString SamplesCSV(TEXT("TimeSeconds,Alive,CombatActive,Within1000,Within2000,Within4000,Within8000,Threatening,UniqueHitters1s,ThreateningArrivals,PlayerHP,TotalDamage,TotalHealing,Kills,PlayerDeaths,PlayerLevel,PlayerXP,PlayerXPMax,PlayerHPMax,PlayerAttackDamage,PlayerArmor,PlayerDodgeChance,PlayerHPRegen,PlayerRunSpeed,EnemyLevelMin,EnemyLevelMax,EnemyLevelAverage,EnemySpawnLevelMin,EnemySpawnLevelMax,EnemySpawnLevelAverage,InventoryItems,EquippedItems,AuthorityFrameDeltaMs,SampleIntervalMs,BombardmentActiveZones,BombardmentPeakActiveZones,BombardmentStarts,BombardmentImpacts,BombardmentCancellations,BombardmentDamageApplications\n"));
	for (const FPopulationSample& Sample : PopulationSamples)
	{
		SamplesCSV += FString::Printf(
			TEXT("%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%d,%d"),
			Sample.TimeSeconds,
			Sample.Alive,
			Sample.CombatActive,
			Sample.Within1000,
			Sample.Within2000,
			Sample.Within4000,
			Sample.Within8000,
			Sample.Threatening,
			Sample.UniqueHittersOneSecond,
			Sample.ThreateningArrivals,
			Sample.PlayerHP,
			Sample.TotalDamage,
			Sample.TotalHealing,
			Sample.Kills,
			Sample.PlayerDeaths);
		SamplesCSV += FString::Printf(
			TEXT(",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.3f,%.3f,%d,%d,%.3f,%d,%d,%.3f,%d,%d,%.3f,%.3f"),
			Sample.PlayerLevel,
			Sample.PlayerXP,
			Sample.PlayerXPMax,
			Sample.PlayerHPMax,
			Sample.PlayerAttackDamage,
			Sample.PlayerArmor,
			Sample.PlayerDodgeChance,
			Sample.PlayerHPRegen,
			Sample.PlayerRunSpeed,
			Sample.EnemyLevelMin,
			Sample.EnemyLevelMax,
			Sample.EnemyLevelAverage,
			Sample.EnemySpawnLevelMin,
			Sample.EnemySpawnLevelMax,
			Sample.EnemySpawnLevelAverage,
			Sample.InventoryItems,
			Sample.EquippedItems,
			Sample.AuthorityFrameDeltaMs,
			Sample.SampleIntervalMs);
		SamplesCSV += FString::Printf(TEXT(",%d,%d,%d,%d,%d,%d\n"), Sample.BombardmentActiveZones,
			Sample.BombardmentPeakActiveZones, Sample.BombardmentStarts, Sample.BombardmentImpacts,
			Sample.BombardmentCancellations, Sample.BombardmentDamageApplications);
	}

	FString EventsCSV(TEXT("TimeSeconds,Damage,Source,Archetype,DamageType\n"));
	for (const FDamageEvent& Event : DamageEvents)
	{
		EventsCSV += FString::Printf(
			TEXT("%.3f,%.3f,%s,%s,%s\n"),
			Event.TimeSeconds,
			Event.Amount,
			*EscapeCSV(Event.Source),
			*EscapeCSV(Event.Archetype),
			*EscapeCSV(Event.DamageType));
	}

	FString HealingCSV(TEXT("TimeSeconds,Healing,SourceType,Instigator,SourceObject\n"));
	for (const FHealingEvent& Event : HealingEvents)
	{
		HealingCSV += FString::Printf(
			TEXT("%.3f,%.3f,%s,%s,%s\n"),
			Event.TimeSeconds,
			Event.Amount,
			*EscapeCSV(Event.SourceType),
			*EscapeCSV(Event.Instigator),
			*EscapeCSV(Event.SourceObject));
	}

	FString ProgressionCSV(TEXT("TimeSeconds,Event,PlayerLevel,PlayerXP,PlayerXPMax,PlayerHPMax,PlayerAttackDamage,PlayerArmor,PlayerDodgeChance,PlayerHPRegen,PlayerRunSpeed,EnemyLevelMin,EnemyLevelMax,EnemyLevelAverage,EnemySpawnLevelMin,EnemySpawnLevelMax,EnemySpawnLevelAverage,Kills,InventoryItems,EquippedItems\n"));
	for (const FProgressionEvent& Event : ProgressionEvents)
	{
		ProgressionCSV += FString::Printf(
			TEXT("%.3f,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.3f,%.3f,%d,%d,%.3f,%d,%d,%.3f,%d,%d,%d\n"),
			Event.TimeSeconds,
			*EscapeCSV(Event.Event),
			Event.PlayerLevel,
			Event.PlayerXP,
			Event.PlayerXPMax,
			Event.PlayerHPMax,
			Event.PlayerAttackDamage,
			Event.PlayerArmor,
			Event.PlayerDodgeChance,
			Event.PlayerHPRegen,
			Event.PlayerRunSpeed,
			Event.EnemyLevelMin,
			Event.EnemyLevelMax,
			Event.EnemyLevelAverage,
			Event.EnemySpawnLevelMin,
			Event.EnemySpawnLevelMax,
			Event.EnemySpawnLevelAverage,
			Event.Kills,
			Event.InventoryItems,
			Event.EquippedItems);
	}

	FString ItemsCSV(TEXT("TimeSeconds,Change,Definition,ItemLevel,Rarity,Slot,SlotIndex,PlayerLevel\n"));
	for (const FItemEvent& Event : ItemEvents)
	{
		ItemsCSV += FString::Printf(
			TEXT("%.3f,%s,%s,%d,%s,%s,%d,%d\n"),
			Event.TimeSeconds,
			*EscapeCSV(Event.Change),
			*EscapeCSV(Event.Definition),
			Event.ItemLevel,
			*EscapeCSV(Event.Rarity),
			*EscapeCSV(Event.Slot),
			Event.SlotIndex,
			Event.PlayerLevel);
	}

	FString MarkersCSV(TEXT("TimeSeconds,Marker\n"));
	for (const FMarkerEvent& Marker : MarkerEvents)
	{
		MarkersCSV += FString::Printf(TEXT("%.3f,%s\n"), Marker.TimeSeconds, *EscapeCSV(Marker.Label));
	}

	FString HitchesCSV(TEXT("TimeSeconds,AuthorityFrameDeltaMs,ThresholdOverageMs,Alive,CombatActive,Threatening,Kills,PlayerLevel,InventoryItems,EquippedItems\n"));
	for (const FHitchEvent& Event : HitchEvents)
	{
		HitchesCSV += FString::Printf(
			TEXT("%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d\n"),
			Event.TimeSeconds,
			Event.AuthorityFrameDeltaMs,
			Event.ThresholdOverageMs,
			Event.Alive,
			Event.CombatActive,
			Event.Threatening,
			Event.Kills,
			Event.PlayerLevel,
			Event.InventoryItems,
			Event.EquippedItems);
	}

	FString KillsCSV(TEXT("TimeSeconds,KillIndex,Enemy,Archetype,EnemyLevel,BaseXP,ScaledXPBeforeRoleMultiplier,RoleMultiplier,KillerMultiplier,AwardedXP,RecipientWasKiller,PlayerLevelAfter,PlayerXPAfter,PlayerXPMaxAfter\n"));
	for (const FKillRewardEvent& Event : KillRewardEvents)
	{
		KillsCSV += FString::Printf(
			TEXT("%.3f,%d,%s,%s,%d,%.3f,%.3f,%.5f,%.5f,%.3f,%s,%d,%.3f,%.3f\n"),
			Event.TimeSeconds,
			Event.KillIndex,
			*EscapeCSV(Event.Enemy),
			*EscapeCSV(Event.Archetype),
			Event.EnemyLevel,
			Event.BaseXP,
			Event.ScaledXPBeforeRoleMultiplier,
			Event.RoleMultiplier,
			Event.KillerMultiplier,
			Event.AwardedXP,
			Event.bRecipientWasKiller ? TEXT("true") : TEXT("false"),
			Event.PlayerLevelAfter,
			Event.PlayerXPAfter,
			Event.PlayerXPMaxAfter);
	}

	const bool bSummarySaved = FFileHelper::SaveStringToFile(Summary, *(ReportBasePath + TEXT("_summary.txt")));
	const bool bSamplesSaved = FFileHelper::SaveStringToFile(SamplesCSV, *(ReportBasePath + TEXT("_samples.csv")));
	const bool bEventsSaved = FFileHelper::SaveStringToFile(EventsCSV, *(ReportBasePath + TEXT("_damage.csv")));
	const bool bHealingSaved = FFileHelper::SaveStringToFile(HealingCSV, *(ReportBasePath + TEXT("_healing.csv")));
	const bool bProgressionSaved = FFileHelper::SaveStringToFile(ProgressionCSV, *(ReportBasePath + TEXT("_progression.csv")));
	const bool bItemsSaved = FFileHelper::SaveStringToFile(ItemsCSV, *(ReportBasePath + TEXT("_items.csv")));
	const bool bMarkersSaved = FFileHelper::SaveStringToFile(MarkersCSV, *(ReportBasePath + TEXT("_markers.csv")));
	const bool bHitchesSaved = FFileHelper::SaveStringToFile(HitchesCSV, *(ReportBasePath + TEXT("_hitches.csv")));
	const bool bKillsSaved = FFileHelper::SaveStringToFile(KillsCSV, *(ReportBasePath + TEXT("_kills.csv")));
	const bool bBombardmentsSaved = FFileHelper::SaveStringToFile(BuildBombardmentCSV(), *(ReportBasePath + TEXT("_bombardments.csv")));
	const bool bTargetingSaved = FFileHelper::SaveStringToFile(BuildTargetingCSV(), *(ReportBasePath + TEXT("_targeting.csv")));
	bReportWritten = bSummarySaved && bSamplesSaved && bEventsSaved && bHealingSaved
		&& bProgressionSaved && bItemsSaved && bMarkersSaved && bHitchesSaved && bKillsSaved
		&& bBombardmentsSaved && bTargetingSaved;
	if (!bReportWritten)
	{
		UE_LOG(LogAeyerjiCombatTest, Error, TEXT("[CombatTest] Failed to write one or more report files at %s"), *ReportBasePath);
	}
	else
	{
		UE_LOG(LogAeyerjiCombatTest, Display, TEXT("[CombatTest] Report=%s"), *ReportBasePath);
	}
}

void AAeyerjiCombatBalanceTestHarness::StopTest(const bool bCleanupSpawnedActors)
{
	if (!HasAuthority())
	{
		return;
	}

	if (TestState != EAeyerjiCombatTestState::Completed)
	{
		CompleteTest(TEXT("ManualStop"));
	}
	else
	{
		WriteReports();
	}

	if (bCleanupSpawnedActors)
	{
		CleanupSpawnedActors();
	}
}

void AAeyerjiCombatBalanceTestHarness::CleanupSpawnedActors()
{
	if (!HasAuthority() || bCleanupPerformed)
	{
		return;
	}
	bCleanupPerformed = true;
	UnbindPlayerHealth();
	UnbindPlayerInventory();

	for (APawn* SpawnedPawn : SpawnedEnemies)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
		{
			Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiCombatBalanceTestHarness::HandleEnemyDied);
		}
		if (!bObservingProductionRift && IsValid(SpawnedPawn))
		{
			SpawnedPawn->Destroy();
		}
	}
	SpawnedEnemies.Reset();

	if (IsValid(TestSpawner))
	{
		TestSpawner->ResetEncounter();
		TestSpawner->ReleaseEnemyPool(true);
		TestSpawner->ClearCombatTestScalingOverrides();
		TestSpawner->Destroy();
		TestSpawner = nullptr;
	}
}

void AAeyerjiCombatBalanceTestHarness::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EnemyXPAwardedHandle.IsValid())
	{
		UAeyerjiXPLibrary::OnEnemyXPAwarded().Remove(EnemyXPAwardedHandle);
		EnemyXPAwardedHandle.Reset();
	}
	UnbindPlayerHealth();
	UnbindPlayerInventory();
	if (HasAuthority())
	{
		// Preserve partial metrics when PIE, Standalone, or an unattended smoke process ends
		// before the player explicitly stops or completes the scenario.
		if (!ScenarioName.IsNone() && TestState != EAeyerjiCombatTestState::Completed)
		{
			CompleteTest(TEXT("SessionEnded"));
		}
		CleanupSpawnedActors();
	}
	Super::EndPlay(EndPlayReason);
}

FString AAeyerjiCombatBalanceTestHarness::GetStateString() const
{
	switch (TestState)
	{
	case EAeyerjiCombatTestState::Spawning:
		return TEXT("Spawning");
	case EAeyerjiCombatTestState::Ready:
		return TEXT("Ready");
	case EAeyerjiCombatTestState::Running:
		return TEXT("Running");
	case EAeyerjiCombatTestState::Completed:
		return TEXT("Completed");
	default:
		return TEXT("Unknown");
	}
}

FString AAeyerjiCombatBalanceTestHarness::BuildSummary() const
{
	FString Countdown;
	if (TestState == EAeyerjiCombatTestState::Ready && ActiveRequest.AutoEngageDelay >= 0.f && GetWorld())
	{
		const float Remaining = FMath::Max(
			0.f,
			static_cast<float>(ReadyWorldTime + ActiveRequest.AutoEngageDelay - GetWorld()->GetTimeSeconds()));
		Countdown = FString::Printf(TEXT(" AutoEngage=%.1fs"), Remaining);
	}
	const FString PopulationIdentity = bObservingProductionRift
		? FString::Printf(TEXT("PoolActors=%d"), SpawnedEnemyCount)
		: FString::Printf(TEXT("Spawned=%d/%d Failed=%d"), SpawnedEnemyCount, RequestedEnemyCount, FailedSpawnCount);

	return FString::Printf(
		TEXT("[%s] %s State=%s%s L=%d WT=%d Seed=%d\n")
		TEXT("Isolation WorldSpawning=%s\n")
		TEXT("Population %s Alive=%d Active=%d Threatening=%d Hitters1s=%d Near[1k/2k/4k/8k]=%d/%d/%d/%d\n")
		TEXT("Flow Kills=%d KillsPer10s=%.2f ThreateningArrivals=%d ArrivalRate=%.2f/s NetPressure=%.2f/s\n")
		TEXT("Player HP=%.1f/%.1f Min=%.1f Attack=%.1f Deaths=%d Respawns=%d Baseline=%s\n")
		TEXT("Progression L=%d XP=%.0f/%.0f Armor=%.1f Dodge=%.1f%% Regen=%.1f Speed=%.1f EnemyLiveL=%d-%d(%.1f) SpawnL=%d-%d(%.1f) Items=%d Equipped=%d Added=%d EquippedEvents=%d\n")
		TEXT("Damage Total=%.1f AvgDPS=%.1f Peak[0.5/1/3]=%.1f/%.1f/%.1f Healing=%.1f Heal/s=%.1f Time=%.1f Result=%s"),
		bObservingProductionRift ? TEXT("BalanceRun") : TEXT("CombatTest"),
		*ScenarioName.ToString(),
		*GetStateString(),
		*Countdown,
		EnemyLevel,
		WorldTier,
		TestSeed,
		AeyerjiCombatTestIsolation::IsWorldSpawningSuppressed() ? TEXT("SUPPRESSED") : TEXT("ENABLED"),
		*PopulationIdentity,
		AliveEnemyCount,
		CombatActiveEnemyCount,
		ThreateningEnemyCount,
		UniqueHittersOneSecond,
		EnemiesWithin1000,
		EnemiesWithin2000,
		EnemiesWithin4000,
		EnemiesWithin8000,
		EnemyKills,
		PlayerKillsPerTenSeconds,
		ThreateningArrivalCount,
		ThreateningArrivalsPerSecond,
		CalculateNetPressure(ThreateningArrivalsPerSecond, PlayerKillsPerTenSeconds),
		PlayerHealth,
		PlayerMaxHealth,
		LowestPlayerHealth,
		PlayerAttackDamage,
		PlayerDeathCount,
		PlayerRespawnCount,
		bCanonicalPlayerBaseline ? TEXT("PASS") : TEXT("WARNING"),
		CurrentPlayerLevel,
		CurrentPlayerXP,
		CurrentPlayerXPMax,
		CurrentPlayerArmor,
		CurrentPlayerDodgeChance * 100.f,
		CurrentPlayerHPRegen,
		CurrentPlayerRunSpeed,
		ActiveEnemyLevelMin,
		ActiveEnemyLevelMax,
		ActiveEnemyLevelAverage,
		ActiveEnemySpawnLevelMin,
		ActiveEnemySpawnLevelMax,
		ActiveEnemySpawnLevelAverage,
		InventoryItemCount,
		EquippedItemCount,
		ItemsAddedDuringRun,
		ItemsEquippedDuringRun,
		TotalIncomingDamage,
		AverageIncomingDPS,
		PeakDamageHalfSecond,
		PeakDamageOneSecond,
		PeakDamageThreeSeconds,
		TotalHealing,
		AverageHealingPerSecond,
		ElapsedCombatSeconds,
		CompletionReason.IsNone() ? TEXT("-") : *CompletionReason.ToString());
}

int32 AAeyerjiCombatBalanceTestHarness::CountLocallyVisibleEnemies(
	APlayerController* LocalController,
	int32& OutLocalActive) const
{
	OutLocalActive = 0;
	if (!IsValid(LocalController) || !GetWorld())
	{
		return 0;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	LocalController->GetViewportSize(ViewportWidth, ViewportHeight);
	if (ViewportWidth <= 0 || ViewportHeight <= 0)
	{
		return 0;
	}

	int32 Visible = 0;
	for (TActorIterator<AEnemyParentNative> It(GetWorld()); It; ++It)
	{
		const AEnemyParentNative* Enemy = *It;
		if (!IsValid(Enemy)
			|| !Enemy->IsAlive(AeyerjiTags::State_Dead)
			|| (bObservingProductionRift && !Enemy->IsEncounterCombatActive())
			|| (!bObservingProductionRift && Enemy->GetOwner() != this && !Enemy->ActorHasTag(TestEnemyActorTag)))
		{
			continue;
		}

		OutLocalActive += Enemy->IsEncounterCombatActive() ? 1 : 0;
		FVector2D ScreenPosition;
		if (LocalController->ProjectWorldLocationToScreen(Enemy->GetActorLocation(), ScreenPosition)
			&& ScreenPosition.X >= 0.f
			&& ScreenPosition.Y >= 0.f
			&& ScreenPosition.X <= static_cast<float>(ViewportWidth)
			&& ScreenPosition.Y <= static_cast<float>(ViewportHeight))
		{
			++Visible;
		}
	}

	return Visible;
}

FString AAeyerjiCombatBalanceTestHarness::BuildLocalSummary(APlayerController* LocalController) const
{
	int32 LocalActive = 0;
	const int32 Visible = CountLocallyVisibleEnemies(LocalController, LocalActive);
	return BuildSummary() + FString::Printf(
		TEXT("\nClient ViewportVisible=%d LocalActive=%d (%s for full log)"),
		Visible,
		LocalActive,
		bObservingProductionRift ? TEXT("AJ_BalanceRunStatus") : TEXT("AJ_CombatTestStatus"));
}

void AAeyerjiCombatBalanceTestHarness::UpdateLocalHUD(const float DeltaSeconds)
{
	if (!bLocalCombatTestHUDEnabled || !GEngine || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	LocalHUDAccumulator += DeltaSeconds;
	if (LocalHUDAccumulator < LocalHUDIntervalSeconds)
	{
		return;
	}
	LocalHUDAccumulator = 0.f;

	APlayerController* LocalController = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(LocalController) || !LocalController->IsLocalController())
	{
		return;
	}

	GEngine->AddOnScreenDebugMessage(
		CombatTestHUDMessageKey,
		LocalHUDIntervalSeconds + 0.1f,
		bCanonicalPlayerBaseline ? FColor::Cyan : FColor::Yellow,
		BuildLocalSummary(LocalController));

	if (!bObservingProductionRift)
	{
		DrawDebugCircle(GetWorld(), TestCenter, MinimumSpawnRadius, 64, FColor::Green, false, LocalHUDIntervalSeconds + 0.1f, 0, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		DrawDebugCircle(GetWorld(), TestCenter, MaximumSpawnRadius, 64, FColor::Orange, false, LocalHUDIntervalSeconds + 0.1f, 0, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
	}
}

void AAeyerjiCombatBalanceTestHarness::SetLocalHUDEnabled(const bool bEnabled)
{
	bLocalCombatTestHUDEnabled = bEnabled;
	if (!bEnabled && GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(CombatTestHUDMessageKey);
	}
}

bool AAeyerjiCombatBalanceTestHarness::IsLocalHUDEnabled()
{
	return bLocalCombatTestHUDEnabled;
}

FString AAeyerjiCombatBalanceTestHarness::GetPresetList()
{
	return TEXT("Sanity1, Floor8, Region12, Ranged12, Grunt20, Dense24, Elite24, Overwhelmed36, Cap48, Bombardier12");
}

void AAeyerjiCombatBalanceTestHarness::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ScenarioName);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CompositionName);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, TestState);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemyLevel);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, WorldTier);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, TestSeed);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, RequestedEnemyCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, SpawnedEnemyCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, FailedSpawnCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, AliveEnemyCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CombatActiveEnemyCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemiesWithin1000);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemiesWithin2000);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemiesWithin4000);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemiesWithin8000);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EnemyKills);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerKillsPerTenSeconds);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ThreateningArrivalCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ThreateningArrivalsPerSecond);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ThreateningEnemyCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, UniqueHittersOneSecond);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ElapsedCombatSeconds);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerHealth);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerMaxHealth);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerAttackDamage);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, LowestPlayerHealth);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, TotalIncomingDamage);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, AverageIncomingDPS);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, TotalHealing);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, AverageHealingPerSecond);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerDeathCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PlayerRespawnCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerLevel);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerXP);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerXPMax);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerArmor);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerDodgeChance);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerHPRegen);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CurrentPlayerRunSpeed);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemyLevelMin);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemyLevelMax);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemyLevelAverage);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemySpawnLevelMin);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemySpawnLevelMax);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ActiveEnemySpawnLevelAverage);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, InventoryItemCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, EquippedItemCount);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ItemsAddedDuringRun);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, ItemsEquippedDuringRun);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PeakDamageHalfSecond);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PeakDamageOneSecond);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, PeakDamageThreeSeconds);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, TestCenter);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, MinimumSpawnRadius);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, MaximumSpawnRadius);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, bCanonicalPlayerBaseline);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, CompletionReason);
	DOREPLIFETIME(AAeyerjiCombatBalanceTestHarness, bObservingProductionRift);
}
