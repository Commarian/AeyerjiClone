// Fill out your copyright notice in the Description page of Project Settings.


#include "AeyerjiGameInstance.h"

#include "AbilitySystemInterface.h"
#include "AeyerjiGameMode.h"
#include "AeyerjiPlayerState.h"
#include "AeyerjiCharacter.h"
#include "CharacterStatsLibrary.h"
#include "AeyerjiSaveGame.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Kismet/GameplayStatics.h"

void UAeyerjiGameInstance::Shutdown()
{
	// Iterate every PlayerState still alive (listen server / standalone)
	
	/*for (TObjectIterator<AAeyerjiPlayerState> It; It; ++It)
	{
		
		AAeyerjiPlayerState* PS = *It;
		if (!PS->GetWorld()->IsGameWorld()) continue;

		const FString Slot = UCharacterStatsLibrary::MakeStableCharSlotName(PS);
		UAeyerjiSaveGame* Data = Cast<UAeyerjiSaveGame>(
				    UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
		
		// We don't need to look for attributes in PlayerState anymore as they're in the Character
		UE_LOG(LogTemp, Log, TEXT("Saving from SHUTDOWN"));
		UCharacterStatsLibrary::SaveAeyerjiChar(Data, PS, Slot);
		
	}*/

	Super::Shutdown();        // always last
}
