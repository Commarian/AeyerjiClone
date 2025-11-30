//  AttributeSet_Ranges.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet_Ranges.generated.h"


/* ---- 4 tiny inline helpers ------------------------------------------------
   They generate:  GetBlinkRangeAttribute(),  GetBlinkRange(),  SetBlinkRange(),
				   InitBlinkRange()
---------------------------------------------------------------------------- */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName)                 \
GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName)       \
GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName)                     \
GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName)                     \
GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class AEYERJI_API UAttributeSet_Ranges : public UAttributeSet
{
	GENERATED_BODY()

public:

	/* ---- Blink range ---------------------------------------------------- */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blink",
			  ReplicatedUsing = OnRep_BlinkRange)
	FGameplayAttributeData BlinkRange;
	ATTRIBUTE_ACCESSORS(UAttributeSet_Ranges, BlinkRange)

	UFUNCTION()
	void OnRep_BlinkRange(const FGameplayAttributeData& OldValue);

	// replication
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override;
};
