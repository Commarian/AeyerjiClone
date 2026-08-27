// Fill out your copyright notice in the Description page of Project Settings.


#include "AeyerjiGameInstance.h"

#include "AeyerjiPlayerState.h"
#include "AeyerjiPlayerController.h"
#include "AeyerjiGameState.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"
#include "Systems/AeyerjiDifficultyTuning.h"

void UAeyerjiGameInstance::SetDifficultySlider(float NewValue)
{
	const int32 DerivedWorldTier = UAeyerjiDifficultySettings::DifficultySliderToWorldTier(NewValue);
	UE_LOG(LogTemp, Display,
		TEXT("GameInstance::SetDifficultySlider Requested=%.2f DerivedWorldTier=%d"),
		NewValue,
		DerivedWorldTier);
	SetWorldTier(DerivedWorldTier);
}

void UAeyerjiGameInstance::SetWorldTier(int32 NewWorldTier)
{
	UWorld* World = GetWorld();
	const int32 PreviousWorldTier = WorldTier;
	const float PreviousDifficulty = DifficultySlider;

	WorldTier = FMath::Clamp(NewWorldTier, 0, UAeyerjiDifficultySettings::WorldTierMax);
	bHasWorldTierSelection = true;

	const float NewDifficultySlider = UAeyerjiDifficultySettings::WorldTierToDifficultySlider(WorldTier);
	if (!FMath::IsNearlyEqual(DifficultySlider, NewDifficultySlider))
	{
		DifficultySlider = NewDifficultySlider;
	}
	bHasDifficultySelection = true;

	UE_LOG(LogTemp, Display,
		TEXT("GameInstance::SetWorldTier Requested=%d Applied=%d DifficultySlider=%.2f (PrevWorldTier=%d PrevSlider=%.2f NetMode=%d World=%s)"),
		NewWorldTier, WorldTier, DifficultySlider, PreviousWorldTier, PreviousDifficulty,
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		*GetNameSafe(World));

	if (World && World->GetNetMode() == NM_Client)
	{
		if (AAeyerjiPlayerController* LocalPC = Cast<AAeyerjiPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
		{
			UE_LOG(LogTemp, Display,
				TEXT("GameInstance::SetWorldTier forwarding to server via RPC (Controller=%s Value=%d)"),
				*GetNameSafe(LocalPC),
				WorldTier);
			LocalPC->Server_SetWorldTier(WorldTier);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GameInstance::SetWorldTier could not find local AeyerjiPlayerController for server sync."));
		}
	}

	PersistDifficultySelectionToSave();
}

void UAeyerjiGameInstance::ApplySavedWorldTier(int32 NewWorldTier)
{
	UWorld* World = GetWorld();
	const int32 PreviousWorldTier = WorldTier;
	const float PreviousDifficulty = DifficultySlider;

	WorldTier = FMath::Clamp(NewWorldTier, 0, UAeyerjiDifficultySettings::WorldTierMax);
	bHasWorldTierSelection = true;

	const float NewDifficultySlider = UAeyerjiDifficultySettings::WorldTierToDifficultySlider(WorldTier);
	if (!FMath::IsNearlyEqual(DifficultySlider, NewDifficultySlider))
	{
		DifficultySlider = NewDifficultySlider;
	}
	bHasDifficultySelection = true;

	UE_LOG(LogTemp, Display,
		TEXT("GameInstance::ApplySavedWorldTier Requested=%d Applied=%d DifficultySlider=%.2f (PrevWorldTier=%d PrevSlider=%.2f NetMode=%d World=%s)"),
		NewWorldTier,
		WorldTier,
		DifficultySlider,
		PreviousWorldTier,
		PreviousDifficulty,
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		*GetNameSafe(World));
}

float UAeyerjiGameInstance::GetDifficultyScale() const
{
	const float Normalized = DifficultySlider / UAeyerjiDifficultySettings::DifficultySliderMax;
	return FMath::Clamp(Normalized, 0.f, 1.f);
}

void UAeyerjiGameInstance::PersistDifficultySelectionToSave()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("PersistDifficultySelectionToSave: No world available. Difficulty=%.2f WorldTier=%d"), DifficultySlider, WorldTier);
		return;
	}

	AAeyerjiPlayerState* AeyerjiPS = nullptr;
	APlayerController* CandidatePC = UGameplayStatics::GetPlayerController(World, 0);
	if (!CandidatePC)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				CandidatePC = PC;
				break;
			}
		}
	}

	if (CandidatePC)
	{
		AeyerjiPS = CandidatePC->GetPlayerState<AAeyerjiPlayerState>();
	}

	if (World->GetNetMode() != NM_Client && (!CandidatePC || !CandidatePC->IsLocalController()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("PersistDifficultySelectionToSave: No local owner is available for persistence. Difficulty=%.2f WorldTier=%d NetMode=%d World=%s Controller=%s"),
			DifficultySlider,
			WorldTier,
			static_cast<int32>(World->GetNetMode()),
			*GetNameSafe(World),
			*GetNameSafe(CandidatePC));
		return;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		UE_LOG(LogTemp, Error,
			TEXT("PersistDifficultySelectionToSave: Save manager unavailable. Difficulty=%.2f WorldTier=%d"),
			DifficultySlider,
			WorldTier);
		return;
	}

	if (!SaveManager->MutateCachedProfileDifficulty(DifficultySlider, WorldTier, AeyerjiPS))
	{
		UE_LOG(LogTemp, Error,
			TEXT("PersistDifficultySelectionToSave: Save manager mutation failed. Difficulty=%.2f WorldTier=%d PlayerState=%s"),
			DifficultySlider,
			WorldTier,
			*GetNameSafe(AeyerjiPS));
	}
}

