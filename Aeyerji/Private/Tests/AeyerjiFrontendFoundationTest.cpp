#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Aeyerji/AeyerjiSaveGame.h"
#include "Engine/CurveTable.h"
#include "Frontend/AeyerjiFrontendRules.h"
#include "Kismet/GameplayStatics.h"
#include "Progression/AeyerjiProgressionLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiFrontendSnapshotAndProgressionTest,
	"Aeyerji.Frontend.SnapshotAndSharedProgression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiFrontendSnapshotAndProgressionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
	TestNotNull(TEXT("Profile save object is available."), SaveData);
	if (!SaveData)
	{
		return false;
	}
	SaveData->Revision = 17;
	SaveData->Attributes.Level = 10;
	SaveData->Attributes.XP = 23.f;
	SaveData->Gold = 456;
	SaveData->HighestUnlockedRiftTier = 8;
	SaveData->LastSelectedRiftTier = 5;

	const FAeyerjiFrontendSnapshot Snapshot = AeyerjiFrontendRules::BuildSnapshot(
		SaveData, EAeyerjiFrontendProfileState::Ready, EAeyerjiFrontendOperationState::Idle);
	TestEqual(TEXT("Snapshot carries revision."), Snapshot.ProfileRevision, int64(17));
	TestEqual(TEXT("Snapshot carries character level."), Snapshot.CharacterLevel, 10);
	TestEqual(TEXT("Snapshot carries current XP."), Snapshot.CurrentXP, 23.f);
	TestEqual(TEXT("Snapshot carries gold."), Snapshot.Gold, int64(456));
	TestEqual(TEXT("Snapshot carries the unlock cap."), Snapshot.HighestUnlockedExcursionTier, 8);
	TestEqual(TEXT("Snapshot carries the preferred tier."), Snapshot.PreferredExcursionTier, 5);

	UCurveTable* CurveTable = LoadObject<UCurveTable>(nullptr, TEXT("/Game/Player/XPCurveTable.XPCurveTable"));
	TestNotNull(TEXT("Shared XP curve table loads."), CurveTable);
	if (CurveTable)
	{
		FCurveTableRowHandle Row;
		Row.CurveTable = CurveTable;
		Row.RowName = FName(TEXT("XP_Needed"));
		const float DirectCurveValue = Row.Eval(10.f, TEXT("Frontend progression automation"));
		TestTrue(TEXT("Shared XP resolver matches the gameplay curve row."),
			FMath::IsNearlyEqual(Snapshot.XPRequiredForNextLevel, DirectCurveValue));
		TestTrue(TEXT("Shared XP resolver is positive."), UAeyerjiProgressionLibrary::GetXPRequiredForLevel(10) > 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiFrontendLobbyRulesTest,
	"Aeyerji.Frontend.AuthoritativeLobbyRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiFrontendLobbyRulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Lowest stable PlayerId is leader."),
		AeyerjiFrontendRules::ResolveLeaderPlayerId({ 42, 7, 19 }), 7);
	TestEqual(TEXT("Empty roster has no leader."),
		AeyerjiFrontendRules::ResolveLeaderPlayerId({}), INDEX_NONE);
	TestTrue(TEXT("Roster changes clear readiness."),
		AeyerjiFrontendRules::ShouldResetAllReadiness({ 7, 19 }, { 7, 19, 42 }, 7, 7, false));
	TestTrue(TEXT("Leader changes clear readiness."),
		AeyerjiFrontendRules::ShouldResetAllReadiness({ 7, 19 }, { 7, 19 }, 7, 19, false));
	TestTrue(TEXT("Activity/tier changes clear readiness."),
		AeyerjiFrontendRules::ShouldResetAllReadiness({ 7, 19 }, { 7, 19 }, 7, 7, true));
	TestFalse(TEXT("An unchanged lobby preserves readiness."),
		AeyerjiFrontendRules::ShouldResetAllReadiness({ 7, 19 }, { 7, 19 }, 7, 7, false));

	FAeyerjiLobbySnapshot Lobby;
	Lobby.Phase = EAeyerjiLobbyPhase::Waiting;
	Lobby.LeaderPlayerId = 7;
	Lobby.ActivityType = EAeyerjiRiftActivityType::Excursion;
	Lobby.SelectedExcursionTier = 2;
	Lobby.CommonExcursionTierCap = 3;
	for (const int32 PlayerId : { 7, 19 })
	{
		FAeyerjiLobbyMemberView& Member = Lobby.Members.AddDefaulted_GetRef();
		Member.PlayerId = PlayerId;
		Member.CharacterLevel = 10;
		Member.HighestUnlockedExcursionTier = 3;
		Member.ProfileState = EAeyerjiLobbyProfileState::Verified;
		Member.bReady = true;
	}
	FAeyerjiRiftTierRow TierRow;
	TierRow.MinimumCharacterLevel = 10;
	TestEqual(TEXT("Verified ready party passes launch validation."),
		AeyerjiFrontendRules::ValidateLaunch(Lobby, 7, &TierRow), EAeyerjiFrontendFailure::None);
	TestEqual(TEXT("Non-leader cannot launch."),
		AeyerjiFrontendRules::ValidateLaunch(Lobby, 19, &TierRow), EAeyerjiFrontendFailure::NotLeader);
	Lobby.Members[1].bReady = false;
	TestEqual(TEXT("Every member must manually ready."),
		AeyerjiFrontendRules::ValidateLaunch(Lobby, 7, &TierRow), EAeyerjiFrontendFailure::PartyNotReady);
	Lobby.Members[1].bReady = true;
	Lobby.SelectedExcursionTier = 4;
	TestEqual(TEXT("Selection above common cap is rejected."),
		AeyerjiFrontendRules::ValidateLaunch(Lobby, 7, &TierRow), EAeyerjiFrontendFailure::TierLockedForParty);
	Lobby.SelectedExcursionTier = 2;
	Lobby.Members[1].CharacterLevel = 9;
	TestEqual(TEXT("Tier minimum level is enforced for every member."),
		AeyerjiFrontendRules::ValidateLaunch(Lobby, 7, &TierRow), EAeyerjiFrontendFailure::TierLevelRequirement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiFrontendTransportAndLaunchRequestTest,
	"Aeyerji.Frontend.ProfileTransportAndOneShotLaunch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiFrontendTransportAndLaunchRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	constexpr int32 MaxBytes = 4 * 1024 * 1024;
	constexpr int32 MaxChunk = 48 * 1024;
	TestTrue(TEXT("Bounded profile transport layout is accepted."),
		AeyerjiFrontendRules::IsProfileTransferLayoutValid(128 * 1024, MaxChunk, MaxBytes, MaxChunk));
	TestFalse(TEXT("Empty profile transport is rejected."),
		AeyerjiFrontendRules::IsProfileTransferLayoutValid(0, MaxChunk, MaxBytes, MaxChunk));
	TestFalse(TEXT("Oversized profile transport is rejected."),
		AeyerjiFrontendRules::IsProfileTransferLayoutValid(MaxBytes + 1, MaxChunk, MaxBytes, MaxChunk));
	TestFalse(TEXT("Oversized RPC chunks are rejected."),
		AeyerjiFrontendRules::IsProfileTransferLayoutValid(128 * 1024, MaxChunk + 1, MaxBytes, MaxChunk));

	FAeyerjiPendingRunLaunchRequest Request;
	Request.RequestId = 31;
	Request.ActivityType = EAeyerjiRiftActivityType::Excursion;
	Request.ExcursionTier = 4;
	Request.MapId = FName(TEXT("AutomationMap"));
	Request.MapPackageName = FName(TEXT("/Game/Levels/AutomationMap"));
	TestFalse(TEXT("Wrong request id cannot consume launch handoff."),
		AeyerjiFrontendRules::ConsumeLaunchRequest(Request, 30));
	TestTrue(TEXT("Exact request id consumes launch handoff once."),
		AeyerjiFrontendRules::ConsumeLaunchRequest(Request, 31));
	TestFalse(TEXT("Consumed launch handoff cannot be consumed twice."),
		AeyerjiFrontendRules::ConsumeLaunchRequest(Request, 31));
	return true;
}

#endif
