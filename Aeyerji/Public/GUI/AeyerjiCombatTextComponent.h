#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "AeyerjiCombatTextComponent.generated.h"

class APlayerController;
class UW_AeyerjiCombatText;

/** Local client component that owns screen-space floating combat text widgets. */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiCombatTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiCombatTextComponent();

	/** Returns the existing local component on the controller or creates one at runtime. */
	static UAeyerjiCombatTextComponent* GetOrCreateForPlayerController(APlayerController* PlayerController);

	/** Creates a single floating combat text widget above the target actor. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Combat Text")
	bool ShowCombatText(
		AActor* TargetActor,
		const FText& Text,
		FLinearColor Color,
		float Scale,
		EAeyerjiCombatTextResultType ResultType,
		float Magnitude);

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text")
	TSubclassOf<UW_AeyerjiCombatText> WidgetClass;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text")
	int32 ZOrder = 80;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text", meta=(ClampMin="0.05"))
	float LifetimeSeconds = 1.05f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text", meta=(ClampMin="0.0"))
	float FloatDistancePixels = 56.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text")
	FVector WorldOffset = FVector(0.f, 0.f, 135.f);

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text")
	FVector2D ScreenOffset = FVector2D(0.f, -12.f);

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text", meta=(ClampMin="0.0"))
	float HorizontalSpreadPixels = 28.f;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text", meta=(ClampMin="1"))
	int32 MaxActiveEntries = 48;

	UPROPERTY(EditAnywhere, Category="Aeyerji|Combat Text", meta=(ClampMin="0.0"))
	float MaxDrawDistance = 10000.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FActiveCombatText
	{
		TWeakObjectPtr<AActor> TargetActor;
		TWeakObjectPtr<UW_AeyerjiCombatText> Widget;
		FVector WorldOffset = FVector::ZeroVector;
		FVector2D InitialScreenOffset = FVector2D::ZeroVector;
		FVector2D LastScreenPosition = FVector2D::ZeroVector;
		float AgeSeconds = 0.f;
		float LifetimeSeconds = 1.f;
		float FloatDistancePixels = 0.f;
		bool bHasLastScreenPosition = false;
	};

	TArray<FActiveCombatText> ActiveTexts;

	APlayerController* GetLocalPlayerController() const;
	bool ProjectToScreen(const FVector& WorldLocation, FVector2D& OutPosition) const;
	int32 CountActiveTextsForTarget(const AActor* TargetActor) const;
	void RemoveActiveTextAt(int32 Index);
	void TrimToMaxEntries();
};
