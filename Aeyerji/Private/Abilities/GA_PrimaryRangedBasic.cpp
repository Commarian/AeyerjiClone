#include "Abilities/GA_PrimaryRangedBasic.h"

#include "AbilitySystemComponent.h"
#include "Abilities/AbilityTeamUtils.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Projectiles/AeyerjiProjectile_RangedBasic.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryRangedGA, Verbose, All);

namespace
{
	constexpr float KMinAttackSpeed = 0.01f;

}

UGA_PrimaryRangedBasic::UGA_PrimaryRangedBasic()
	: MontageTask(nullptr)
	, bCompletionBroadcasted(false)
	, bHasCachedTriggerEventData(false)
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bServerRespectsRemoteAbilityCancellation = true;

	FGameplayTagContainer Tags = GetAssetTags();
	Tags.AddTag(AeyerjiTags::Ability_Primary);
	Tags.AddTag(AeyerjiTags::Ability_Primary_Ranged_Basic);
	SetAssetTags(Tags);

	ActivationOwnedTags.AddTag(AeyerjiTags::Ability_Primary);
	ActivationBlockedTags.Reset();
	ActivationBlockedTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);

	if (!DamageSetByCallerTag.IsValid())
	{
		static const FName DamageTagName(TEXT("Data.Damage"));
		DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(DamageTagName, /*ErrorIfNotFound=*/false);
	}
}

void UGA_PrimaryRangedBasic::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                             const FGameplayAbilityActorInfo* ActorInfo,
                                             const FGameplayAbilityActivationInfo ActivationInfo,
                                             const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ActivateAbility: Missing actor info or avatar. Ending ability."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CheckCost(Handle, ActorInfo) || !CheckCooldown(Handle, ActorInfo))
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ActivateAbility: Cost or cooldown check failed. Ending ability."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ActivateAbility: CommitAbility failed. Ending ability."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bCompletionBroadcasted = false;

	bHasCachedTriggerEventData = TriggerEventData != nullptr;
	if (TriggerEventData)
	{
		CachedTriggerEventData = *TriggerEventData;
	}

	const float AttackSpeed = ResolveAttackSpeed(ActorInfo);
	StartMontage(AttackSpeed);

	const float ProjectileSpeed = ResolveProjectileSpeed(ActorInfo);
	ScheduleProjectileSpawn(ProjectileSpeed);
}

void UGA_PrimaryRangedBasic::CancelAbility(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo,
                                           bool bReplicateCancelAbility)
{
	ClearMontageTask();

	if (ShouldProcessServerLogic())
	{
		if (ActiveProjectile.IsValid())
		{
			ActiveProjectile->OnProjectileImpact.RemoveAll(this);
			ActiveProjectile->OnProjectileExpired.RemoveAll(this);
			ActiveProjectile->Destroy();
			ActiveProjectile.Reset();
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnDelayHandle);
		}
		bHasCachedTriggerEventData = false;
	}

	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UGA_PrimaryRangedBasic::EndAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo,
                                        bool bReplicateEndAbility,
                                        bool bWasCancelled)
{
	ClearMontageTask();

	if (ShouldProcessServerLogic())
	{
		if (ActiveProjectile.IsValid())
		{
			ActiveProjectile->OnProjectileImpact.Clear();
			ActiveProjectile->OnProjectileExpired.Clear();
			ActiveProjectile.Reset();
		}

		if (!bWasCancelled)
		{
			BroadcastPrimaryAttackComplete();
		}

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnDelayHandle);
		}
		bHasCachedTriggerEventData = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_PrimaryRangedBasic::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (!CooldownGameplayEffectClass)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ApplyCooldown: Missing cooldown GE class. Falling back to super."));
		Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));
	if (!SpecHandle.IsValid())
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ApplyCooldown: Failed to build cooldown spec."));
		return;
	}

	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}

