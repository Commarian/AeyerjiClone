#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Systems/AeyerjiSaveTypes.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "AeyerjiWorldStateSaveGame.generated.h"

/**
 * Shared save artifact for persistent world-state facts.
 */
UCLASS()
class AEYERJI_API UAeyerjiWorldStateSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Save schema version used by the manager to detect manager-era payloads. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int32 SchemaVersion = 0;

	/** Monotonic revision used to identify the latest shared-world payload. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int64 Revision = 0;

	/** UTC timestamp of the last successful commit. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FDateTime LastModifiedUtc = FDateTime::MinValue();

	/** Shared owner stamp for save manager metadata consistency. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FString OwnerKey;

	/** Artifact kind stamp used by the save manager. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	EAeyerjiSaveArtifactKind ArtifactKind = EAeyerjiSaveArtifactKind::WorldState;

	/** Persistent world-state entries. Runtime-only entries are intentionally excluded. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	TArray<FAeyerjiWorldStateEntry> Entries;
};
