#include "Director/AeyerjiWorldDirector.h"

#include "../AeyerjiGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "TimerManager.h"

AAeyerjiWorldDirector::AAeyerjiWorldDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAeyerjiWorldDirector::BeginPlay()
{
	Super::BeginPlay();

	if (!bEnterZoneOnBeginPlay)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (bOnlyRunOnAuthority && !HasAuthority())
	{
		return;
	}

	World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiWorldDirector::ExecuteDeferredStartupFlow);
}

void AAeyerjiWorldDirector::ExecuteDeferredStartupFlow()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (bOnlyRunOnAuthority && !HasAuthority())
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GameInstance->GetSubsystem<UAeyerjiStreamingSubsystem>())
		{
			FName StartupZoneId = StartZoneId;
			const bool bUsedPendingStartupOverride = StreamingSubsystem->TakePendingStartupZoneOverride(StartupZoneId);
			if (!bUsedPendingStartupOverride
				&& bPreferSavedZone
				&& !StreamingSubsystem->GetCurrentZoneId().IsNone())
			{
				StartupZoneId = StreamingSubsystem->GetCurrentZoneId();
			}

			UE_LOG(LogTemp, Display,
				TEXT("AeyerjiWorldDirector: StartupZone=%s UsedPendingOverride=%d PreferSavedZone=%d"),
				*StartupZoneId.ToString(),
				bUsedPendingStartupOverride ? 1 : 0,
				bPreferSavedZone ? 1 : 0);

			if (StartupZoneId.IsNone())
			{
				FZoneDef DefaultZoneDef;
				if (StreamingSubsystem->GetZoneDefinition(FName(TEXT("Zone.Menu")), DefaultZoneDef))
				{
					StartupZoneId = DefaultZoneDef.ZoneId;
				}
			}

			if (bUseServerWorldFlow)
			{
				if (AAeyerjiGameState* AeyerjiGameState = GetWorld()->GetGameState<AAeyerjiGameState>())
				{
					if (!StartupZoneId.IsNone())
					{
						if (AeyerjiGameState->Server_BeginWorldTransition(StartupZoneId))
						{
							return;
						}
					}
				}
			}

			StreamingSubsystem->EnterStartupZone(StartupZoneId, bPreferSavedZone);
		}
	}
}