bool UAeyerjiGameInstance::StartGameplaySession(const bool bCampaignMode)
{
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		return StreamingSubsystem->StartGameplaySession(bCampaignMode);
	}

	return false;
}

bool UAeyerjiGameInstance::RequestZoneTransition(const FName ZoneId)
{
	if (ZoneId.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("GameInstance::RequestZoneTransition failed - ZoneId is None."));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameInstance::RequestZoneTransition failed - World missing for ZoneId=%s."), *ZoneId.ToString());
		return false;
	}

	UE_LOG(LogTemp, Display,
		TEXT("GameInstance::RequestZoneTransition ZoneId=%s NetMode=%d World=%s"),
		*ZoneId.ToString(),
		static_cast<int32>(World->GetNetMode()),
		*GetNameSafe(World));

	if (World->GetNetMode() != NM_Client)
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			const bool bStarted = GS->Server_BeginWorldTransition(ZoneId);
			UE_LOG(LogTemp, Display,
				TEXT("GameInstance::RequestZoneTransition authority path %s for ZoneId=%s"),
				bStarted ? TEXT("started") : TEXT("failed"),
				*ZoneId.ToString());
			return bStarted;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("GameInstance::RequestZoneTransition authority path failed - GameState missing for ZoneId=%s"),
			*ZoneId.ToString());
		return false;
	}

	APlayerController* BasePC = UGameplayStatics::GetPlayerController(World, 0);
	if (AAeyerjiPlayerController* LocalPC = Cast<AAeyerjiPlayerController>(BasePC))
	{
		LocalPC->Server_RequestZoneTransition(ZoneId);
		UE_LOG(LogTemp, Display,
			TEXT("GameInstance::RequestZoneTransition client path sent RPC via %s for ZoneId=%s"),
			*GetNameSafe(LocalPC),
			*ZoneId.ToString());
		return true;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("GameInstance::RequestZoneTransition client path failed - Controller class is %s (expected AAeyerjiPlayerController)"),
		*GetNameSafe(BasePC ? BasePC->GetClass() : nullptr));
	return false;
}

bool UAeyerjiGameInstance::ReturnToMainMenuMap()
{
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		return StreamingSubsystem->TravelToMainMenu();
	}

	return false;
}

bool UAeyerjiGameInstance::EnterStreamingZone(const FName ZoneId)
{
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		return StreamingSubsystem->EnterZone(ZoneId);
	}

	return false;
}

UAeyerjiStreamingSubsystem* UAeyerjiGameInstance::GetStreamingSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UAeyerjiStreamingSubsystem>();
		}
	}

	return nullptr;
}
