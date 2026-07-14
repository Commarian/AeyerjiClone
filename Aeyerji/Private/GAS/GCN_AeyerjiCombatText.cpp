#include "GAS/GCN_AeyerjiCombatText.h"

#include "Components/AeyerjiCombatCueProfileComponent.h"
#include "GUI/AeyerjiCombatTextComponent.h"
#include "GUI/AeyerjiCombatTextLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UGCN_AeyerjiCombatText::UGCN_AeyerjiCombatText()
{
	IsOverride = false;
}

bool UGCN_AeyerjiCombatText::OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) const
{
	const bool bBurstHandled = Super::OnExecute_Implementation(Target, Parameters);

	if (!IsValid(Target))
	{
		return bBurstHandled;
	}

	UWorld* World = Target->GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		return bBurstHandled;
	}

	bool bProfileHandled = false;
	if (bPlayTargetCombatCueProfile)
	{
		if (UAeyerjiCombatCueProfileComponent* CueProfileComponent =
			Target->FindComponentByClass<UAeyerjiCombatCueProfileComponent>())
		{
			bProfileHandled = CueProfileComponent->PlayCombatCuePresentation(CombatTextType, Parameters);
		}
	}

	if (!bSpawnCombatText)
	{
		return bBurstHandled || bProfileHandled;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC || !PC->IsLocalController())
	{
		return bBurstHandled || bProfileHandled;
	}

	if (!UAeyerjiCombatTextLibrary::ShouldDisplayForLocalPlayer(PC, Target, Parameters))
	{
		return bBurstHandled || bProfileHandled;
	}

	FAeyerjiDamageResult DamageResult;
	UAeyerjiCombatTextLibrary::ExtractDamageResultFromCueParameters(Parameters, DamageResult);

	FText DisplayText;
	FLinearColor DisplayColor;
	float DisplayScale = 1.f;
	float Magnitude = 0.f;
	if (!UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
		CombatTextType,
		DamageResult,
		UAeyerjiCombatTextLibrary::GetCombatTextMode(),
		DisplayText,
		DisplayColor,
		DisplayScale,
		Magnitude))
	{
		return bBurstHandled || bProfileHandled;
	}

	UAeyerjiCombatTextComponent* CombatTextComponent = UAeyerjiCombatTextComponent::GetOrCreateForPlayerController(PC);
	if (!CombatTextComponent)
	{
		return bBurstHandled || bProfileHandled;
	}

	const bool bTextHandled = CombatTextComponent->ShowCombatText(
		Target,
		DisplayText,
		DisplayColor,
		DisplayScale * FMath::Max(0.01f, TextScaleMultiplier),
		CombatTextType,
		Magnitude);

	return bBurstHandled || bProfileHandled || bTextHandled;
}
