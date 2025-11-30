#pragma once
#include "Engine/GameInstance.h"
#include "AeyerjiGameInstance.generated.h"

UCLASS()
class AEYERJI_API UAeyerjiGameInstance : public UGameInstance
{
	GENERATED_BODY()

protected:
	virtual void Shutdown() override;   // called in PIE stop & real quit
};