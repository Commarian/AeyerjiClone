#include "Abilities/GA_Death.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiCharacter.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "AeyerjiGameplayTags.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Enemy/EnemyParentNative.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Logging/AeyerjiLog.h"
#include "TimerManager.h"

namespace
{
	float ResolvePlayerDeathFinalizeDelay(const ACharacter* Character, const float DefaultDelay)
	{
		if (!Character || !Character->IsPlayerControlled())
		{
			return DefaultDelay;
		}

		UWorld* World = Character->GetWorld();
		if (!World)
		{
			return DefaultDelay;
		}

		for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
		{
			const AAeyerjiLevelDirector* Director = *It;
			if (Director && Director->IsActiveSurvivalRun())
			{
				return Director->ResolvePlayerRespawnDelaySeconds(DefaultDelay);
			}
		}

		return DefaultDelay;
	}
}

UGA_Death::UGA_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Identify this ability so death-state cancellation can exclude finalization.
	FGameplayTagContainer AssetTags = GetAssetTags();
	AssetTags.AddTag(AeyerjiTags::Ability_Death);
	SetAssetTags(AssetTags);

	// Passive, non-cancelable
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag    = AeyerjiTags::State_Dead;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
	AbilityTriggers.Add(Trigger);

	// Activate when tag appears, no input needed.
	/* Optional: tags the ability adds / blocks while active */
	ActivationOwnedTags.AddTagFast(AeyerjiTags::State_Dead);
}

void UGA_Death::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* /*TriggerEventData*/)
{
	bDeathFinalized = false;

	// Authority only; clients may see the State.Dead tag but should not run this ability.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	AJ_LOG(this, TEXT("Dying now"));
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacter* Char = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	if (!Char)
	{
		AJ_LOG(this, TEXT("GA_Death ActivateAbility: Avatar is not a character, aborting."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	UWorld* World = Char->GetWorld();
	if (!World)
	{
		AJ_LOG(this, TEXT("GA_Death ActivateAbility: World is null, aborting."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	const float FinalizeDelay = FMath::Max(ResolvePlayerDeathFinalizeDelay(Char, RespawnDelay), 0.f);
	if (FinalizeDelay <= 0.f)
	{
		Server_FinishDeath();
		return;
	}

	World->GetTimerManager().SetTimer(
		RespawnHandle, this, &UGA_Death::Server_FinishDeath,
		FinalizeDelay, /*bLoop=*/false);
}

void UGA_Death::EndAbility(const FGameplayAbilitySpecHandle Handle,
						   const FGameplayAbilityActorInfo* ActorInfo,
						   const FGameplayAbilityActivationInfo ActivationInfo,
						   bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnHandle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Death::Server_FinishDeath()
{
	// Prevent double-finalization if multiple timers/events fire.
	static const FName DeathFinalizeLogTag(TEXT("GA_Death"));
	if (bDeathFinalized)
	{
		AJ_LOG(this, TEXT("GA_Death: Server_FinishDeath already finalized; skipping."));
		return;
	}
	bDeathFinalized = true;

	ACharacter* DeadChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!DeadChar)
	{
		return;
	}
	if (!DeadChar->HasAuthority())
	{
		return; // Only the server can respawn/destroy.
	}

	if (UWorld* World = DeadChar->GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnHandle);
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);

	// Cancel any remaining abilities to avoid shutdown crashes during ASC destruction.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->CancelAllAbilities();
	}

	if (DeadChar->IsPlayerControlled())
	{
		if (AController* PC = DeadChar->GetController())
		{
			// Persist the dying pawn before possession swaps so the respawn load sees the latest inventory/action bar.
			if (const APawn* DeadPawn = Cast<APawn>(DeadChar))
			{
				if (AAeyerjiPlayerState* PS = DeadPawn->GetPlayerState<AAeyerjiPlayerState>())
				{
					PS->CommitCheckpointProfileFromPawn(EAeyerjiSaveCheckpointReason::DeathBeforeRespawn, DeadPawn, /*bBumpRevision=*/true);
				}
			}

			AJ_LOG(this, TEXT("Server_FinishDeath: restarting controller %s from pawn %s"),
				*GetNameSafe(PC), *GetNameSafe(DeadChar));

			DeadChar->SetLifeSpan(1.0f);
			if (UWorld* World = DeadChar->GetWorld())
			{
				AGameModeBase* GM = World->GetAuthGameMode<AGameModeBase>();
				APawn* NewPawn = nullptr;

				if (bUseCustomRespawn && GM)
				{
					AActor* StartSpot = nullptr;
					if (!RespawnPlayerStartTag.IsNone())
					{
						StartSpot = GM->FindPlayerStart(PC, RespawnPlayerStartTag.ToString());
					}
					if (!StartSpot)
					{
						StartSpot = GM->ChoosePlayerStart(PC);
					}

					FTransform SpawnTM = StartSpot ? StartSpot->GetActorTransform() : DeadChar->GetActorTransform();
					TSubclassOf<APawn> PawnClass = RespawnPawnClassOverride ? RespawnPawnClassOverride : TSubclassOf<APawn>(DeadChar->GetClass());

					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
					Params.Owner = PC;
					Params.Instigator = DeadChar;

					NewPawn = World->SpawnActor<APawn>(PawnClass, SpawnTM, Params);
					if (NewPawn)
					{
						PC->Possess(NewPawn);
					}
					else
					{
						AJ_LOG(this, TEXT("Server_FinishDeath: custom respawn failed to spawn pawn of class %s"),
							*GetNameSafe(PawnClass));
					}
				}

				if (!NewPawn && GM)
				{
					GM->RestartPlayer(PC);
					NewPawn = PC->GetPawn();
				}

				AJ_LOG(this, TEXT("Server_FinishDeath: controller %s now possesses %s"),
					*GetNameSafe(PC), *GetNameSafe(NewPawn));
			}
		}
	}
	else
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(DeadChar))
		{
			if (Enemy->TryReturnToOwningSpawnerPool())
			{
				return;
			}
		}

		// Destroy after cancelling abilities to avoid ASC teardown during active callbacks.
		DeadChar->Destroy();
	}
}
