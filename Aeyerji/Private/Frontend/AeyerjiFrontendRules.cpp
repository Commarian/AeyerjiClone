#include "Frontend/AeyerjiFrontendRules.h"

#include "Aeyerji/AeyerjiSaveGame.h"
#include "Progression/AeyerjiProgressionLibrary.h"

FAeyerjiFrontendSnapshot AeyerjiFrontendRules::BuildSnapshot(const UAeyerjiSaveGame* SaveData,
	const EAeyerjiFrontendProfileState ProfileState, const EAeyerjiFrontendOperationState OperationState)
{
	FAeyerjiFrontendSnapshot Snapshot;
	Snapshot.ProfileState = ProfileState;
	Snapshot.OperationState = OperationState;
	if (!SaveData)
	{
		return Snapshot;
	}
	Snapshot.ProfileRevision = SaveData->Revision;
	Snapshot.CharacterLevel = FMath::Max(1, SaveData->Attributes.Level);
	Snapshot.CurrentXP = FMath::Max(0.f, SaveData->Attributes.XP);
	Snapshot.XPRequiredForNextLevel = UAeyerjiProgressionLibrary::GetXPRequiredForLevel(Snapshot.CharacterLevel);
	Snapshot.Gold = FMath::Max<int64>(0, SaveData->Gold);
	Snapshot.HighestUnlockedExcursionTier = FMath::Max(1, SaveData->HighestUnlockedRiftTier);
	Snapshot.PreferredExcursionTier = FMath::Clamp(
		SaveData->LastSelectedRiftTier, 1, Snapshot.HighestUnlockedExcursionTier);
	Snapshot.bHasLatestRun = SaveData->RecentRuns.Num() > 0;
	Snapshot.LatestRun = Snapshot.bHasLatestRun ? SaveData->RecentRuns[0] : FAeyerjiCompletedRunRecord();
	return Snapshot;
}

int32 AeyerjiFrontendRules::ResolveLeaderPlayerId(const TArray<int32>& PlayerIds)
{
	int32 LeaderId = INDEX_NONE;
	for (const int32 PlayerId : PlayerIds)
	{
		if (LeaderId == INDEX_NONE || PlayerId < LeaderId)
		{
			LeaderId = PlayerId;
		}
	}
	return LeaderId;
}

bool AeyerjiFrontendRules::ShouldResetAllReadiness(const TArray<int32>& PreviousRoster,
	const TArray<int32>& NewRoster, const int32 PreviousLeader, const int32 NewLeader,
	const bool bSharedSelectionChanged)
{
	return bSharedSelectionChanged || PreviousRoster != NewRoster || PreviousLeader != NewLeader;
}

bool AeyerjiFrontendRules::IsProfileTransferLayoutValid(const int32 TotalBytes, const int32 ChunkSize,
	const int32 MaximumBytes, const int32 MaximumChunkSize)
{
	return TotalBytes > 0 && TotalBytes <= MaximumBytes
		&& ChunkSize > 0 && ChunkSize <= MaximumChunkSize;
}

EAeyerjiFrontendFailure AeyerjiFrontendRules::ValidateLaunch(const FAeyerjiLobbySnapshot& Snapshot,
	const int32 RequesterPlayerId, const FAeyerjiRiftTierRow* TierRow)
{
	if (Snapshot.Phase != EAeyerjiLobbyPhase::Waiting || Snapshot.LeaderPlayerId != RequesterPlayerId)
	{
		return EAeyerjiFrontendFailure::NotLeader;
	}
	if (Snapshot.Members.IsEmpty())
	{
		return EAeyerjiFrontendFailure::PartyNotReady;
	}
	for (const FAeyerjiLobbyMemberView& Member : Snapshot.Members)
	{
		if (Member.ProfileState != EAeyerjiLobbyProfileState::Verified || !Member.bReady)
		{
			return EAeyerjiFrontendFailure::PartyNotReady;
		}
	}
	if (Snapshot.ActivityType == EAeyerjiRiftActivityType::StandardRift)
	{
		return EAeyerjiFrontendFailure::None;
	}
	if (!TierRow)
	{
		return EAeyerjiFrontendFailure::TierNotDefined;
	}
	if (Snapshot.SelectedExcursionTier <= 0
		|| Snapshot.SelectedExcursionTier > Snapshot.CommonExcursionTierCap)
	{
		return EAeyerjiFrontendFailure::TierLockedForParty;
	}
	for (const FAeyerjiLobbyMemberView& Member : Snapshot.Members)
	{
		if (Member.CharacterLevel < FMath::Max(1, TierRow->MinimumCharacterLevel))
		{
			return EAeyerjiFrontendFailure::TierLevelRequirement;
		}
	}
	return EAeyerjiFrontendFailure::None;
}

bool AeyerjiFrontendRules::ConsumeLaunchRequest(FAeyerjiPendingRunLaunchRequest& Request,
	const int32 ExpectedRequestId)
{
	if (!Request.IsValid() || Request.RequestId != ExpectedRequestId)
	{
		return false;
	}
	Request = FAeyerjiPendingRunLaunchRequest();
	return true;
}