void UGA_PrimaryRangedBasic::StartMontage(float AttackSpeed)
{
	UAnimMontage* MontageToPlay = nullptr;
	if (const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo())
	{
		MontageToPlay = SelectAttackMontage(*Info);
	}

	if (!MontageToPlay)
	{
		return;
	}

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = Info ? Info->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("StartMontage: No anim instance on avatar."));
		return;
	}

	const float PlayRate = FMath::Max(AttackSpeed / FMath::Max(BaselineAttackSpeed, KMinAttackSpeed), KMinAttackSpeed);

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, MontageToPlay, PlayRate);
	if (!MontageTask)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("StartMontage: Failed to create montage task."));
		return;
	}

	MontageTask->OnCompleted.AddDynamic(this, &UGA_PrimaryRangedBasic::OnMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UGA_PrimaryRangedBasic::OnMontageCompleted);
	MontageTask->OnInterrupted.AddDynamic(this, &UGA_PrimaryRangedBasic::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UGA_PrimaryRangedBasic::OnMontageCancelled);
	MontageTask->ReadyForActivation();
}

void UGA_PrimaryRangedBasic::ClearMontageTask()
{
	if (MontageTask)
	{
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
}

void UGA_PrimaryRangedBasic::ScheduleProjectileSpawn(float ProjectileSpeed)
{
	if (!ShouldProcessServerLogic())
	{
		return;
	}

	if (SpawnDelaySeconds <= 0.f)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ScheduleProjectileSpawn: SpawnProjectileNow(ProjectileSpeed);"));
		SpawnProjectileNow(ProjectileSpeed);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// Delay is intentionally server-driven so the projectile leaves in sync with the montage.
		FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &UGA_PrimaryRangedBasic::SpawnProjectileNow, ProjectileSpeed);
		World->GetTimerManager().SetTimer(SpawnDelayHandle, Delegate, SpawnDelaySeconds, /*bLoop=*/false);
	}
}

void UGA_PrimaryRangedBasic::SpawnProjectileNow(float ProjectileSpeed)
{
	if (!ShouldProcessServerLogic() || !ProjectileClass)
	{
		return;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		return;
	}

	const FTransform SpawnTransform = ComputeMuzzleTransform(ActorInfo);
	const FVector SpawnLocation = SpawnTransform.GetLocation();
	const FRotator SpawnRotation = SpawnTransform.Rotator();
	UE_LOG(LogPrimaryRangedGA, Verbose, TEXT("SpawnProjectileNow: Location=%s Rotation(Pitch=%.2f Yaw=%.2f Roll=%.2f) Speed=%.2f"),
		*SpawnLocation.ToString(),
		SpawnRotation.Pitch,
		SpawnRotation.Yaw,
		SpawnRotation.Roll,
		ProjectileSpeed);

	const FGameplayEventData* TriggerEventData = bHasCachedTriggerEventData ? &CachedTriggerEventData : nullptr;
	AActor* TargetActor = ResolveTargetActor(ActorInfo, TriggerEventData);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = ActorInfo->AvatarActor.Get();
	Params.Instigator = ActorInfo->AvatarActor.IsValid() ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	// Allow the projectile to spawn even if the muzzle is blocked, and nudge it out if possible.
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ensureMsgf(ProjectileClass->IsChildOf(AAeyerjiProjectile_RangedBasic::StaticClass()),
           TEXT("ProjectileClass %s is not a AAeyerjiProjectile_RangedBasic"), *GetNameSafe(ProjectileClass));
		   
	AAeyerjiProjectile_RangedBasic* Projectile = World->SpawnActor<AAeyerjiProjectile_RangedBasic>(
		ProjectileClass,
		SpawnTransform,
		Params);

	if (!Projectile)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("SpawnProjectileNow: Failed to spawn projectile of class %s."),
			*GetNameSafe(ProjectileClass));
		return;
	}

	Projectile->InitializeProjectile(this, TargetActor, ProjectileSpeed, StationaryTargetSpeedTolerance);
	Projectile->OnProjectileImpact.AddUObject(this, &UGA_PrimaryRangedBasic::HandleProjectileImpact);
	// Both impact and expiry need to notify the ability so state can be cleaned up on the server.
	Projectile->OnProjectileExpired.AddUObject(this, &UGA_PrimaryRangedBasic::HandleProjectileExpired);

	ActiveProjectile = Projectile;

	bHasCachedTriggerEventData = false;
}

