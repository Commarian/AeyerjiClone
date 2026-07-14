#pragma once

#include "CoreMinimal.h"
#include "AeyerjiRunTypes.h"
#include "Systems/AeyerjiRiftTypes.h"
#include "AeyerjiFrontendTypes.generated.h"

/** Local profile resolution state presented by the frontend. */
UENUM(BlueprintType)
enum class EAeyerjiFrontendProfileState : uint8
{
	Uninitialized,
	Resolving,
	Ready,
	Failed
};

/** Current asynchronous frontend/session operation. */
UENUM(BlueprintType)
enum class EAeyerjiFrontendOperationState : uint8
{
	Idle,
	ResolvingProfile,
	CreatingSession,
	SearchingSessions,
	JoiningSession,
	LeavingSession,
	Launching,
	Failed
};

/** Replicated party staging phase. */
UENUM(BlueprintType)
enum class EAeyerjiLobbyPhase : uint8
{
	Waiting,
	Launching,
	InGameplay
};

/** Server-verified profile preflight state for one lobby member. */
UENUM(BlueprintType)
enum class EAeyerjiLobbyProfileState : uint8
{
	NotSubmitted,
	Receiving,
	Verified,
	Failed
};

/** Stable frontend failure codes. Blueprint should present the supplied localized text. */
UENUM(BlueprintType)
enum class EAeyerjiFrontendFailure : uint8
{
	None,
	Busy,
	OnlineUnavailable,
	ProfileResolveFailed,
	ProfileTransferRejected,
	SessionCreateFailed,
	SessionSearchFailed,
	SessionJoinFailed,
	SessionLeaveFailed,
	SessionFull,
	SessionInProgress,
	NetworkFailure,
	NotLeader,
	PartyNotReady,
	TierNotDefined,
	TierLockedForParty,
	TierLevelRequirement,
	LaunchFailed
};

/** Compact immutable local-profile summary used by the persistent frontend header. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiFrontendSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	EAeyerjiFrontendProfileState ProfileState = EAeyerjiFrontendProfileState::Uninitialized;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	EAeyerjiFrontendOperationState OperationState = EAeyerjiFrontendOperationState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	int64 ProfileRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	int32 CharacterLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	float CurrentXP = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	float XPRequiredForNextLevel = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	int64 Gold = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	int32 HighestUnlockedExcursionTier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	int32 PreferredExcursionTier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	FAeyerjiCompletedRunRecord LatestRun;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend")
	bool bHasLatestRun = false;
};

/** Blueprint-safe projection of a native online-session search result. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSessionSearchResultView
{
	GENERATED_BODY()

	/** Ephemeral identifier valid only until the next search. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	int32 ResultId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	FString PartyName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	FString HostName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	int32 MaximumPlayers = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	int32 PingMilliseconds = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	EAeyerjiRiftActivityType ActivityType = EAeyerjiRiftActivityType::StandardRift;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	int32 ExcursionTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Session")
	bool bJoinable = false;
};

/** Presentation-safe server-owned state for one connected lobby participant. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiLobbyMemberView
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 CharacterLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 HighestUnlockedExcursionTier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	EAeyerjiLobbyProfileState ProfileState = EAeyerjiLobbyProfileState::NotSubmitted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	bool bReady = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	bool bLeader = false;
};

/** Replicated server-authoritative party staging snapshot consumed by the menu. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiLobbySnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	EAeyerjiLobbyPhase Phase = EAeyerjiLobbyPhase::Waiting;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 LeaderPlayerId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	EAeyerjiRiftActivityType ActivityType = EAeyerjiRiftActivityType::StandardRift;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 SelectedExcursionTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	int32 CommonExcursionTierCap = 0;

	/** Synchronized GameState server time at which authoritative travel begins. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	float LaunchAtServerTimeSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	TArray<FAeyerjiLobbyMemberView> Members;
};

/** Server-only GameInstance handoff that survives lobby-to-gameplay travel. */
USTRUCT()
struct AEYERJI_API FAeyerjiPendingRunLaunchRequest
{
	GENERATED_BODY()

	int32 RequestId = 0;
	EAeyerjiRiftActivityType ActivityType = EAeyerjiRiftActivityType::StandardRift;
	int32 ExcursionTier = 0;
	FName MapId = NAME_None;
	FName MapPackageName = NAME_None;

	bool IsValid() const
	{
		return RequestId > 0
			&& !MapPackageName.IsNone()
			&& (ActivityType == EAeyerjiRiftActivityType::StandardRift || ExcursionTier > 0);
	}
};

