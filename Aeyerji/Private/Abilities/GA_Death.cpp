#include "Abilities/GA_Death.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiGameplayTags.h"
#include "Abilities/AeyerjiRagdollHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "Logging/AeyerjiLog.h"

UGA_Death::UGA_Death()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

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
	if (Char)
	{
		// Stop movement & input
		if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
		Char->DisableInput(nullptr);

		if (const bool bHasAnim = (DeathMontage && ActorInfo->AnimInstance.IsValid()))
		{
			// Play at normal speed
			float PlayLen = ActorInfo->AnimInstance->Montage_Play(DeathMontage, /*PlayRate=*/1.f);
			if (PlayLen <= 0.f)
			{
				// Fallback: no anim actually played → ragdoll now
				FAeyerjiRagdollHelpers::StartRagdoll(Char);
				// give viewers time to see the fall
				PlayLen = 1.25f;
			}
			// When montage ends, start ragdoll
			FOnMontageEnded OnEnd;
			OnEnd.BindLambda([this, Char](UAnimMontage*, bool /*bInterrupted*/)
			{
				FAeyerjiRagdollHelpers::StartRagdoll(Char);
			});
			ActorInfo->AnimInstance->Montage_SetEndDelegate(OnEnd, DeathMontage);

			// Schedule finish AFTER montage + a short ragdoll display
			const float RagdollViewSeconds = FMath::Max(RespawnDelay, 0.f);
			GetWorld()->GetTimerManager().SetTimer(
				RespawnHandle, this, &UGA_Death::Server_FinishDeath,
				PlayLen + RagdollViewSeconds, /*bLoop=*/false);
		}
		else
		{
			// No montage → ragdoll immediately, then delay cleanup a bit
			FAeyerjiRagdollHelpers::StartRagdoll(Char);
			const float RagdollViewSeconds = FMath::Max(RespawnDelay, 0.f);
			GetWorld()->GetTimerManager().SetTimer(
				RespawnHandle, this, &UGA_Death::Server_FinishDeath,
				RagdollViewSeconds, /*bLoop=*/false);
		}
	}
	else
	{
		AJ_LOG(this, TEXT("GA_Death ActivateAbility: Avatar is not a character, aborting."));
		return;
	}

	// Broadcast finished after 5 s - AI corpses cleaned up, players will respawn.
	FTimerDelegate EndAbilityDelegate;
	EndAbilityDelegate.BindUObject(
		this,
		&UGA_Death::OnDeathTimeout,               // helper we’ll add below
		Handle,                                  // pass-through params
		ActivationInfo
	);

	FTimerHandle Timer;
	Char->GetWorldTimerManager().SetTimer(Timer, EndAbilityDelegate, 5.f, /*bLoop=*/false);
}

void UGA_Death::EndAbility(const FGameplayAbilitySpecHandle Handle,
						   const FGameplayAbilityActorInfo* ActorInfo,
						   const FGameplayAbilityActivationInfo ActivationInfo,
						   bool bReplicateEndAbility, bool bWasCancelled)
{
	// Only do ability-level cleanup here (detach VFX, stop cues, etc.)
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Death::Server_FinishDeath()
{
	ACharacter* DeadChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!DeadChar)
	{
		return;
	}
	if (!DeadChar->HasAuthority())
	{
		return; // Only the server can respawn/destroy.
	}

	// If you want EndAbility to replicate the final state you can call it here,
	// but keep it non-destructive as above.
	Super::EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);

	if (DeadChar->IsPlayerControlled())
	{
		if (AController* PC = DeadChar->GetController())
		{
			DeadChar->SetLifeSpan(1.0f);
			if (AGameModeBase* GM = DeadChar->GetWorld()->GetAuthGameMode<AGameModeBase>())
			{
				GM->RestartPlayer(PC);
			}
		}
	}
	else
	{
		DeadChar->Destroy();
	}
}