void UGA_PrimaryRangedBasic::HandleProjectileImpact(AActor* HitActor, const FHitResult& Hit)
{
	if (!HitActor)
	{
		return;
	}

	if (!HitActor || HitActor == GetAvatarActorFromActorInfo())
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("HandleProjectileImpact: Hit actor %s has no ASC; damage skipped."), *GetNameSafe(HitActor));
		return;
	}

	if (AbilityTeamUtils::AreOnSameTeam(GetAvatarActorFromActorInfo(), HitActor))
	{
		// Friendly fire is suppressed at this layer to avoid spending damage budget or ending the montage.
		UE_LOG(LogPrimaryRangedGA, VeryVerbose, TEXT("HandleProjectileImpact: Ignoring hit on same-team actor %s."), *GetNameSafe(HitActor));
		return;
	}

	const float RemainingHP = TargetASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	UE_LOG(LogPrimaryRangedGA, Verbose, TEXT("HandleProjectileImpact: Target %s ASC=%s RemainingHP=%.2f"),
		*GetNameSafe(HitActor),
		*GetNameSafe(TargetASC),
		RemainingHP);

	FGameplayAbilityTargetDataHandle TargetData;
	TargetData.Add(new FGameplayAbilityTargetData_SingleTargetHit(Hit));

	if (ShouldProcessServerLogic())
	{
		ApplyDamageToTarget(TargetData);
		BP_HandleRangedDamage(HitActor, TargetData);
		BroadcastPrimaryAttackComplete();
	}

	if (IsLocallyPredicting())
	{
		BP_HandlePredictedImpact(HitActor, TargetData);
	}
}

void UGA_PrimaryRangedBasic::HandleProjectileExpired()
{
	if (!ShouldProcessServerLogic())
	{
		return;
	}

	if (ActiveProjectile.IsValid())
	{
		ActiveProjectile->OnProjectileImpact.RemoveAll(this);
		ActiveProjectile->OnProjectileExpired.RemoveAll(this);
		ActiveProjectile.Reset();
	}

	if (!bCompletionBroadcasted)
	{
		BroadcastPrimaryAttackComplete();
	}

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_PrimaryRangedBasic::ApplyDamageToTarget(const FGameplayAbilityTargetDataHandle& TargetData)
{
	if (TargetData.Num() == 0)
	{
		return;
	}

	FGameplayEffectSpecHandle DamageSpec;
	if (!BuildDamageSpec(DamageSpec))
	{
		return;
	}

	if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
	{
		const float DamageMagnitude = DamageSetByCallerTag.IsValid()
			? DamageSpec.Data.Get()->GetSetByCallerMagnitude(DamageSetByCallerTag, /*WarnIfMissing=*/false, 0.f)
			: 0.f;
		UE_LOG(LogPrimaryRangedGA, Verbose, TEXT("ApplyDamageToTarget: Damage=%.2f Targets=%d"), DamageMagnitude, TargetData.Num());
	}

	const TArray<FActiveGameplayEffectHandle> AppliedHandles =
		ApplyGameplayEffectSpecToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, DamageSpec, TargetData);

	if (AppliedHandles.Num() == 0)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("ApplyDamageToTarget: Spec built but no effects were applied."));
	}
}

