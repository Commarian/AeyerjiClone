#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Systems/AeyerjiSaveTypes.h"
#include "AeyerjiStreamingSaveGame.generated.h"

/**
 * Persistent runtime state for streaming/session flow that should survive process restarts.
 */
UCLASS()
class AEYERJI_API UAeyerjiStreamingSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Save schema version used by the manager to detect manager-era payloads. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int32 SchemaVersion = 0;

	/** Monotonic revision used to resolve local/cloud conflicts. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int64 Revision = 0;

	/** UTC timestamp of the last successful authoritative commit. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FDateTime LastModifiedUtc = FDateTime::MinValue();

	/** Stable owner identity used for per-user save namespaces. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FString OwnerKey;

	/** Artifact kind stamp used when reconciling local and cloud payloads. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	EAeyerjiSaveArtifactKind ArtifactKind = EAeyerjiSaveArtifactKind::Streaming;

	/** Last zone successfully entered by the player/session. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Streaming")
	FName LastZoneId = NAME_None;

	/** Last selected gameplay map identifier. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Flow")
	FName LastGameplayMapId = NAME_None;

	/** Current campaign mode selection (true = sequential campaign progression). */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Flow")
	bool bCampaignMode = false;

	/** Next campaign map cursor used for sequential map selection. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Flow")
	int32 CampaignMapCursor = 0;

	/** Teleporters unlocked in persistent progression. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Persistence")
	TArray<FName> UnlockedTeleporterIds;

	/** Lightweight quest-state flags for persistent progression. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Persistence")
	TMap<FName, bool> QuestFlags;
};
