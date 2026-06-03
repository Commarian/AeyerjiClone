#pragma once

#include "CoreMinimal.h"
#include "AeyerjiSaveTypes.generated.h"

/** Distinguishes profile saves from streaming/world-flow saves in storage and RPC payloads. */
UENUM(BlueprintType)
enum class EAeyerjiSaveArtifactKind : uint8
{
	Unknown = 0,
	Profile,
	Streaming,
	WorldState
};

/** Authoritative moments where the runtime profile is allowed to be persisted. */
UENUM(BlueprintType)
enum class EAeyerjiSaveCheckpointReason : uint8
{
	Unknown = 0,
	ProfileCreatedOrMigrated,
	DeathBeforeRespawn,
	RunCompleted,
	ReturnToMenu,
	RetryRun,
	PawnEndPlay,
	LogoutOrShutdown,
	Manual
};

/** Small metadata header replicated alongside serialized save bytes. */
USTRUCT(BlueprintType)
struct FAeyerjiSaveTransportHeader
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	EAeyerjiSaveArtifactKind ArtifactKind = EAeyerjiSaveArtifactKind::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FString OwnerKey;

	// When a profile uses a named character slot, carry that slot through pawn RPC
	// transport so it cannot race against PlayerState replication/RPC ordering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FString ExplicitSaveSlotOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int32 SchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int64 Revision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FDateTime LastModifiedUtc = FDateTime::MinValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	bool bHadPersistedData = false;
};
