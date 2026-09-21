#include "Abilities/Enemy/GA_EnemyBombardment.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/AeyerjiBombardment.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameplayEffect.h"

UGA_EnemyBombardment::UGA_EnemyBombardment()
{
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
	SetAssetTags(FGameplayTagContainer(AeyerjiTags::Ability_Primary_Ranged_Bombardment));
	ActivationOwnedTags.AddTag(AeyerjiTags::Ability_Primary);
	ActivationOwnedTags.AddTag(AeyerjiTags::State_Ability_Casting);
	ActivationBlockedTags.AddTag(AeyerjiTags::State_Ability_Casting);
	ActivationBlockedTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);
	BombardmentCooldownTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);
	DefaultDamageTypeTag = AeyerjiTags::DamageType_Physical;
	// A readable fixed blast has deterministic damage by default; armor and immunity still use the normal GAS execution.
	DefaultDamageRules = FAeyerjiDamageRuleConfig();
	BombardmentClass = AAeyerjiBombardment::StaticClass();
}

void UGA_EnemyBombardment::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bEndingBombardment = false;
	AEnemyParentNative* Enemy = ActorInfo ? Cast<AEnemyParentNative>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->IsEncounterCombatActive() || !ASC
		|| IsOwnerDead(ActorInfo) || !BombardmentClass || !AAeyerjiBombardment::CanStartBombardment(Enemy->GetWorld()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	AEnemyAIController* Controller = Cast<AEnemyAIController>(Enemy->GetController());
	AActor* Target = TriggerEventData ? const_cast<AActor*>(TriggerEventData->Target.Get()) : nullptr;
	if (!Target && Controller)
	{
		Target = Controller->GetTargetActor();
	}
	const float Range = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	if (!AAeyerjiBombardment::IsDamageTarget(Enemy, Target) || !FMath::IsFinite(Range) || Range <= 0.f
		|| FVector::DistSquared2D(Enemy->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(static_cast<double>(Range)))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FVector Feet = Target->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		Feet.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	FCollisionQueryParams Trace(SCENE_QUERY_STAT(BombardmentAim), true, Enemy);
	Trace.AddIgnoredActor(Target);
	FHitResult Ground;
	const bool bGroundFound = Enemy->GetWorld()->LineTraceSingleByObjectType(Ground,
		Feet + FVector(0, 0, 100), Feet - FVector(0, 0, 500), FCollisionObjectQueryParams(ECC_WorldStatic), Trace);
	if (!bGroundFound || Enemy->GetWorld()->LineTraceTestByChannel(Enemy->GetActorLocation(),
		Target->GetActorLocation(), ECC_Visibility, Trace))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(UGE_DamagePhysical::StaticClass(), GetAbilityLevel());
	const float Damage = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
	if (!DamageSpec.IsValid() || !FMath::IsFinite(Damage) || Damage < 0.f || !FMath::IsFinite(DamageScalar)
		|| !FMath::IsFinite(WarningSeconds) || !FMath::IsFinite(BlastRadius) || !FMath::IsFinite(AttackIntervalSeconds))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	ApplyDamageTypeTagToSpec(DamageSpec, DefaultDamageTypeTag);
	ApplyDefaultDamageRulesToSpec(DamageSpec);
	DamageSpec.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_Instant, Damage * FMath::Clamp(DamageScalar, 0.f, 10.f));
	FActorSpawnParameters Spawn;
	Spawn.Owner = Enemy;
	Spawn.Instigator = Enemy;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAeyerjiBombardment* Zone = Enemy->GetWorld()->SpawnActor<AAeyerjiBombardment>(BombardmentClass,
		Ground.ImpactPoint, FRotator::ZeroRotator, Spawn);
	if (!Zone)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	ActiveBombardment = Zone;
	Zone->OnResolved.AddUObject(this, &ThisClass::HandleBombardmentResolved);
	if (!Zone->InitializeBombardment(Enemy, Target, Ground.ImpactPoint,
		FMath::Clamp(BlastRadius, 50.f, 600.f), FMath::Clamp(WarningSeconds, 0.75f, 2.f), DamageSpec))
	{
		Zone->OnResolved.RemoveAll(this);
		Zone->Destroy();
		ActiveBombardment.Reset();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	if (Controller)
	{
		Controller->StopMovement();
	}
	Enemy->GetCharacterMovement()->StopMovementImmediately();
	if (CastMontage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, CastMontage);
		if (MontageTask)
		{
			MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &ThisClass::HandleMontageInterrupted);
			MontageTask->ReadyForActivation();
		}
	}
}

void UGA_EnemyBombardment::ApplyCooldown(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo) const
{
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(UGE_AeyerjiAbilityCooldown::StaticClass(), GetAbilityLevel());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_CooldownSeconds,
			FMath::IsFinite(AttackIntervalSeconds) ? FMath::Clamp(AttackIntervalSeconds, 3.f, 30.f) : 5.f);
		Spec.Data->DynamicGrantedTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
}

const FGameplayTagContainer* UGA_EnemyBombardment::GetCooldownTags() const
{
	return &BombardmentCooldownTags;
}

void UGA_EnemyBombardment::HandleBombardmentResolved(bool bCancelled)
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancelled);
}

void UGA_EnemyBombardment::HandleMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UGA_EnemyBombardment::EndAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bEndingBombardment)
	{
		return;
	}
	bEndingBombardment = true;
	if (MontageTask)
	{
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
	if (AAeyerjiBombardment* Zone = ActiveBombardment.Get())
	{
		Zone->OnResolved.RemoveAll(this);
		Zone->CancelBombardment();
	}
	ActiveBombardment.Reset();
	// The shared StateTree waits for completion even when no target/capacity was available or the attack was interrupted.
	if (ActorInfo && ActorInfo->IsNetAuthority() && ActorInfo->AbilitySystemComponent.IsValid())
	{
		FGameplayEventData Event;
		Event.EventTag = AeyerjiTags::Event_PrimaryAttack_Completed;
		Event.Instigator = ActorInfo->AvatarActor.Get();
		Event.Target = ActorInfo->AvatarActor.Get();
		ActorInfo->AbilitySystemComponent->HandleGameplayEvent(Event.EventTag, &Event);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
