#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PrimaryMeleeComboProviderInterface.generated.h"

class UAnimMontage;

/**
 * Interface implemented by pawns/avatars that want to drive their own primary melee combo montages.
 * The gameplay ability queries this at activation time and will fall back to its own data if the interface is absent.
 */
UINTERFACE(BlueprintType)
class AEYERJI_API UPrimaryMeleeComboProviderInterface : public UInterface
{
	GENERATED_BODY()
};

class AEYERJI_API IPrimaryMeleeComboProviderInterface
{
	GENERATED_BODY()

public:
	/**
	 * Return the ordered list of combo montages for the primary melee attack.
	 * Only the first few entries are consumed (see ability MaxProviderComboMontages), but the list can be longer for future expansion.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat|Animation")
	void GetPrimaryMeleeComboMontages(TArray<UAnimMontage*>& OutMontages) const;
};
