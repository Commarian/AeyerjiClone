#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Abilities/Enemy/GA_EnemyBombardment.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Enemy/AeyerjiBombardment.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/World.h"
#include "GAS/GE_DamagePhysical.h"
#include "Serialization/Csv/CsvParser.h"
#include "Testing/AeyerjiCombatBalanceTestHarness.h"
#include <limits>

namespace
{
	struct FBombardmentTestWorld
	{
		UWorld* World = nullptr;
		FBombardmentTestWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Values);
		}
		~FBombardmentTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		AEnemyParentNative* SpawnCharacter(const FVector& Position, uint8 Team)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AEnemyParentNative* Actor = World->SpawnActor<AEnemyParentNative>(Position, FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				return nullptr;
			}
			Actor->SetGenericTeamId(FGenericTeamId(Team));
			Actor->SetCanBeDamaged(true);
			Actor->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Actor->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
			Actor->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Overlap);
			Actor->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			UAbilitySystemComponent* ASC = Actor->GetAbilitySystemComponent();
			ASC->InitAbilityActorInfo(Actor, Actor);
			if (!ASC->GetSet<UAeyerjiAttributeSet>())
			{
				ASC->AddAttributeSetSubobject(NewObject<UAeyerjiAttributeSet>(Actor));
			}
			ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), 1000.f);
			ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), 1000.f);
			ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetArmorAttribute(), 0.f);
			ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetPhysicalDamageBonusAttribute(), 0.f);
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAeyerjiBombardmentGameplayTest, "Aeyerji.CombatTest.Bombardment.Gameplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiBombardmentGameplayTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("The visible radius includes its boundary"), AAeyerjiBombardment::IsInsideBlast(FVector::ZeroVector, FVector(250, 0, 90), 250, 180));
	TestFalse(TEXT("A target outside the warning escapes"), AAeyerjiBombardment::IsInsideBlast(FVector::ZeroVector, FVector(251, 0, 90), 250, 180));
	TestFalse(TEXT("A different floor is excluded"), AAeyerjiBombardment::IsInsideBlast(FVector::ZeroVector, FVector(0, 0, 181), 250, 180));
	TestFalse(TEXT("Nonfinite geometry is rejected"), AAeyerjiBombardment::IsInsideBlast(FVector::ZeroVector, FVector::ZeroVector, std::numeric_limits<float>::infinity(), 180));
	FBombardmentTestWorld Fixture;
	if (!TestNotNull(TEXT("Isolated physics world"), Fixture.World)) { return false; }
	AEnemyParentNative* Source = Fixture.SpawnCharacter(FVector(1200, 0, 90), 1);
	AEnemyParentNative* Target = Fixture.SpawnCharacter(FVector(0, 0, 90), 0);
	if (!TestNotNull(TEXT("Source"), Source) || !TestNotNull(TEXT("Target"), Target)) { return false; }
	UAbilitySystemComponent* SourceASC = Source->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UGE_DamagePhysical::StaticClass(), 1.f, SourceASC->MakeEffectContext());
	Spec.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_Instant, 30.f);
	auto SpawnZone = [&]() { return Fixture.World->SpawnActor<AAeyerjiBombardment>(); };
	AAeyerjiBombardment* First = SpawnZone();
	AAeyerjiBombardment* Second = SpawnZone();
	AAeyerjiBombardment* Third = SpawnZone();
	if (!First || !Second || !Third) { AddError(TEXT("Could not create warning actors")); return false; }
	TestTrue(TEXT("First warning starts"), First->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	TestFalse(TEXT("A warning cannot initialize twice"), First->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	TestTrue(TEXT("Second warning starts"), Second->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	TestFalse(TEXT("Third warning rejected by shared world cap"), Third->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	Second->CancelBombardment();
	TestTrue(TEXT("Cancellation releases capacity immediately"), AAeyerjiBombardment::CanStartBombardment(Fixture.World));
	Target->SetActorLocation(FVector(500, 0, 90));
	First->ResolveImpact();
	TestEqual(TEXT("Moving out before impact causes no damage"), TargetASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()), 1000.f);
	Target->SetActorLocation(FVector(0, 0, 90));
	TestTrue(TEXT("Next warning initializes"), Third->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	Third->ResolveImpact();
	TestEqual(TEXT("Impact submits actual GAS damage once"), TargetASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()), 970.f);
	Third->ResolveImpact();
	TestEqual(TEXT("Repeated impact cannot damage twice"), TargetASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()), 970.f);
	Target->SetGenericTeamId(FGenericTeamId(1));
	TestFalse(TEXT("Friendly characters are excluded"), AAeyerjiBombardment::IsDamageTarget(Source, Target));
	Target->SetGenericTeamId(FGenericTeamId(0));
	AAeyerjiBombardment* Interrupted = SpawnZone();
	if (!Interrupted) { return false; }
	TestTrue(TEXT("Interruptible warning starts"), Interrupted->InitializeBombardment(Source, Target, FVector::ZeroVector, 250, 1.4f, Spec));
	SourceASC->AddLooseGameplayTag(AeyerjiTags::State_CrowdControl_Stunned);
	Interrupted->ResolveImpact();
	TestFalse(TEXT("Stunned source cancels warning"), Interrupted->IsPending());
	TestEqual(TEXT("Cancelled warning causes no damage"), TargetASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()), 970.f);
	SourceASC->RemoveLooseGameplayTag(AeyerjiTags::State_CrowdControl_Stunned);
	// Activate the real primary through GAS and exercise the production pool-return seam.
	AActor* Ground = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Floor = NewObject<UBoxComponent>(Ground);
	Ground->SetRootComponent(Floor);
	Floor->SetBoxExtent(FVector(3000, 3000, 20));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Floor->SetCollisionObjectType(ECC_WorldStatic);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->RegisterComponent();
	Ground->SetActorLocation(FVector(0, 0, -20));
	SourceASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetAttackRangeAttribute(), 2000.f);
	SourceASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetAttackDamageAttribute(), 30.f);
	const FGameplayAbilitySpecHandle AbilityHandle = SourceASC->GiveAbility(FGameplayAbilitySpec(UGA_EnemyBombardment::StaticClass(), 1));
	FGameplayEventData Payload;
	Payload.Target = Target;
	int32 Completions = 0;
	auto& CompletionDelegate = SourceASC->GenericGameplayEventCallbacks.FindOrAdd(AeyerjiTags::Event_PrimaryAttack_Completed);
	const FDelegateHandle CompletionHandle = CompletionDelegate.AddLambda([&](const FGameplayEventData*) { ++Completions; });
	TestTrue(TEXT("GAS activates the real primary with a supplied target"), SourceASC->TriggerAbilityFromGameplayEvent(
		AbilityHandle, SourceASC->AbilityActorInfo.Get(), AeyerjiTags::Event_External_Target, &Payload, *SourceASC));
	TestTrue(TEXT("Primary remains active during its warning"), SourceASC->FindAbilitySpecFromHandle(AbilityHandle)->IsActive());
	TestTrue(TEXT("Cast owns the replicated casting tag"), SourceASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting));
	Source->PrepareForPooledDeactivation();
	TestFalse(TEXT("Pool return cancels the primary immediately"), SourceASC->FindAbilitySpecFromHandle(AbilityHandle)->IsActive());
	TestFalse(TEXT("Pool return releases cast lock"), SourceASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting));
	TestEqual(TEXT("StateTree completion sent once"), Completions, 1);
	CompletionDelegate.Remove(CompletionHandle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAeyerjiBombardmentTelemetryTest, "Aeyerji.CombatTest.Bombardment.Telemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiBombardmentTelemetryTest::RunTest(const FString& Parameters)
{
	FBombardmentTestWorld Fixture;
	if (!Fixture.World) { return false; }
	AAeyerjiCombatBalanceTestHarness* Harness = Fixture.World->SpawnActor<AAeyerjiCombatBalanceTestHarness>();
	AActor* Source = Fixture.World->SpawnActor<AActor>();
	AActor* Aim = Fixture.World->SpawnActor<AActor>();
	if (!Harness || !Source || !Aim) { AddError(TEXT("Could not create telemetry fixture")); return false; }
	Harness->TestState = EAeyerjiCombatTestState::Running;
	using Phase = EAeyerjiBombardmentTelemetryEvent;
	auto Record = [&](uint32 Id, Phase Event, int32 Applications = INDEX_NONE)
	{
		Harness->RecordBombardmentEvent(Source, Aim, Id, Event, FVector(1, 2, 3), 250, Applications, Applications);
	};
	Record(1, Phase::Started);
	Record(1, Phase::Started);
	Record(2, Phase::Started);
	Record(1, Phase::Impacted, 1);
	Record(1, Phase::Impacted, 1);
	Harness->TestedPawn = nullptr; // Player rebinding must not own or reset event recording.
	Record(2, Phase::Cancelled);
	Record(99, Phase::Impacted, 5);
	TestEqual(TEXT("Starts deduplicate"), Harness->BombardmentStarts, 2);
	TestEqual(TEXT("Impacts deduplicate and require a recorded start"), Harness->BombardmentImpacts, 1);
	TestEqual(TEXT("Cancellation survives loss of tested player"), Harness->BombardmentCancellations, 1);
	TestEqual(TEXT("Applications count submitted damage specs"), Harness->BombardmentDamageApplications, 1);
	TestEqual(TEXT("All tracked warnings terminal"), Harness->ActiveBombardments.Num(), 0);
	TestEqual(TEXT("Peak concurrency"), Harness->BombardmentPeakActiveZones, 2);
	Harness->BombardmentEvents[0].Source = TEXT("Source,\"quoted\"");
	const FCsvParser Parser(Harness->BuildBombardmentCSV());
	const FCsvParser::FRows& Rows = Parser.GetRows();
	TestEqual(TEXT("Header plus four discrete events"), Rows.Num(), 5);
	for (const auto& Row : Rows)
	{
		TestEqual(TEXT("CSV column count including quoted source"), Row.Num(), 18);
	}
	TestTrue(TEXT("Summary agrees with authoritative event counters"), Harness->BuildBombardmentSummary().Contains(TEXT("BombardmentDamageApplications=1\n")));
	Harness->ScenarioName = TEXT("BombardmentAutomation");
	auto& Sample = Harness->PopulationSamples.AddDefaulted_GetRef();
	Sample.BombardmentStarts = Harness->BombardmentStarts;
	Sample.BombardmentImpacts = Harness->BombardmentImpacts;
	Sample.BombardmentCancellations = Harness->BombardmentCancellations;
	Sample.BombardmentDamageApplications = Harness->BombardmentDamageApplications;
	Sample.BombardmentActiveZones = Harness->ActiveBombardments.Num();
	Sample.BombardmentPeakActiveZones = Harness->BombardmentPeakActiveZones;
	Harness->WriteReports();
	TestTrue(TEXT("Production report writer saves all report files"), Harness->bReportWritten);
	FString SavedSamples;
	FString SavedEvents;
	FString SavedSummary;
	TestTrue(TEXT("Samples file is readable"), FFileHelper::LoadFileToString(SavedSamples, *(Harness->ReportBasePath + TEXT("_samples.csv"))));
	TestTrue(TEXT("Bombardment file is readable"), FFileHelper::LoadFileToString(SavedEvents, *(Harness->ReportBasePath + TEXT("_bombardments.csv"))));
	TestTrue(TEXT("Summary file is readable"), FFileHelper::LoadFileToString(SavedSummary, *(Harness->ReportBasePath + TEXT("_summary.txt"))));
	const FCsvParser SampleParser(SavedSamples);
	for (const auto& Row : SampleParser.GetRows())
	{
		TestEqual(TEXT("Appended population fields align with header"), Row.Num(), 40);
	}
	TestEqual(TEXT("Saved event CSV agrees with rendered events"), SavedEvents, Harness->BuildBombardmentCSV());
	TestTrue(TEXT("Saved summary agrees with final sample counters"), SavedSummary.Contains(TEXT("BombardmentStarts=2\n"))
		&& SavedSummary.Contains(TEXT("BombardmentDamageApplications=1\n")));
	for (uint32 Id = 100; Id < 4300; ++Id)
	{
		Record(Id, Phase::Started);
		Record(Id, Phase::Cancelled);
	}
	TestEqual(TEXT("Long run event storage is bounded"), Harness->BombardmentEvents.Num(), 8192);
	TestTrue(TEXT("Truncation is reported"), Harness->BombardmentDroppedEvents > 0);
	TestEqual(TEXT("Storage truncation does not leak active warnings"), Harness->ActiveBombardments.Num(), 0);
	Harness->ResetBombardmentTelemetry();
	TestEqual(TEXT("New recording resets previous totals"), Harness->BombardmentStarts, 0);
	TestEqual(TEXT("New recording resets previous events"), Harness->BombardmentEvents.Num(), 0);
	Harness->TestState = EAeyerjiCombatTestState::Completed;
	Record(5000, Phase::Started);
	TestEqual(TEXT("Stopped report is immutable"), Harness->BombardmentStarts, 0);
	FAeyerjiCombatTestRequest Request;
	FString Error;
	TestTrue(TEXT("Opt-in mixed preset resolves without requiring its asset"), Harness->ResolvePreset(TEXT("Bombardier12"), Request, Error));
	TestEqual(TEXT("Mixed preset population"), Request.EnemyCount, 12);
	TestEqual(TEXT("Mixed preset composition"), Request.CompositionName, FName(TEXT("BombardierMixed")));
	return true;
}

#endif
