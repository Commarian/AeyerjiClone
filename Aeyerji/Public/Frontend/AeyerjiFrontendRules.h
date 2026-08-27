#pragma once

#include "CoreMinimal.h"
#include "Frontend/AeyerjiFrontendTypes.h"

class UAeyerjiSaveGame;
struct FAeyerjiRiftTierRow;

/** Pure deterministic rules shared by frontend authority code and automation. */
namespace AeyerjiFrontendRules
{
	AEYERJI_API FAeyerjiFrontendSnapshot BuildSnapshot(
		const UAeyerjiSaveGame* SaveData,
		EAeyerjiFrontendProfileState ProfileState,
		EAeyerjiFrontendOperationState OperationState);

	AEYERJI_API int32 ResolveLeaderPlayerId(const TArray<int32>& PlayerIds);

	AEYERJI_API bool ShouldResetAllReadiness(
		const TArray<int32>& PreviousRoster,
		const TArray<int32>& NewRoster,
		int32 PreviousLeader,
		int32 NewLeader,
		bool bSharedSelectionChanged);

	AEYERJI_API bool IsProfileTransferLayoutValid(
		int32 TotalBytes,
		int32 ChunkSize,
		int32 MaximumBytes,
		int32 MaximumChunkSize);

	/**
	 * Validates profile ownership before accepting a client transport.
	 * Authenticated identities require the server-derived owner, while an unauthenticated
	 * NULL/LAN identity may bind one bounded owner that cannot change afterward.
	 */
	AEYERJI_API bool IsProfileOwnerKeyAccepted(
		const FString& SubmittedOwner,
		const FString& ExpectedOwner,
		const FString& BoundOwner,
		bool bAllowFirstUnauthenticatedBinding);

	AEYERJI_API EAeyerjiFrontendFailure ValidateLaunch(
		const FAeyerjiLobbySnapshot& Snapshot,
		int32 RequesterPlayerId,
		const FAeyerjiRiftTierRow* TierRow);

	/** Clears the request only when the expected id matches, making repeated consumption fail. */
	AEYERJI_API bool ConsumeLaunchRequest(FAeyerjiPendingRunLaunchRequest& Request, int32 ExpectedRequestId);
}