bool UGA_PrimaryRangedBasic::BuildDamageSpec(FGameplayEffectSpecHandle& OutSpecHandle) const
{
	TSubclassOf<UGameplayEffect> DamageClass = nullptr;
	if (DamageEffectClass.IsValid())
	{
		DamageClass = DamageEffectClass.Get();
	}
	else if (DamageEffectClass.ToSoftObjectPath().IsValid())
	{
		DamageClass = DamageEffectClass.LoadSynchronous();
	}

	if (!DamageClass)
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("BuildDamageSpec: DamageEffectClass not set."));
		return false;
	}

	OutSpecHandle = MakeOutgoingGameplayEffectSpec(DamageClass, GetAbilityLevel());
	if (!OutSpecHandle.IsValid())
	{
		UE_LOG(LogPrimaryRangedGA, Warning, TEXT("BuildDamageSpec: Failed to create outgoing spec."));
		return false;
	}

	if (DamageSetByCallerTag.IsValid())
	{
		const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
		const UAeyerjiAttributeSet* AttrSet = Info && Info->AbilitySystemComponent.IsValid()
			? Info->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>() : nullptr;

		float AttackDamage = 0.f;
		if (AttrSet)
		{
			AttackDamage = AttrSet->GetAttackDamage();
		}

		const float FinalDamage = -AttackDamage * DamageScalar;
		OutSpecHandle.Data.Get()->SetSetByCallerMagnitude(DamageSetByCallerTag, FinalDamage);
		UE_LOG(LogPrimaryRangedGA, Verbose, TEXT("BuildDamageSpec: AttackDamage=%.2f DamageScalar=%.2f FinalDamage=%.2f"),
			AttackDamage,
			DamageScalar,
			FinalDamage);
	}

	return true;
}

FTransform UGA_PrimaryRangedBasic::ComputeMuzzleTransform(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;

	if (!Avatar)
	{
		return FTransform::Identity;
	}

	const USkeletalMeshComponent* Mesh = (ActorInfo && ActorInfo->SkeletalMeshComponent.IsValid())
		? ActorInfo->SkeletalMeshComponent.Get()
		: nullptr;

	if (!Mesh)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Avatar))
		{
			Mesh = Character->GetMesh();
		}
	}

	if (Mesh && MuzzleSocketName != NAME_None && Mesh->DoesSocketExist(MuzzleSocketName))
	{
		return Mesh->GetSocketTransform(MuzzleSocketName, RTS_World);
	}

	const FVector BaseLocation = Avatar->GetActorLocation();
	const FRotator Facing = Avatar->GetActorRotation();
	FVector SpawnLocation = BaseLocation + Facing.Vector() * ForwardSpawnOffset;
	SpawnLocation.Z += VerticalSpawnOffset;
	return FTransform(Facing, SpawnLocation);
}

float UGA_PrimaryRangedBasic::ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
	{
		return BaselineAttackSpeed;
	}

	if (const UAeyerjiAttributeSet* Attr = ActorInfo->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>())
	{
		const float AttackSpeed = Attr->GetAttackCooldown();
		return FMath::Max(AttackSpeed, KMinAttackSpeed);
	}

	return BaselineAttackSpeed;
}

float UGA_PrimaryRangedBasic::ResolveProjectileSpeed(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		if (const UAeyerjiAttributeSet* Attr = ActorInfo->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>())
		{
			const float AttributeSpeed = Attr->GetProjectileSpeedRanged();
			if (AttributeSpeed > KINDA_SMALL_NUMBER)
			{
				return AttributeSpeed;
			}
		}
	}

	return ProjectileSpeedFallback;
}

