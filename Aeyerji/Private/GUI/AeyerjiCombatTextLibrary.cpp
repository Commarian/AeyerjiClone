#include "GUI/AeyerjiCombatTextLibrary.h"

#include "AbilitySystemComponent.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Settings/AeyerjiGameUserSettings.h"
#include "GUI/AeyerjiStringLibrary.h"

namespace
{
	bool IsActorRelevantToLocalPlayer(const AActor* Actor, const APlayerController* LocalPC)
	{
		if (!Actor || !LocalPC)
		{
			return false;
		}

		const APawn* LocalPawn = LocalPC->GetPawn();
		const APlayerState* LocalPlayerState = LocalPC->PlayerState;
		if (Actor == LocalPC || Actor == LocalPawn || Actor == LocalPlayerState)
		{
			return true;
		}

		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (Pawn->GetController() == LocalPC || Pawn == LocalPawn || Pawn->GetPlayerState() == LocalPlayerState)
			{
				return true;
			}
		}

		if (Actor->GetInstigatorController() == LocalPC)
		{
			return true;
		}

		const AActor* Owner = Actor->GetOwner();
		for (int32 Depth = 0; Owner && Depth < 8; ++Depth)
		{
			if (Owner == LocalPC || Owner == LocalPawn || Owner == LocalPlayerState)
			{
				return true;
			}

			if (const APawn* OwnerPawn = Cast<APawn>(Owner))
			{
				if (OwnerPawn->GetController() == LocalPC || OwnerPawn == LocalPawn || OwnerPawn->GetPlayerState() == LocalPlayerState)
				{
					return true;
				}
			}

			Owner = Owner->GetOwner();
		}

		return false;
	}

	bool IsObjectRelevantToLocalPlayer(const UObject* Object, const APlayerController* LocalPC)
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			return IsActorRelevantToLocalPlayer(Actor, LocalPC);
		}

		if (const UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			return IsActorRelevantToLocalPlayer(Component->GetOwner(), LocalPC);
		}

		return false;
	}

	FText FormatDamageNumber(const float Value)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 0;
		Options.MaximumFractionalDigits = 0;
		return FText::AsNumber(FMath::Max(0, FMath::RoundToInt(Value)), &Options);
	}
}

EAeyerjiCombatTextMode UAeyerjiCombatTextLibrary::GetCombatTextMode()
{
	if (const UAeyerjiGameUserSettings* Settings = UAeyerjiGameUserSettings::GetAeyerjiGameUserSettings())
	{
		return Settings->GetCombatTextMode();
	}

	return EAeyerjiCombatTextMode::ImportantOnly;
}

bool UAeyerjiCombatTextLibrary::ExtractDamageResultFromCueParameters(
	const FGameplayCueParameters& Parameters,
	FAeyerjiDamageResult& OutResult)
{
	const FAeyerjiGameplayEffectContext* Context = FAeyerjiGameplayEffectContext::Extract(Parameters.EffectContext);
	if (!Context)
	{
		OutResult = FAeyerjiDamageResult();
		return false;
	}

	OutResult = Context->GetDamageResult();
	return true;
}

bool UAeyerjiCombatTextLibrary::ShouldShowResultTypeForMode(
	const EAeyerjiCombatTextResultType ResultType,
	const EAeyerjiCombatTextMode Mode)
{
	if (Mode == EAeyerjiCombatTextMode::Off)
	{
		return false;
	}

	if (Mode == EAeyerjiCombatTextMode::All)
	{
		return true;
	}

	return ResultType != EAeyerjiCombatTextResultType::Damage;
}

bool UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
	const EAeyerjiCombatTextResultType ResultType,
	const FAeyerjiDamageResult& DamageResult,
	const EAeyerjiCombatTextMode Mode,
	FText& OutText,
	FLinearColor& OutColor,
	float& OutScale,
	float& OutMagnitude)
{
	OutText = FText::GetEmpty();
	OutColor = FLinearColor::White;
	OutScale = 1.f;
	OutMagnitude = FMath::Max(0.f, DamageResult.FinalDamage);

	if (!ShouldShowResultTypeForMode(ResultType, Mode))
	{
		return false;
	}

	switch (ResultType)
	{
	case EAeyerjiCombatTextResultType::Damage:
		if (OutMagnitude <= 0.f)
		{
			return false;
		}
		OutText = FormatDamageNumber(OutMagnitude);
		OutColor = FLinearColor(0.92f, 0.94f, 1.f, 1.f);
		OutScale = 1.f;
		return true;

	case EAeyerjiCombatTextResultType::Critical:
		if (OutMagnitude <= 0.f)
		{
			return false;
		}
		{
			// CRIT prefix from GlobalStringTable.csv. Use Format for proper localization.
			const FText CritTemplate = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CombatCrit"));
			const FText DamageNum = FormatDamageNumber(OutMagnitude);
			OutText = CritTemplate.IsEmpty()
				? FText::FromString(FString::Printf(TEXT("CRIT %s"), *DamageNum.ToString()))
				: FText::Format(CritTemplate, DamageNum);
		}
		OutColor = FLinearColor(1.f, 0.68f, 0.08f, 1.f);
		OutScale = 1.22f;
		return true;

	case EAeyerjiCombatTextResultType::Dodged:
		OutText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CombatDodged"));
		OutColor = FLinearColor(0.45f, 0.82f, 1.f, 1.f);
		OutScale = 1.f;
		return true;

	case EAeyerjiCombatTextResultType::Staggered:
		OutText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CombatStagger"));
		OutColor = FLinearColor(0.82f, 0.50f, 1.f, 1.f);
		OutScale = 1.08f;
		return true;

	case EAeyerjiCombatTextResultType::Killing:
		OutText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CombatKill"));
		OutColor = FLinearColor(1.f, 0.16f, 0.08f, 1.f);
		OutScale = 1.30f;
		return true;

	default:
		return false;
	}
}

bool UAeyerjiCombatTextLibrary::ShouldDisplayForLocalPlayer(
	APlayerController* LocalPlayerController,
	AActor* TargetActor,
	const FGameplayCueParameters& Parameters)
{
	if (!LocalPlayerController || !LocalPlayerController->IsLocalController())
	{
		return false;
	}

	if (IsActorRelevantToLocalPlayer(TargetActor, LocalPlayerController))
	{
		return true;
	}

	if (IsActorRelevantToLocalPlayer(Parameters.GetInstigator(), LocalPlayerController))
	{
		return true;
	}

	if (IsActorRelevantToLocalPlayer(Parameters.GetEffectCauser(), LocalPlayerController))
	{
		return true;
	}

	if (IsObjectRelevantToLocalPlayer(Parameters.GetSourceObject(), LocalPlayerController))
	{
		return true;
	}

	const FGameplayEffectContextHandle& EffectContext = Parameters.EffectContext;
	if (IsActorRelevantToLocalPlayer(EffectContext.GetInstigator(), LocalPlayerController)
		|| IsActorRelevantToLocalPlayer(EffectContext.GetEffectCauser(), LocalPlayerController)
		|| IsActorRelevantToLocalPlayer(EffectContext.GetOriginalInstigator(), LocalPlayerController))
	{
		return true;
	}

	if (const UAbilitySystemComponent* OriginalASC = EffectContext.GetOriginalInstigatorAbilitySystemComponent())
	{
		return IsActorRelevantToLocalPlayer(OriginalASC->GetAvatarActor(), LocalPlayerController)
			|| IsActorRelevantToLocalPlayer(OriginalASC->GetOwnerActor(), LocalPlayerController);
	}

	return false;
}
