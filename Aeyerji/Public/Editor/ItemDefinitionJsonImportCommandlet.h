// ItemDefinitionJsonImportCommandlet.h
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "ItemDefinitionJsonImportCommandlet.generated.h"

/**
 * Editor commandlet that imports bulk-authored JSON item data into cooked UItemDefinition assets.
 */
UCLASS()
class AEYERJI_API UItemDefinitionJsonImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UItemDefinitionJsonImportCommandlet();

	/** Imports item definitions from the JSON file passed with -Json= and writes assets under -Dest=. */
	virtual int32 Main(const FString& Params) override;
};