AActor* UGA_PrimaryRangedBasic::ResolveTargetActor(const FGameplayAbilityActorInfo* ActorInfo,
                                                   const FGameplayEventData* TriggerEventData) const
{
	const AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;

	if (TriggerEventData && TriggerEventData->Target.Get())
	{
		AActor* Candidate = const_cast<AActor*>(TriggerEventData->Target.Get());
		if (!AbilityTeamUtils::AreOnSameTeam(AvatarActor, Candidate))
		{
			return Candidate;
		}
	}

	if (TriggerEventData && TriggerEventData->TargetData.Num() > 0)
	{
		for (int32 Index = 0; Index < TriggerEventData->TargetData.Num(); ++Index)
		{
			if (const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(Index))
			{
				if (const FHitResult* HitResult = Data->GetHitResult())
				{
					if (AActor* HitActor = HitResult->GetActor())
					{
						if (!AbilityTeamUtils::AreOnSameTeam(AvatarActor, HitActor))
						{
							return HitActor;
						}
					}
				}

				const TArray<TWeakObjectPtr<AActor>> Actors = Data->GetActors();
				for (const TWeakObjectPtr<AActor>& WeakActor : Actors)
				{
					if (WeakActor.IsValid() && !AbilityTeamUtils::AreOnSameTeam(AvatarActor, WeakActor.Get()))
					{
						return WeakActor.Get();
					}
				}
			}
		}
	}

	if (!ActorInfo)
	{
		return nullptr;
	}

	if (AController* PlayerController = ActorInfo->PlayerController.Get())
	{
		if (const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(PlayerController))
		{
			AActor* Target = EnemyAI->GetTargetActor();
			if (!AbilityTeamUtils::AreOnSameTeam(AvatarActor, Target))
			{
				return Target;
			}
		}
	}

	AController* ResolvedController = nullptr;
	if (ActorInfo->OwnerActor.IsValid())
	{
		ResolvedController = Cast<AController>(ActorInfo->OwnerActor.Get());
	}

	if (!ResolvedController)
	{
		if (const APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get()))
		{
			ResolvedController = Pawn->GetController();
		}
	}

	if (const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(ResolvedController))
	{
		AActor* Target = EnemyAI->GetTargetActor();
		if (!AbilityTeamUtils::AreOnSameTeam(AvatarActor, Target))
		{
			return Target;
		}
	}

	return nullptr;
}

bool UGA_PrimaryRangedBasic::ShouldProcessServerLogic() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo && ActorInfo->IsNetAuthority();
}

bool UGA_PrimaryRangedBasic::IsLocallyPredicting() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo && ActorInfo->IsLocallyControlled() && !ActorInfo->IsNetAuthority();
}

void UGA_PrimaryRangedBasic::BroadcastPrimaryAttackComplete()
{
	if (!bSendCompletionGameplayEvent || bCompletionBroadcasted)
	{
		return;
	}

	bCompletionBroadcasted = true;

	if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(GetCurrentActorInfo()))
	{
		FGameplayEventData Payload;
		Payload.EventTag = AeyerjiTags::Event_PrimaryAttack_Completed;
		Payload.Instigator = GetAvatarActorFromActorInfo();
		Payload.Target = Payload.Instigator;

		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	}
}

void UGA_PrimaryRangedBasic::OnMontageCompleted()
{
	HandleMontageFinished(/*bWasCancelled=*/false);
}

void UGA_PrimaryRangedBasic::OnMontageInterrupted()
{
	HandleMontageFinished(/*bWasCancelled=*/true);
}

void UGA_PrimaryRangedBasic::OnMontageCancelled()
{
	HandleMontageFinished(/*bWasCancelled=*/true);
}

void UGA_PrimaryRangedBasic::HandleMontageFinished(bool bWasCancelled)
{
	if (!MontageTask)
	{
		return;
	}

	MontageTask->EndTask();
	MontageTask = nullptr;

	if (bWasCancelled && ShouldProcessServerLogic())
	{
		if (ActiveProjectile.IsValid())
		{
			ActiveProjectile->OnProjectileImpact.RemoveAll(this);
			ActiveProjectile->OnProjectileExpired.RemoveAll(this);
			ActiveProjectile->Destroy();
			ActiveProjectile.Reset();
		}
	}
}

UAnimMontage* UGA_PrimaryRangedBasic::SelectAttackMontage_Implementation(const FGameplayAbilityActorInfo& ActorInfo) const
{
	return AttackMontage.IsValid() ? AttackMontage.Get() : nullptr;
}
