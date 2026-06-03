#include "Abilities/GA_PrimaryMeleeBasic.h"

#include "AbilitySystemComponent.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemGlobals.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Combat/PrimaryMeleeComboProviderInterface.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "MouseNavBlueprintLibrary.h"
#include "Enemy/EnemyAIController.h"
#include "DrawDebugHelpers.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
#include "Logging/LogMacros.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/EngineTypes.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "WorldCollision.h"
#include "Engine/OverlapResult.h"
#include "Engine/HitResult.h"

class AAeyerjiPlayerController;

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryMeleeGA, Display, All);

namespace
{
	// Prevent extreme attack speed scaling from ever producing a zero-rate montage.
	constexpr float kMinAttackSpeed = 0.01f;

}

UGA_PrimaryMeleeBasic::UGA_PrimaryMeleeBasic()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bRetriggerInstancedAbility = false;

	BaselineAttackSpeed              = 1.f;
	DamageScalar                     = 1.f;
	DefaultDamageTypeTag             = AeyerjiTags::DamageType_Physical;
	MinCooldownDuration              = 0.05f;
	bSendCompletionGameplayEvent     = true;
	bCompletionBroadcasted           = false;
	bCachedHitShapeValid             = false;
	CurrentPhase                     = EPrimaryMeleePhase::None;
	ActivePhaseTag                   = FGameplayTag();
	bHasCommittedAtImpact            = false;
	MovementLockTag                  = AeyerjiTags::State_Ability_PrimaryMelee_BlockMovement;
	bMovementLocked                  = false;
	CancelWindowTimerHandle.Invalidate();
	ComboResetDelay                  = 0.65f;
	CurrentComboIndex                = INDEX_NONE;
	NextComboIndex                   = 0;
	ComboStagesExecuted              = 0;
	bComboInputBuffered              = false;
	ComboResetTimerHandle.Invalidate();

	// Prime the asset/activation tags used to gate other abilities and expose state to other systems.
	{
		FGameplayTagContainer AbilityAssetTags = GetAssetTags();
		AbilityAssetTags.AddTag(AeyerjiTags::Ability_Primary);
		AbilityAssetTags.AddTag(AeyerjiTags::Ability_Primary_Melee_Basic);
		SetAssetTags(AbilityAssetTags);

		ActivationOwnedTags.AddTag(AeyerjiTags::Ability_Primary);
		ActivationBlockedTags.Reset();
		ActivationBlockedTags.AddTag(AeyerjiTags::State_Dead);
		ActivationBlockedTags.AddTag(AeyerjiTags::State_CrowdControl_Stunned);
		ActivationBlockedTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);
	}

	if (!DamageSetByCallerTag.IsValid())
	{
		static const FName DamageTagName(TEXT("SetByCaller.Damage.Instant"));
		DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(DamageTagName, /*ErrorIfNotFound=*/false);
	}

	if (!AilmentDamagePerSecondSetByCallerTag.IsValid())
	{
		static const FName AilmentDamageTagName(TEXT("SetByCaller.Damage.PerSecond"));
		AilmentDamagePerSecondSetByCallerTag = FGameplayTag::RequestGameplayTag(AilmentDamageTagName, /*ErrorIfNotFound=*/false);
	}

	if (!AilmentDurationSetByCallerTag.IsValid())
	{
		static const FName AilmentDurationTagName(TEXT("SetByCaller.Duration"));
		AilmentDurationSetByCallerTag = FGameplayTag::RequestGameplayTag(AilmentDurationTagName, /*ErrorIfNotFound=*/false);
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("Constructed %s (BaselineAttackSpeed=%.2f DamageScalar=%.2f MinCooldown=%.3f)"),
		*GetNameSafe(this),
		BaselineAttackSpeed,
		DamageScalar,
		MinCooldownDuration);
}

void UGA_PrimaryMeleeBasic::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
											const FGameplayAbilityActorInfo* ActorInfo,
											const FGameplayAbilityActivationInfo ActivationInfo,
											const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	AEnemyAIController* EnemyAI = ResolveEnemyAIController(ActorInfo);
	AActor* RawTargetActor = EnemyAI ? EnemyAI->GetTargetActor() : nullptr;
	const bool bFriendlyTargetBlocked = RawTargetActor && AvatarActor && !bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(AvatarActor, RawTargetActor);
	AActor* FilteredTargetActor = bFriendlyTargetBlocked ? nullptr : RawTargetActor;
	const float InitialAttackRange = ResolveAttackRange();
	const float InitialTargetDistance = (AvatarActor && RawTargetActor) ? FVector::Dist(AvatarActor->GetActorLocation(), RawTargetActor->GetActorLocation()) : -1.f;
	const FString HandleStr      = Handle.ToString();
	const FString TriggerTagStr  = TriggerEventData ? TriggerEventData->EventTag.ToString() : FString(TEXT("None"));
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility -> Handle=%s Avatar=%s ASC=%s TriggerTag=%s PredictionKey=%d"),
		*HandleStr,
		*GetNameSafe(AvatarActor),
		ASC ? *GetNameSafe(ASC) : TEXT("None"),
		*TriggerTagStr,
		ActivationInfo.GetActivationPredictionKey().Current);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: avatar=%s ai=%s rawTarget=%s target=%s friendlyBlocked=%s distance=%.1f attackRange=%.1f serverLogic=%s predicting=%s."),
		*GetNameSafe(AvatarActor),
		*GetNameSafe(EnemyAI),
		*GetNameSafe(RawTargetActor),
		*GetNameSafe(FilteredTargetActor),
		bFriendlyTargetBlocked ? TEXT("true") : TEXT("false"),
		InitialTargetDistance,
		InitialAttackRange,
		ShouldProcessServerLogic() ? TEXT("true") : TEXT("false"),
		IsLocallyPredicting() ? TEXT("true") : TEXT("false"));

	if (!CheckCost(Handle, ActorInfo))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: CheckCost failed. Ending ability before execution."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	if (!CheckCooldown(Handle, ActorInfo))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: CheckCooldown failed. Ending ability before execution."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	bHasCommittedAtImpact = false;
	DamagedActors.Reset();
	bCompletionBroadcasted = false;
	bCachedHitShapeValid = false;
	CachedHitForward = FVector::ZeroVector;
	CachedHitOrigin = FVector::ZeroVector;
	ResetComboRuntimeState();
	ClearComboResetTimer();
	ClearConeTraceTimer();
	SetMovementLock(false);
	SetCanBeCanceled(true);
	RefreshComboMontagesFromAvatar(ActorInfo);

	const float AttackSpeed = ResolveAttackSpeed(ActorInfo);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Final AttackSpeed=%.3f (Baseline=%.3f) DamagedActors cleared."),
		AttackSpeed,
		BaselineAttackSpeed);

	StartupClickedTarget = ResolvePreferredClickedTarget(ActorInfo, /*MaxAgeSeconds=*/2.5f);
	if (AvatarActor)
	{
		AActor* FacingTarget = FilteredTargetActor ? FilteredTargetActor : StartupClickedTarget.Get();
		if (FacingTarget && FacingTarget != AvatarActor)
		{
			FVector ToTarget = FacingTarget->GetActorLocation() - AvatarActor->GetActorLocation();
			ToTarget.Z = 0.f;
			if (!ToTarget.IsNearlyZero())
			{
				const FRotator DesiredYaw(0.f, ToTarget.Rotation().Yaw, 0.f);
				AvatarActor->SetActorRotation(DesiredYaw);
				if (APawn* AvatarPawn = Cast<APawn>(AvatarActor))
				{
					if (AController* Controller = AvatarPawn->GetController())
					{
						Controller->SetControlRotation(DesiredYaw);
					}
				}
			}
		}
	}

	const int32 ComboCount = GetConfiguredComboCount(ActorInfo);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: comboCount=%d nextComboIndex=%d startupClickedTarget=%s attackSpeed=%.3f."),
		ComboCount,
		NextComboIndex,
		*GetNameSafe(StartupClickedTarget.Get()),
		AttackSpeed);
	if (ComboCount <= 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: No valid combo montages configured. Ending ability."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	if (NextComboIndex < 0 || NextComboIndex >= ComboCount)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: NextComboIndex %d out of range, clamping to zero."), NextComboIndex);
		NextComboIndex = 0;
	}

	const int32 StageIndexToPlay = NextComboIndex;

	if (!StartComboStage(StageIndexToPlay, ActorInfo, AttackSpeed))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: StartComboStage failed. Ability will end."));
		if (ShouldProcessServerLogic())
		{
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Server logic active, broadcasting completion before ending."));
			BroadcastPrimaryAttackComplete();
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA ActivateAbility: StartComboStage succeeded for stage %d."), StageIndexToPlay);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Montage started successfully."));
	}
}

void UGA_PrimaryMeleeBasic::InputPressed(const FGameplayAbilitySpecHandle Handle,
										 const FGameplayAbilityActorInfo* ActorInfo,
										 const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);

	if (!IsActive())
	{
		return;
	}

	bComboInputBuffered = true;

	UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("InputPressed: Buffered combo input (ComboStagesExecuted=%d NextComboIndex=%d)."),
		ComboStagesExecuted,
		NextComboIndex);
}

void UGA_PrimaryMeleeBasic::CancelAbility(const FGameplayAbilitySpecHandle Handle,
										  const FGameplayAbilityActorInfo* ActorInfo,
										  const FGameplayAbilityActivationInfo ActivationInfo,
										  bool bReplicateCancelAbility)
{
	const FString HandleStr = Handle.ToString();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("CancelAbility -> Handle=%s Avatar=%s MontageTask=%s"),
		*HandleStr,
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		MontageTask ? *GetNameSafe(MontageTask) : TEXT("None"));

	SetAbilityPhase(EPrimaryMeleePhase::Cancelled);
	StopMontageTask();
	ClearCancelWindowTimer();
	ClearConeTraceTimer();
	SetMovementLock(false);
	SetCanBeCanceled(true);

	NextComboIndex = 0;
	ClearComboResetTimer();
	ResetComboRuntimeState();
	StartupClickedTarget.Reset();
	bCachedHitShapeValid = false;
	CachedHitForward = FVector::ZeroVector;
	CachedHitOrigin = FVector::ZeroVector;

	Super::CancelAbility(Handle, ActorInfo, ActivationInfo, bReplicateCancelAbility);
}

void UGA_PrimaryMeleeBasic::EndAbility(const FGameplayAbilitySpecHandle Handle,
									   const FGameplayAbilityActorInfo* ActorInfo,
									   const FGameplayAbilityActivationInfo ActivationInfo,
									   bool bReplicateEndAbility,
									   bool bWasCancelled)
{
	const FString HandleStr = Handle.ToString();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("EndAbility -> Handle=%s Cancelled=%s MontageTask=%s DamagedActors=%d"),
		*HandleStr,
		bWasCancelled ? TEXT("true") : TEXT("false"),
		MontageTask ? *GetNameSafe(MontageTask) : TEXT("None"),
		DamagedActors.Num());

	ClearMontageFailsafeTimer();
	StopMontageTask();
	ClearConeTraceTimer();

	DamagedActors.Reset();
	ClearAbilityPhase();
	bHasCommittedAtImpact = false;
	ClearCancelWindowTimer();
	SetMovementLock(false);
	SetCanBeCanceled(true);
	ResetComboRuntimeState();
	StartupClickedTarget.Reset();
	bCachedHitShapeValid = false;
	CachedHitForward = FVector::ZeroVector;
	CachedHitOrigin = FVector::ZeroVector;

	if (bWasCancelled)
	{
		NextComboIndex = 0;
		ClearComboResetTimer();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_PrimaryMeleeBasic::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
										  const FGameplayAbilityActorInfo* ActorInfo,
										  const FGameplayAbilityActivationInfo ActivationInfo) const
{
	// Attribute-based: just apply the GE; the BP reads AttackCooldown from UAeyerjiAttributeSet.
	if (!CooldownGameplayEffectClass)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ApplyCooldown: No CooldownGameplayEffectClass, falling back to Super."));
		Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle =
		MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));

	if (SpecHandle.IsValid())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ApplyCooldown: Applying cooldown GE %s to owner."),
			*GetNameSafe(CooldownGameplayEffectClass.GetDefaultObject()));
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ApplyCooldown: Failed to build cooldown spec for %s."),
			*GetNameSafe(CooldownGameplayEffectClass.Get()));
	}
}

bool UGA_PrimaryMeleeBasic::CheckCooldown(const FGameplayAbilitySpecHandle Handle,
                                          const FGameplayAbilityActorInfo* ActorInfo,
                                          FGameplayTagContainer* OptionalRelevantTags) const
{
	if (Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags))
	{
		return true;
	}

	const FGameplayAbilityActorInfo* Info = ActorInfo ? ActorInfo : GetCurrentActorInfo();
	const UAbilitySystemComponent* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return false;
	}

	const FGameplayTagContainer* CooldownTags = GetCooldownTags();
	if (!CooldownTags || CooldownTags->IsEmpty())
	{
		return false;
	}

	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(*CooldownTags);
	const TArray<float> Durations = ASC->GetActiveEffectsTimeRemaining(Query);
	const double Now = ASC->GetWorld() ? ASC->GetWorld()->GetTimeSeconds() : 0.0;
	if (LastCooldownDebugTime < 0.0 || (Now - LastCooldownDebugTime) >= 0.5)
	{
		LastCooldownDebugTime = Now;
		FGameplayTagContainer OwnedTags;
		ASC->GetOwnedGameplayTags(OwnedTags);
		const FString CooldownTagStr = CooldownTags->ToStringSimple();
		const FString OwnedTagStr = OwnedTags.ToStringSimple();
		FString DurationStr;
		for (int32 Idx = 0; Idx < Durations.Num(); ++Idx)
		{
			DurationStr += FString::Printf(TEXT("%s%.3f"),
				Idx == 0 ? TEXT("") : TEXT(", "),
				Durations[Idx]);
		}
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose,
			TEXT("[BossPrimaryAttack] MeleeGA CheckCooldown: owned=[%s] cooldownTags=[%s] durations=[%s]"),
			*OwnedTagStr,
			*CooldownTagStr,
			DurationStr.IsEmpty() ? TEXT("none") : *DurationStr);
	}

	for (const float TimeRemaining : Durations)
	{
		// Treat only positive, non-trivial remaining time as an actual cooldown.
		// Some setups stamp cooldown tags as infinite effects or loose tags; those should not hard-block attacks.
		if (TimeRemaining > KINDA_SMALL_NUMBER)
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose,
				TEXT("[BossPrimaryAttack] MeleeGA CheckCooldown: blocking (remaining=%.3f)."), TimeRemaining);
			return false;
		}
	}

	if (OptionalRelevantTags)
	{
		OptionalRelevantTags->Reset();
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("CheckCooldown: cooldown tags present but no active effects; allowing activation."));
	return true;
}

bool UGA_PrimaryMeleeBasic::StartMontage(float AttackSpeed, UAnimMontage* MontageToPlay)
{
	UAnimMontage* Montage = MontageToPlay;
	if (!Montage)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartMontage: Montage pointer null (AttackMontage asset missing)."));
		return false;
	}

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UAnimInstance* AnimInst = Info ? Info->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartMontage: No AnimInstance on avatar %s."),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		return false;
	}

	if (Montage->GetPlayLength() <= 0.f)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartMontage: Montage %s has zero length, refusing to play."),
			*GetNameSafe(Montage));
		return false;
	}

	const float Rate = CalculateMontagePlayRate(AttackSpeed);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartMontage: Montage=%s AttackSpeed=%.3f Baseline=%.3f PlayRate=%.3f"),
		*GetNameSafe(Montage),
		AttackSpeed,
		BaselineAttackSpeed,
		Rate);

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, Rate);
	if (!MontageTask)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartMontage: Failed to create montage task."));
		return false;
	}

	BindMontageDelegates(MontageTask);
	MontageTask->ReadyForActivation();
	ArmMontageFailsafe(Montage, Rate);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartMontage: MontageTask %s ready for activation."), *GetNameSafe(MontageTask));
	return true;
}

void UGA_PrimaryMeleeBasic::StopMontageTask()
{
	ClearMontageFailsafeTimer();

	if (!MontageTask)
	{
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StopMontageTask: Ending MontageTask %s."),
		*GetNameSafe(MontageTask));
	MontageTask->EndTask();
	MontageTask = nullptr;
}

void UGA_PrimaryMeleeBasic::ArmMontageFailsafe(UAnimMontage* Montage, float PlayRate)
{
	ClearMontageFailsafeTimer();

	if (!Montage)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float SafePlayRate = FMath::Max(PlayRate, KINDA_SMALL_NUMBER);
		const float FailsafeDelay = FMath::Max(0.25f, (Montage->GetPlayLength() / SafePlayRate) + 0.25f);
		World->GetTimerManager().SetTimer(MontageFailsafeTimerHandle, this, &UGA_PrimaryMeleeBasic::OnMontageFailsafeExpired, FailsafeDelay, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartMontage: armed montage failsafe for %.3fs (montage=%s rate=%.3f)."),
			FailsafeDelay,
			*GetNameSafe(Montage),
			SafePlayRate);
	}
}

void UGA_PrimaryMeleeBasic::ClearMontageFailsafeTimer()
{
	if (UWorld* World = GetWorld())
	{
		if (MontageFailsafeTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(MontageFailsafeTimerHandle);
		}
	}

	MontageFailsafeTimerHandle.Invalidate();
}

void UGA_PrimaryMeleeBasic::OnMontageFailsafeExpired()
{
	MontageFailsafeTimerHandle.Invalidate();

	if (!IsActive())
	{
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA MontageFailsafe: forcing finish for active ability handle=%s phase=%d."),
		*CurrentSpecHandle.ToString(),
		static_cast<int32>(CurrentPhase));
	HandleMontageFinished(/*bWasCancelled=*/false);
}

void UGA_PrimaryMeleeBasic::ClearConeTraceTimer()
{
	if (UWorld* World = GetWorld())
	{
		if (ConeTraceTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(ConeTraceTimerHandle);
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ClearConeTraceTimer: Cleared cone sweep timer."));
		}
	}

	ConeTraceTimerHandle.Invalidate();
	ActiveConeStrikeElapsed = 0.f;
	ActiveConeStrikeDuration = 0.f;
	ActiveConeStrikeInterval = 0.f;
}

void UGA_PrimaryMeleeBasic::StartConeStrike()
{
	if (!IsActive())
	{
		return;
	}

	if (CurrentPhase == EPrimaryMeleePhase::Cancelled || CurrentPhase == EPrimaryMeleePhase::Recovery)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartConeStrike: Phase=%d, skipping strike."), static_cast<int32>(CurrentPhase));
		return;
	}

	const float ConeAngle = ResolveAttackAngleDegrees();
	const bool bHasCleaveAngle = ConeAngle > KINDA_SMALL_NUMBER;
	if (!bHasCleaveAngle)
	{
		if (ResolveEnemyAIController(CurrentActorInfo))
		{
			AActor* EnemyTarget = ResolveEnemyTargetActor(CurrentActorInfo);
			if (!EnemyTarget)
			{
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartConeStrike: Enemy has no valid target for single-target strike; ending ability."));
				if (ShouldProcessServerLogic())
				{
					BroadcastPrimaryAttackComplete();
					EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
				}
				return;
			}

			if (AActor* InstigatorActor = GetAvatarActorFromActorInfo())
			{
				FHitResult TargetHit;
				const float AttackRange = ResolveAttackRange();
				const float PreferredRange = (AttackRange > 0.f) ? AttackRange * 1.75f : ConeTraceRangeFallback * 1.5f;
				if (!TryBuildHitFromActor(InstigatorActor, EnemyTarget, PreferredRange, TargetHit))
				{
					UE_LOG(LogPrimaryMeleeGA, Verbose,
						TEXT("[BossPrimaryAttack] MeleeGA StartConeStrike: Enemy target %s out of range for single-target strike (distance=%.1f preferredRange=%.1f); ending ability."),
						*GetNameSafe(EnemyTarget),
						FVector::Dist(InstigatorActor->GetActorLocation(), EnemyTarget->GetActorLocation()),
						PreferredRange);
					if (ShouldProcessServerLogic())
					{
						BroadcastPrimaryAttackComplete();
						EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
					}
					return;
				}
			}
		}
	}

	SetAbilityPhase(EPrimaryMeleePhase::HitWindow);
	SetMovementLock(true);
	SetCanBeCanceled(false);
	ClearCancelWindowTimer();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartConeStrike: entered hit window (phase=%d montageRate=%.3f)."),
		static_cast<int32>(CurrentPhase),
		CurrentMontagePlayRate);

	if (!EnsureAbilityCommitted())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartConeStrike: Commit failed; server will end ability, clients will wait for replication."));
		if (ShouldProcessServerLogic())
		{
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		}
		return;
	}

	const float RateScale = (CurrentMontagePlayRate > KINDA_SMALL_NUMBER) ? (1.f / CurrentMontagePlayRate) : 1.f;
	const float InitialDelay = FMath::Max(0.f, ConeStrikeDelay * RateScale);
	ActiveConeStrikeInterval = FMath::Max(KINDA_SMALL_NUMBER, ConeStrikeTickInterval * RateScale);
	ActiveConeStrikeDuration = FMath::Max(0.f, ConeStrikeDuration * RateScale);
	ActiveConeStrikeElapsed = 0.f;

	if (InitialDelay <= 0.f)
	{
		ExecuteConeTraceSweep();
		ActiveConeStrikeElapsed = ActiveConeStrikeInterval;
	}

	if (ActiveConeStrikeDuration <= KINDA_SMALL_NUMBER || ActiveConeStrikeElapsed >= ActiveConeStrikeDuration)
	{
		if (InitialDelay > 0.f && ActiveConeStrikeElapsed <= 0.f)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					ConeTraceTimerHandle,
					this,
					&UGA_PrimaryMeleeBasic::ExecuteConeTraceSweep,
					InitialDelay,
					false);
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartConeStrike: One-shot sweep scheduled (Delay=%.3f)."), InitialDelay);
			}
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float FirstDelay = (InitialDelay > 0.f) ? InitialDelay : ActiveConeStrikeInterval;
		World->GetTimerManager().SetTimer(
			ConeTraceTimerHandle,
			this,
			&UGA_PrimaryMeleeBasic::TickConeStrike,
			ActiveConeStrikeInterval,
			true,
			FirstDelay);

		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartConeStrike: Armed sweeps (Interval=%.3f Duration=%.3f Delay=%.3f)."),
			ActiveConeStrikeInterval,
			ActiveConeStrikeDuration,
			InitialDelay);
	}
}

void UGA_PrimaryMeleeBasic::TickConeStrike()
{
	if (!IsActive())
	{
		ClearConeTraceTimer();
		return;
	}

	if (ActiveConeStrikeElapsed >= ActiveConeStrikeDuration)
	{
		ClearConeTraceTimer();
		return;
	}

	ExecuteConeTraceSweep();
	ActiveConeStrikeElapsed += ActiveConeStrikeInterval;

	if (ActiveConeStrikeElapsed >= ActiveConeStrikeDuration)
	{
		ClearConeTraceTimer();
	}
}

void UGA_PrimaryMeleeBasic::ExecuteConeTraceSweep()
{
	if (!IsActive())
	{
		return;
	}

	if (CurrentPhase == EPrimaryMeleePhase::Cancelled || CurrentPhase == EPrimaryMeleePhase::Recovery)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Phase=%d, ignoring sweep."), static_cast<int32>(CurrentPhase));
		return;
	}

	AActor* InstigatorActor = GetAvatarActorFromActorInfo();
	if (!InstigatorActor)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ExecuteConeTraceSweep: Avatar actor invalid, aborting hit processing."));
		return;
	}

	const float AttackRange = ResolveAttackRange();
	TArray<FHitResult> UniqueHits;
	UniqueHits.Reserve(8);

	auto TryRegisterHit = [&](const FHitResult& Hit, const TCHAR* SourceLabel)
	{
		AActor* TargetActor = Hit.GetActor();
		if (!TargetActor || TargetActor == InstigatorActor)
		{
			return false;
		}

		if (!bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(InstigatorActor, TargetActor))
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ExecuteConeTraceSweep: Ignoring same-team actor %s (Source=%s)."),
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		if (DamagedActors.Contains(WeakTarget))
		{
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Skipping already damaged actor %s (Source=%s)."),
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		DamagedActors.Add(WeakTarget);
		UniqueHits.Add(Hit);

		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Added hit on %s (Source=%s)."),
			*GetNameSafe(TargetActor),
			SourceLabel);
		return true;
	};

	// Prefer explicit targets (AI target or clicked pawn) before any cone sweep.
	auto TryConsumePreferred = [&](AActor* PreferredTarget, const TCHAR* Source)
	{
		if (!PreferredTarget)
		{
			return;
		}

		FHitResult PreferredHit;
		const float PreferredRange = (AttackRange > 0.f) ? AttackRange * 1.75f : ConeTraceRangeFallback * 1.5f;
		if (TryBuildHitFromActor(InstigatorActor, PreferredTarget, PreferredRange, PreferredHit))
		{
			if (TryRegisterHit(PreferredHit, Source))
			{
				StartupClickedTarget = nullptr;
			}
		}
		else
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ExecuteConeTraceSweep: Preferred target %s outside range %.1f (Source=%s)."),
				*GetNameSafe(PreferredTarget),
				PreferredRange,
				Source);
		}
	};

	TryConsumePreferred(ResolveEnemyTargetActor(CurrentActorInfo), TEXT("EnemyTarget"));
	TryConsumePreferred(ResolvePreferredClickedTarget(CurrentActorInfo, /*MaxAgeSeconds=*/2.5f), TEXT("ClickedTarget_Recent"));
	if (StartupClickedTarget.IsValid())
	{
		TryConsumePreferred(StartupClickedTarget.Get(), TEXT("ClickedTarget_Activation"));
	}

	// Cache the swing shape the first time we enter the hit window so turning mid-swing does not back-hit.
	if (!bCachedHitShapeValid)
	{
		CachedHitOrigin = InstigatorActor->GetActorLocation();
		CachedHitForward = InstigatorActor->GetActorForwardVector();

		// Prefer weapon socket direction if available so the cone aligns with the actual swing.
		if (const ACharacter* Character = Cast<ACharacter>(InstigatorActor))
		{
			bool bUsedWeaponSockets = false;
			if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
			{
				static const FName StartSocket(TEXT("WeaponRHandSocket"));
				static const FName EndSocket(TEXT("WeaponTip"));
				if (Mesh->DoesSocketExist(StartSocket) && Mesh->DoesSocketExist(EndSocket))
				{
					const FVector Start = Mesh->GetSocketLocation(StartSocket);
					const FVector End = Mesh->GetSocketLocation(EndSocket);
					CachedHitOrigin = Start;
					CachedHitForward = (End - Start);
					bUsedWeaponSockets = true;
				}
				else if (CachedHitForward.IsNearlyZero())
				{
					// Fallback to mesh forward when the mesh is rotated relative to the capsule.
					const FVector MeshForward = Mesh->GetForwardVector();
					if (!bUsedWeaponSockets && !MeshForward.IsNearlyZero())
					{
						CachedHitForward = MeshForward;
					}
				}
			}
		}

		CachedHitForward.Z = 0.f;
		if (!CachedHitForward.Normalize())
		{
			CachedHitForward = InstigatorActor->GetActorForwardVector().GetSafeNormal();
		}
		bCachedHitShapeValid = !CachedHitForward.IsNearlyZero();
	}

	const float ConeRange = AttackRange;
	const float ConeAngle = ResolveAttackAngleDegrees();
	if (ConeRange > KINDA_SMALL_NUMBER && ConeAngle > KINDA_SMALL_NUMBER)
	{
		TArray<FHitResult> ConeHits;
		GatherConeTraceTargets(
			InstigatorActor,
			ConeRange,
			ConeAngle,
			ConeHits,
			bCachedHitShapeValid ? &CachedHitOrigin : nullptr,
			bCachedHitShapeValid ? &CachedHitForward : nullptr);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Cone trace produced %d candidates (Range=%.1f Angle=%.1f)."),
			ConeHits.Num(),
			ConeRange,
			ConeAngle);

		for (const FHitResult& ConeHit : ConeHits)
		{
			TryRegisterHit(ConeHit, TEXT("ConeTrace"));
		}
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose,
			TEXT("ExecuteConeTraceSweep: Cone trace skipped (Range=%.1f Angle=%.1f)."),
			ConeRange,
			ConeAngle);
	}

	if (UniqueHits.Num() == 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: No unique hits after filtering."));
		return;
	}

	const FGameplayAbilityTargetDataHandle TargetData = MakeUniqueTargetData(UniqueHits);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Generated target data with %d entries (ServerLogic=%s LocalPredict=%s)."),
		TargetData.Num(),
		ShouldProcessServerLogic() ? TEXT("true") : TEXT("false"),
		IsLocallyPredicting() ? TEXT("true") : TEXT("false"));

	if (ShouldProcessServerLogic())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Invoking HandleServerDamage."));
		HandleServerDamage(TargetData);
	}

	if (IsLocallyPredicting())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ExecuteConeTraceSweep: Invoking HandlePredictedFeedback."));
		HandlePredictedFeedback(TargetData);
	}
}

AEnemyAIController* UGA_PrimaryMeleeBasic::ResolveEnemyAIController(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (!ActorInfo)
	{
		return nullptr;
	}

	AController* Controller = ActorInfo->PlayerController.Get();
	if (!Controller && ActorInfo->OwnerActor.IsValid())
	{
		Controller = Cast<AController>(ActorInfo->OwnerActor.Get());
	}

	if (!Controller)
	{
		if (const APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get()))
		{
			Controller = Pawn->GetController();
		}
	}

	return Cast<AEnemyAIController>(Controller);
}

AActor* UGA_PrimaryMeleeBasic::ResolveEnemyTargetActor(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const AEnemyAIController* EnemyAI = ResolveEnemyAIController(ActorInfo);
	if (!EnemyAI)
	{
		return nullptr;
	}

	AActor* Target = EnemyAI->GetTargetActor();
	if (!Target)
	{
		return nullptr;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(AvatarActor, Target))
	{
		return nullptr;
	}

	return Target;
}

void UGA_PrimaryMeleeBasic::HandleServerDamage(const FGameplayAbilityTargetDataHandle& TargetData)
{
	if (TargetData.Num() == 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Empty target data."));
		return;
	}

	FString DamageClassStr;
	if (DamageEffectClass.IsValid())
	{
		DamageClassStr = GetNameSafe(DamageEffectClass.Get());
	}
	else
	{
		DamageClassStr = DamageEffectClass.ToSoftObjectPath().ToString();
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Avatar=%s TargetCount=%d DamageEffect=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		TargetData.Num(),
		*DamageClassStr);

	TSubclassOf<UGameplayEffect> DamageGEClass;
	if (DamageEffectClass.IsValid())
	{
		DamageGEClass = DamageEffectClass.Get();
	}
	else if (DamageEffectClass.ToSoftObjectPath().IsValid())
	{
		DamageGEClass = DamageEffectClass.LoadSynchronous();
	}

	if (!DamageGEClass)
	{
		DamageGEClass = UGE_DamagePhysical::StaticClass();
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: DamageEffectClass not set; using UGE_DamagePhysical."));
	}

	if (DamageGEClass)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Using GE class %s at ability level %d."),
			*GetNameSafe(DamageGEClass.GetDefaultObject()),
			GetAbilityLevel());
		FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(DamageGEClass, GetAbilityLevel());
		if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
		{
			ApplyDamageTypeTagToSpec(DamageSpec, DefaultDamageTypeTag);

			// Push a SetByCaller magnitude so the gameplay effect can stay data-driven while still reflecting attributes.
			if (DamageSetByCallerTag.IsValid())
			{
				float AttackDamageValue = 0.f;
				if (const UAbilitySystemComponent* ASC = GetCurrentActorInfo() ? GetCurrentActorInfo()->AbilitySystemComponent.Get() : nullptr)
				{
					if (const UAeyerjiAttributeSet* Attr = ASC->GetSet<UAeyerjiAttributeSet>())
					{
						AttackDamageValue = Attr->GetAttackDamage();
					}
				}
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: AttackDamage attribute %.2f scalar %.2f final raw %.2f."),
					AttackDamageValue,
					DamageScalar,
					AttackDamageValue * DamageScalar);

				if (AttackDamageValue <= 0.f)
				{
					UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: AttackDamage is %.2f; outgoing damage will be zero."), AttackDamageValue);
				}

				const float FinalDamage = AttackDamageValue * DamageScalar;

				DamageSpec.Data->SetSetByCallerMagnitude(DamageSetByCallerTag, FinalDamage);
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: SetByCaller %s = %.2f."),
					*DamageSetByCallerTag.ToString(),
					FinalDamage);
			}
			else
			{
				UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: DamageSetByCallerTag is invalid; damage GE will rely on baked-in modifiers."));
			}

			int32 TargetsWithASC = 0;
			int32 TargetsWithoutASC = 0;

			for (int32 DataIdx = 0; DataIdx < TargetData.Data.Num(); ++DataIdx)
			{
				const TSharedPtr<FGameplayAbilityTargetData>& Data = TargetData.Data[DataIdx];
				if (!Data.IsValid())
				{
					UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: TargetData[%d] invalid."), DataIdx);
					continue;
				}

				const TArray<TWeakObjectPtr<AActor>> TargetActors = Data->GetActors();

				if (TargetActors.Num() == 0)
				{
					UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: TargetData[%d] has no actors."), DataIdx);
				}

				for (const TWeakObjectPtr<AActor>& ActorPtr : TargetActors)
				{
					AActor* TargetActor = ActorPtr.Get();
					if (!TargetActor)
					{
						UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: Null actor in TargetData[%d]."), DataIdx);
						continue;
					}

					UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
					if (TargetASC)
					{
						++TargetsWithASC;
						UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Target %s will receive damage via ASC %s."),
							*GetNameSafe(TargetActor),
							*GetNameSafe(TargetASC));
					}
					else
					{
						++TargetsWithoutASC;
						UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: Target %s has no ASC; damage will not apply."), *GetNameSafe(TargetActor));
					}
				}
			}

			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: TargetsWithASC=%d TargetsWithoutASC=%d."),
				TargetsWithASC,
				TargetsWithoutASC);

			// Apply once so GE stacking/mitigation happens inside the AbilitySystemComponent.
			ApplyGameplayEffectSpecToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, DamageSpec, TargetData);
		}
		else
		{
			UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: Failed to create damage spec."));
		}
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: Damage effect class invalid; skipping damage application."));
	}

	ApplyAilmentsToTargetData(TargetData);

	BP_HandleMeleeDamage(TargetData);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: BP_HandleMeleeDamage dispatched."));
}

void UGA_PrimaryMeleeBasic::ApplyAilmentsToTargetData(const FGameplayAbilityTargetDataHandle& TargetData)
{
	if (TargetData.Num() == 0 || AilmentEffectsByType.Num() == 0)
	{
		return;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAbilitySystemComponent* SourceASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const UAeyerjiAttributeSet* AttrSet = SourceASC ? SourceASC->GetSet<UAeyerjiAttributeSet>() : nullptr;
	if (!AttrSet)
	{
		return;
	}

	FGameplayTagContainer SourceTags = GetAssetTags();
	if (SourceASC)
	{
		FGameplayTagContainer OwnedTags;
		SourceASC->GetOwnedGameplayTags(OwnedTags);
		SourceTags.AppendTags(OwnedTags);
	}

	for (const TPair<FGameplayTag, TSoftClassPtr<UGameplayEffect>>& Pair : AilmentEffectsByType)
	{
		const FGameplayTag& AilmentTag = Pair.Key;
		if (!AilmentTag.IsValid() || !SourceTags.HasTag(AilmentTag))
		{
			continue;
		}

		TSubclassOf<UGameplayEffect> AilmentClass = Pair.Value.Get();
		if (!AilmentClass && Pair.Value.ToSoftObjectPath().IsValid())
		{
			AilmentClass = Pair.Value.LoadSynchronous();
		}

		if (!AilmentClass)
		{
			continue;
		}

		float AilmentAmount = 0.f;
		float AilmentDuration = 0.f;
		if (!ResolveAilmentMagnitudes(AilmentTag, AilmentAmount, AilmentDuration))
		{
			continue;
		}

		FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(AilmentClass, GetAbilityLevel());
		if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
		{
			continue;
		}

		if (AilmentDamagePerSecondSetByCallerTag.IsValid())
		{
			SpecHandle.Data->SetSetByCallerMagnitude(AilmentDamagePerSecondSetByCallerTag, AilmentAmount);
		}

		if (AilmentDurationSetByCallerTag.IsValid())
		{
			SpecHandle.Data->SetSetByCallerMagnitude(AilmentDurationSetByCallerTag, AilmentDuration);
		}

		ApplyGameplayEffectSpecToTarget(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, SpecHandle, TargetData);
	}
}

bool UGA_PrimaryMeleeBasic::ResolveAilmentMagnitudes(const FGameplayTag& AilmentTypeTag, float& OutAmount, float& OutDuration) const
{
	OutAmount = 0.f;
	OutDuration = 0.f;

	if (!AilmentTypeTag.IsValid())
	{
		return false;
	}

	const UAbilitySystemComponent* SourceASC = GetCurrentActorInfo() ? GetCurrentActorInfo()->AbilitySystemComponent.Get() : nullptr;
	const UAeyerjiAttributeSet* AttrSet = SourceASC ? SourceASC->GetSet<UAeyerjiAttributeSet>() : nullptr;
	if (!AttrSet)
	{
		return false;
	}

	const FString TagString = AilmentTypeTag.ToString();
	if (TagString.StartsWith(TEXT("AilmentType.Poisonous")))
	{
		OutAmount = AttrSet->GetPoisonAmount();
		OutDuration = AttrSet->GetPoisonDuration();
		return true;
	}

	if (TagString.StartsWith(TEXT("AilmentType.Traumatizing")))
	{
		OutAmount = AttrSet->GetTraumaAmount();
		OutDuration = AttrSet->GetTraumaDuration();
		return true;
	}

	if (TagString.StartsWith(TEXT("AilmentType.Corrupting")))
	{
		OutAmount = AttrSet->GetCorruptionAmount();
		OutDuration = AttrSet->GetCorruptionDuration();
		return true;
	}

	return false;
}

void UGA_PrimaryMeleeBasic::HandlePredictedFeedback(const FGameplayAbilityTargetDataHandle& TargetData)
{
	if (TargetData.Num() == 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandlePredictedFeedback: Empty target data."));
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandlePredictedFeedback: Passing %d targets to blueprint."), TargetData.Num());
	BP_HandlePredictedMeleeHit(TargetData);
}

void UGA_PrimaryMeleeBasic::BindMontageDelegates(UAbilityTask_PlayMontageAndWait* Task)
{
	if (!Task)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("BindMontageDelegates: Task null."));
		return;
	}

	Task->OnCompleted.AddDynamic(this, &UGA_PrimaryMeleeBasic::OnMontageCompleted);
	Task->OnBlendOut.AddDynamic(this, &UGA_PrimaryMeleeBasic::OnMontageCompleted);
	Task->OnInterrupted.AddDynamic(this, &UGA_PrimaryMeleeBasic::OnMontageInterrupted);
	Task->OnCancelled.AddDynamic(this, &UGA_PrimaryMeleeBasic::OnMontageCancelled);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BindMontageDelegates: Bound montage delegates for task %s."), *GetNameSafe(Task));
}

void UGA_PrimaryMeleeBasic::OnMontageCompleted()
{
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMontageCompleted."));
	HandleMontageFinished(/*bWasCancelled=*/false);
}

void UGA_PrimaryMeleeBasic::OnMontageInterrupted()
{
	UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("OnMontageInterrupted."));
	HandleMontageFinished(/*bWasCancelled=*/true);
}

void UGA_PrimaryMeleeBasic::OnMontageCancelled()
{
	UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("OnMontageCancelled."));
	HandleMontageFinished(/*bWasCancelled=*/true);
}

void UGA_PrimaryMeleeBasic::HandleMontageFinished(bool bWasCancelled)
{
	ClearMontageFailsafeTimer();

	if (!IsActive())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleMontageFinished: Ability inactive, ignoring finish (Cancelled=%s)."),
			bWasCancelled ? TEXT("true") : TEXT("false"));
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleMontageFinished: Cancelled=%s ShouldProcessServerLogic=%s"),
		bWasCancelled ? TEXT("true") : TEXT("false"),
		ShouldProcessServerLogic() ? TEXT("true") : TEXT("false"));

	ClearCancelWindowTimer();
	ClearConeTraceTimer();
	if (bWasCancelled)
	{
		SetAbilityPhase(EPrimaryMeleePhase::Cancelled);
		SetMovementLock(false);
		NextComboIndex = 0;
		ClearComboResetTimer();

		if (ShouldProcessServerLogic())
		{
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleMontageFinished: Cancelled montage, broadcasting completion and ending ability."));
			BroadcastPrimaryAttackComplete();
		}

		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, true);
		return;
	}

	if (TryLaunchBufferedCombo())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleMontageFinished: Continuing combo to index %d (ComboStagesExecuted=%d)."),
			CurrentComboIndex,
			ComboStagesExecuted);
		return;
	}

	if (!bHasCommittedAtImpact && !EnsureAbilityCommitted())
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleMontageFinished: Commit failed during recovery."));
	}

	SetAbilityPhase(EPrimaryMeleePhase::Recovery);
	SetMovementLock(true);
	SetCanBeCanceled(false);
	ScheduleComboReset();

	if (ShouldProcessServerLogic())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleMontageFinished: Broadcasting primary attack complete."));
		BroadcastPrimaryAttackComplete();
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, false);
}

void UGA_PrimaryMeleeBasic::BroadcastPrimaryAttackComplete()
{
	if (!bSendCompletionGameplayEvent || bCompletionBroadcasted)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BroadcastPrimaryAttackComplete: Skipping (SendEvent=%s Broadcasted=%s)."),
			bSendCompletionGameplayEvent ? TEXT("true") : TEXT("false"),
			bCompletionBroadcasted ? TEXT("true") : TEXT("false"));
		return;
	}

	bCompletionBroadcasted = true;
	const FString CompletionTagStr = AeyerjiTags::Event_PrimaryAttack_Completed.GetTag().ToString();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BroadcastPrimaryAttackComplete: Dispatching gameplay event %s."),
		*CompletionTagStr);

	if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(GetCurrentActorInfo()))
	{
		FGameplayEventData Payload;
		Payload.EventTag   = AeyerjiTags::Event_PrimaryAttack_Completed;
		Payload.Instigator = GetAvatarActorFromActorInfo();
		Payload.Target     = Payload.Instigator;

		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BroadcastPrimaryAttackComplete: Event handled by ASC %s."),
			*GetNameSafe(ASC));
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("BroadcastPrimaryAttackComplete: ASC missing, unable to dispatch event."));
	}
}

bool UGA_PrimaryMeleeBasic::ShouldProcessServerLogic() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo && ActorInfo->IsNetAuthority();
}

bool UGA_PrimaryMeleeBasic::IsLocallyPredicting() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || ActorInfo->IsNetAuthority() || !ActorInfo->IsLocallyControlled())
	{
		return false;
	}

	// Only treat this activation as locally predicting if we actually have a valid prediction key.
	// Server-initiated / server-only abilities can still be locally controlled on clients, but cannot commit from the client.
	return GetCurrentActivationInfo().GetActivationPredictionKey().IsValidKey();
}

float UGA_PrimaryMeleeBasic::GetNumericAttributeOrDefault(const FGameplayAttribute& Attribute, float DefaultValue) const
{
	if (!Attribute.IsValid())
	{
		return DefaultValue;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return DefaultValue;
	}

	const float Value = ASC->GetNumericAttribute(Attribute);
	return (Value > KINDA_SMALL_NUMBER) ? Value : DefaultValue;
}

float UGA_PrimaryMeleeBasic::ResolveAttackAngleDegrees() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return FMath::Clamp(ConeTraceAngleFallback, 0.f, 360.f);
	}

	const float Angle = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackAngleAttribute());
	if (Angle <= KINDA_SMALL_NUMBER)
	{
		return FMath::Clamp(ConeTraceAngleFallback, 0.f, 360.f);
	}

	return FMath::Clamp(Angle, 1.f, 360.f);
}

float UGA_PrimaryMeleeBasic::ResolveAttackRange() const
{
	const float Range = GetNumericAttributeOrDefault(UAeyerjiAttributeSet::GetAttackRangeAttribute(), ConeTraceRangeFallback);
	return FMath::Max(Range, 0.f);
}

void UGA_PrimaryMeleeBasic::GatherConeTraceTargets(AActor* InstigatorActor, float Range, float AngleDegrees, TArray<FHitResult>& OutHits, const FVector* OverrideOrigin, const FVector* OverrideForward) const
{
	OutHits.Reset();

	if (!InstigatorActor || Range <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector Forward = OverrideForward ? *OverrideForward : InstigatorActor->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward = Forward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	const FVector Origin = OverrideOrigin ? *OverrideOrigin : InstigatorActor->GetActorLocation();
	const float HalfAngleDegrees = FMath::Clamp(AngleDegrees * 0.5f, 0.f, 180.f);
	const float HalfAngleRadians = FMath::DegreesToRadians(HalfAngleDegrees);
	const float MaxRangeSq = FMath::Square(Range);
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PrimaryMeleeCone), false, InstigatorActor);
	QueryParams.bTraceComplex = false;
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActor(InstigatorActor);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	TSet<TWeakObjectPtr<AActor>> SeenActors;

	if (bDrawConeTraceDebug)
	{
		const FVector DebugEnd = Origin + Forward * Range;
		DrawDebugSphere(World, Origin, 8.f, 12, ConeTraceDebugColor, false, ConeTraceDebugDuration, 0, 1.f);
		DrawDebugDirectionalArrow(World, Origin, DebugEnd, 30.f, ConeTraceDebugColor, false, ConeTraceDebugDuration, 0, 1.5f);

		// Draw a flat wedge on the horizontal plane so the debug shape matches the actual cone test.
		const int32 DebugSegments = 16;
		FVector PreviousEdge = Origin + Forward.RotateAngleAxis(-HalfAngleDegrees, FVector::UpVector) * Range;
		PreviousEdge.Z = Origin.Z;
		for (int32 SegmentIdx = 1; SegmentIdx <= DebugSegments; ++SegmentIdx)
		{
			const float Alpha = static_cast<float>(SegmentIdx) / static_cast<float>(DebugSegments);
			const float AngleOffset = FMath::Lerp(-HalfAngleDegrees, HalfAngleDegrees, Alpha);
			FVector EdgePoint = Origin + Forward.RotateAngleAxis(AngleOffset, FVector::UpVector) * Range;
			EdgePoint.Z = Origin.Z;

			DrawDebugLine(World, Origin, EdgePoint, ConeTraceDebugColor, false, ConeTraceDebugDuration, 0, 0.75f);
			DrawDebugLine(World, PreviousEdge, EdgePoint, ConeTraceDebugColor, false, ConeTraceDebugDuration, 0, 0.75f);
			PreviousEdge = EdgePoint;
		}
	}

	TArray<FOverlapResult> Overlaps;
	if (!World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(Range),
		QueryParams))
	{
		return;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* TargetActor = Overlap.GetActor();
		if (!TargetActor || TargetActor == InstigatorActor || SeenActors.Contains(TargetActor))
		{
			continue;
		}

		FVector TargetPoint = TargetActor->GetActorLocation();
		UPrimitiveComponent* TargetComponent = Overlap.Component.Get();
		TryResolveTargetCollisionPoint(TargetActor, Origin, TargetPoint, TargetComponent);

		FVector ToTarget = TargetPoint - Origin;
		ToTarget.Z = 0.f;

		const float DistanceSq = ToTarget.SizeSquared();
		if (DistanceSq > MaxRangeSq)
		{
			continue;
		}

		bool bInsideCone = false;
		if (DistanceSq <= KINDA_SMALL_NUMBER)
		{
			bInsideCone = true;
		}
		else
		{
			ToTarget.Normalize();
			bInsideCone = FVector::DotProduct(Forward, ToTarget) >= CosThreshold;
		}

		if (!bInsideCone)
		{
			continue;
		}

		SeenActors.Add(TargetActor);

		const FVector HitNormal = (DistanceSq <= KINDA_SMALL_NUMBER) ? -Forward : -ToTarget;
		FHitResult Hit(TargetActor, TargetComponent, TargetPoint, HitNormal);
		Hit.TraceStart = Origin;
		Hit.TraceEnd = TargetPoint;
		Hit.Location = TargetPoint;
		Hit.ImpactPoint = TargetPoint;
		Hit.ImpactNormal = HitNormal;
		Hit.Normal = HitNormal;
		Hit.Distance = FMath::Sqrt(FVector::DistSquared(Origin, TargetPoint));
		Hit.bBlockingHit = true;
		OutHits.Add(Hit);

		if (bDrawConeTraceDebug)
		{
			DrawDebugPoint(World, TargetPoint, 12.f, ConeTraceDebugColor, false, ConeTraceDebugDuration);
		}
	}
}

AActor* UGA_PrimaryMeleeBasic::ResolvePreferredClickedTarget(const FGameplayAbilityActorInfo* ActorInfo, float MaxAgeSeconds) const
{
	if (!ActorInfo)
	{
		return nullptr;
	}

	const APawn* InstigatorPawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
	const AAeyerjiPlayerController* PC = InstigatorPawn ? Cast<AAeyerjiPlayerController>(InstigatorPawn->GetController()) : nullptr;
	if (!PC)
	{
		return nullptr;
	}

	EMouseNavResult CachedResult = EMouseNavResult::None;
	FVector NavLocation = FVector::ZeroVector;
	FVector CursorLocation = FVector::ZeroVector;
	APawn* ClickedPawn = nullptr;

	// Keep the window short so stale clicks do not override fresh traces.
	if (PC->GetCachedMouseNavContext(CachedResult, NavLocation, CursorLocation, ClickedPawn, MaxAgeSeconds)
		&& CachedResult == EMouseNavResult::ClickedPawn)
	{
		return ClickedPawn;
	}

	return nullptr;
}

bool UGA_PrimaryMeleeBasic::TryBuildHitFromActor(AActor* InstigatorActor, AActor* TargetActor, float MaxRange, FHitResult& OutHit) const
{
	if (!InstigatorActor || !TargetActor)
	{
		return false;
	}

	const FVector Origin = bCachedHitShapeValid ? CachedHitOrigin : InstigatorActor->GetActorLocation();
	FVector TargetLocation = TargetActor->GetActorLocation();
	UPrimitiveComponent* TargetComponent = nullptr;
	TryResolveTargetCollisionPoint(TargetActor, Origin, TargetLocation, TargetComponent);

	const float DistanceSq = FVector::DistSquared2D(Origin, TargetLocation);
	if (MaxRange > 0.f && DistanceSq > FMath::Square(MaxRange))
	{
		return false;
	}

	FVector Direction = bCachedHitShapeValid ? CachedHitForward : (TargetLocation - Origin);
	Direction.Z = 0.f;
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = (TargetLocation - Origin).GetSafeNormal();
	}

	OutHit = FHitResult(TargetActor, TargetComponent, TargetLocation, -Direction);
	OutHit.TraceStart = Origin;
	OutHit.TraceEnd = TargetLocation;
	OutHit.Location = TargetLocation;
	OutHit.ImpactPoint = TargetLocation;
	OutHit.ImpactNormal = -Direction;
	OutHit.Normal = -Direction;
	OutHit.Distance = FMath::Sqrt(DistanceSq);
	OutHit.bBlockingHit = true;

	return true;
}

bool UGA_PrimaryMeleeBasic::TryResolveTargetCollisionPoint(AActor* TargetActor, const FVector& QueryOrigin, FVector& OutTargetPoint, UPrimitiveComponent*& OutTargetComponent) const
{
	OutTargetPoint = TargetActor ? TargetActor->GetActorLocation() : FVector::ZeroVector;
	OutTargetComponent = nullptr;

	if (!TargetActor)
	{
		return false;
	}

	float BestDistanceSq = TNumericLimits<float>::Max();
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(TargetActor);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent || !PrimitiveComponent->IsRegistered() || !PrimitiveComponent->IsCollisionEnabled())
		{
			continue;
		}

		if (PrimitiveComponent->GetCollisionObjectType() != ECC_Pawn)
		{
			continue;
		}

		FVector ClosestPoint = FVector::ZeroVector;
		float DistanceSq = 0.f;
		if (!PrimitiveComponent->GetSquaredDistanceToCollision(QueryOrigin, DistanceSq, ClosestPoint))
		{
			continue;
		}

		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			OutTargetPoint = ClosestPoint;
			OutTargetComponent = PrimitiveComponent;
		}
	}

	if (!OutTargetComponent)
	{
		OutTargetComponent = Cast<UPrimitiveComponent>(TargetActor->GetRootComponent());
	}

	return OutTargetComponent != nullptr;
}

FGameplayAbilityTargetDataHandle UGA_PrimaryMeleeBasic::MakeUniqueTargetData(const TArray<FHitResult>& Hits)
{
	FGameplayAbilityTargetDataHandle Handle;
	for (const FHitResult& Hit : Hits)
	{
		// Bundle each filtered hit into a single-target data entry so downstream code can resolve ASC owners.
		Handle.Add(new FGameplayAbilityTargetData_SingleTargetHit(Hit));
	}
	UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("MakeUniqueTargetData: Created handle for %d hits."), Hits.Num());
	return Handle;
}

void UGA_PrimaryMeleeBasic::SetAbilityPhase(EPrimaryMeleePhase NewPhase)
{
	if (CurrentPhase == NewPhase)
	{
		return;
	}

	const EPrimaryMeleePhase PreviousPhase = CurrentPhase;

	RemoveActivePhaseTag();
	CurrentPhase = NewPhase;

	// Mirror the current phase to loose tags so external systems (like HUD/AI) can react without polling the ability.
	const FGameplayTag NewPhaseTag = GetPhaseTag(NewPhase);
	if (NewPhaseTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(GetCurrentActorInfo()))
		{
			ASC->AddLooseGameplayTag(NewPhaseTag);
		}
		ActivePhaseTag = NewPhaseTag;
	}
	else
	{
		ActivePhaseTag = FGameplayTag();
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("SetAbilityPhase: Previous=%d New=%d"),
		static_cast<int32>(PreviousPhase),
		static_cast<int32>(NewPhase));

	BP_OnAbilityPhaseChanged(NewPhase, PreviousPhase);
}

void UGA_PrimaryMeleeBasic::ClearAbilityPhase()
{
	SetAbilityPhase(EPrimaryMeleePhase::None);
}

FGameplayTag UGA_PrimaryMeleeBasic::GetPhaseTag(EPrimaryMeleePhase Phase) const
{
	switch (Phase)
	{
	case EPrimaryMeleePhase::WindUp:
		return AeyerjiTags::State_Ability_PrimaryMelee_WindUp;
	case EPrimaryMeleePhase::HitWindow:
		return AeyerjiTags::State_Ability_PrimaryMelee_HitWindow;
	case EPrimaryMeleePhase::Recovery:
		return AeyerjiTags::State_Ability_PrimaryMelee_Recovery;
	case EPrimaryMeleePhase::Cancelled:
		return AeyerjiTags::State_Ability_PrimaryMelee_Cancelled;
	default:
		return FGameplayTag();
	}
}

void UGA_PrimaryMeleeBasic::RemoveActivePhaseTag()
{
	if (!ActivePhaseTag.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(GetCurrentActorInfo()))
	{
		if (ASC->HasMatchingGameplayTag(ActivePhaseTag))
		{
			ASC->RemoveLooseGameplayTag(ActivePhaseTag);
		}
	}

	ActivePhaseTag = FGameplayTag();
}

bool UGA_PrimaryMeleeBasic::EnsureAbilityCommitted()
{
	if (bHasCommittedAtImpact)
	{
		return true;
	}

	const bool bAuthority = ShouldProcessServerLogic();
	const bool bPredicting = IsLocallyPredicting();
	if (!bAuthority)
	{
		if (bPredicting)
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("[BossPrimaryAttack] MeleeGA EnsureAbilityCommitted: skipping non-authority commit attempt on predicting client."));
		}

		bHasCommittedAtImpact = true;
		return true;
	}

	if (!CurrentSpecHandle.IsValid() || !CurrentActorInfo)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA EnsureAbilityCommitted: Missing spec handle or actor info."));
		return false;
	}

	if (CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		bHasCommittedAtImpact = true;
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("EnsureAbilityCommitted: Commit succeeded."));
		return true;
	}

	const bool bCostOk = CheckCost(CurrentSpecHandle, CurrentActorInfo);
	const bool bCooldownOk = CheckCooldown(CurrentSpecHandle, CurrentActorInfo);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA EnsureAbilityCommitted: CommitAbility failed (Authority=%s Predicting=%s PredKeyValid=%s CostOk=%s CooldownOk=%s)."),
		bAuthority ? TEXT("true") : TEXT("false"),
		bPredicting ? TEXT("true") : TEXT("false"),
		GetCurrentActivationInfo().GetActivationPredictionKey().IsValidKey() ? TEXT("true") : TEXT("false"),
		bCostOk ? TEXT("true") : TEXT("false"),
		bCooldownOk ? TEXT("true") : TEXT("false"));
	return false;
}

void UGA_PrimaryMeleeBasic::BeginCancelWindow()
{
	if (!IsActive())
	{
		return;
	}

	if (CancelWindowDuration <= 0.f)
	{
		OnCancelWindowExpired();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// This startup timer delays the trace window while movement and cancellation stay locked for the full swing.
		World->GetTimerManager().SetTimer(CancelWindowTimerHandle, this, &UGA_PrimaryMeleeBasic::OnCancelWindowExpired, CancelWindowDuration, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BeginCancelWindow: Armed locked startup window for %.3fs."), CancelWindowDuration);
	}
	else
	{
		OnCancelWindowExpired();
	}
}

void UGA_PrimaryMeleeBasic::OnCancelWindowExpired()
{
	ClearCancelWindowTimer();

	if (!IsActive())
	{
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA OnCancelWindowExpired: startup window elapsed, starting cone strike."));
	SetCanBeCanceled(false);
	SetMovementLock(true);
	StartConeStrike();
}

void UGA_PrimaryMeleeBasic::ClearCancelWindowTimer()
{
	if (UWorld* World = GetWorld())
	{
		if (CancelWindowTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(CancelWindowTimerHandle);
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ClearCancelWindowTimer: Cleared cancel window timer."));
		}
	}
	CancelWindowTimerHandle.Invalidate();
}

void UGA_PrimaryMeleeBasic::SetMovementLock(bool bEnable)
{
	// Drive a shared gameplay tag when possible so movement limitations propagate to other GAS consumers.
	if (!MovementLockTag.IsValid())
	{
		bMovementLocked = bEnable;
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(GetCurrentActorInfo()))
	{
		if (bEnable)
		{
			if (!bMovementLocked)
			{
				ASC->AddLooseGameplayTag(MovementLockTag);
				bMovementLocked = true;
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("SetMovementLock: Applied movement lock tag."));
			}
		}
		else
		{
			if (bMovementLocked && ASC->HasMatchingGameplayTag(MovementLockTag))
			{
				ASC->RemoveLooseGameplayTag(MovementLockTag);
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("SetMovementLock: Removed movement lock tag."));
			}
			bMovementLocked = false;
		}
	}
	else
	{
		bMovementLocked = bEnable;
	}
}

void UGA_PrimaryMeleeBasic::ResetComboRuntimeState()
{
	CurrentComboIndex = INDEX_NONE;
	ComboStagesExecuted = 0;
	bComboInputBuffered = false;
	RuntimeComboMontages.Reset();
}

void UGA_PrimaryMeleeBasic::ClearComboResetTimer()
{
	if (!ComboResetTimerHandle.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ClearComboResetTimer: Cleared pending combo reset timer."));
	}

	ComboResetTimerHandle.Invalidate();
}

void UGA_PrimaryMeleeBasic::OnComboResetTimerExpired()
{
	ComboResetTimerHandle.Invalidate();
	NextComboIndex = 0;
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnComboResetTimerExpired: Reset NextComboIndex to 0."));
}

void UGA_PrimaryMeleeBasic::ScheduleComboReset()
{
	if (ComboStagesExecuted <= 0)
	{
		NextComboIndex = 0;
		return;
	}

	if (ComboResetDelay <= 0.f)
	{
		NextComboIndex = 0;
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ScheduleComboReset: Immediate reset (delay <= 0)."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ComboResetTimerHandle, this, &UGA_PrimaryMeleeBasic::OnComboResetTimerExpired, ComboResetDelay, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ScheduleComboReset: Armed combo reset in %.3fs (NextComboIndex=%d)."),
			ComboResetDelay,
			NextComboIndex);
	}
	else
	{
		NextComboIndex = 0;
	}
}

void UGA_PrimaryMeleeBasic::RefreshComboMontagesFromAvatar(const FGameplayAbilityActorInfo* ActorInfo)
{
	RuntimeComboMontages.Reset();

	if (!ActorInfo)
	{
		return;
	}

	AActor* AvatarActor = ActorInfo->AvatarActor.Get();
	if (!AvatarActor)
	{
		return;
	}

	if (!AvatarActor->GetClass()->ImplementsInterface(UPrimaryMeleeComboProviderInterface::StaticClass()))
	{
		return;
	}

	const int32 MaxCombos = FMath::Max(1, MaxProviderComboMontages);

	TArray<UAnimMontage*> ProviderMontages;
	IPrimaryMeleeComboProviderInterface::Execute_GetPrimaryMeleeComboMontages(AvatarActor, ProviderMontages);

	for (UAnimMontage* Montage : ProviderMontages)
	{
		if (!Montage)
		{
			continue;
		}

		RuntimeComboMontages.Add(Montage);
		if (RuntimeComboMontages.Num() >= MaxCombos)
		{
			break;
		}
	}

	if (RuntimeComboMontages.Num() > 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("RefreshComboMontagesFromAvatar: Using %d provider montages from %s (Max=%d)."),
			RuntimeComboMontages.Num(),
			*GetNameSafe(AvatarActor),
			MaxCombos);
	}
}

int32 UGA_PrimaryMeleeBasic::CompactRuntimeComboMontages()
{
	int32 RemovedEntries = 0;
	for (int32 Index = RuntimeComboMontages.Num() - 1; Index >= 0; --Index)
	{
		if (!RuntimeComboMontages[Index].IsValid())
		{
			RuntimeComboMontages.RemoveAt(Index);
			++RemovedEntries;
		}
	}

	if (RemovedEntries > 0)
	{
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("CompactRuntimeComboMontages: Removed %d stale entries."), RemovedEntries);
	}

	return RuntimeComboMontages.Num();
}

int32 UGA_PrimaryMeleeBasic::GetConfiguredComboCount(const FGameplayAbilityActorInfo* ActorInfo)
{
	const int32 RuntimeCount = CompactRuntimeComboMontages();
	if (RuntimeCount > 0)
	{
		return RuntimeCount;
	}

	if (ComboMontages.Num() > 0)
	{
		int32 ValidCount = 0;
		for (int32 Index = 0; Index < ComboMontages.Num(); ++Index)
		{
			const TSoftObjectPtr<UAnimMontage>& SoftMontage = ComboMontages[Index];
			if (SoftMontage.IsNull())
			{
				continue;
			}

			UAnimMontage* LoadedMontage = SoftMontage.IsValid() ? SoftMontage.Get() : SoftMontage.LoadSynchronous();
			if (LoadedMontage)
			{
				++ValidCount;
			}
			else
			{
				UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("GetConfiguredComboCount: ComboMontages[%d] is null or failed to load."), Index);
			}
		}

		if (ValidCount > 0)
		{
			return ValidCount;
		}
	}

	return ResolveSingleFallbackMontage(ActorInfo) ? 1 : 0;
}

UAnimMontage* UGA_PrimaryMeleeBasic::ResolveComboMontage(const FGameplayAbilityActorInfo* ActorInfo, int32 ComboIndex)
{
	const int32 RuntimeCount = CompactRuntimeComboMontages();
	if (RuntimeCount > 0)
	{
		int32 ValidIndex = 0;
		for (const TWeakObjectPtr<UAnimMontage>& WeakMontage : RuntimeComboMontages)
		{
			if (!WeakMontage.IsValid())
			{
				continue;
			}

			if (ValidIndex == ComboIndex)
			{
				return WeakMontage.Get();
			}

			++ValidIndex;
		}
	}

	if (ComboMontages.Num() > 0)
	{
		int32 ValidIndex = 0;
		for (int32 Index = 0; Index < ComboMontages.Num(); ++Index)
		{
			const TSoftObjectPtr<UAnimMontage>& SoftMontage = ComboMontages[Index];
			if (SoftMontage.IsNull())
			{
				continue;
			}

			UAnimMontage* LoadedMontage = SoftMontage.IsValid() ? SoftMontage.Get() : SoftMontage.LoadSynchronous();
			if (!LoadedMontage)
			{
				continue;
			}

			if (ValidIndex == ComboIndex)
			{
				return LoadedMontage;
			}

			++ValidIndex;
		}
	}

	if (ComboIndex == 0)
	{
		return ResolveSingleFallbackMontage(ActorInfo);
	}

	UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ResolveComboMontage: Requested combo index %d but no montage resolved."), ComboIndex);
	return nullptr;
}

UAnimMontage* UGA_PrimaryMeleeBasic::ResolveSingleFallbackMontage(const FGameplayAbilityActorInfo* ActorInfo) const
{
	if (ActorInfo)
	{
		if (UAnimMontage* SelectedMontage = SelectAttackMontage(*ActorInfo))
		{
			return SelectedMontage;
		}
	}

	if (!AttackMontage.IsNull())
	{
		return AttackMontage.IsValid() ? AttackMontage.Get() : AttackMontage.LoadSynchronous();
	}

	return nullptr;
}

float UGA_PrimaryMeleeBasic::CalculateMontagePlayRate(float AttackSpeed) const
{
	const float EffectiveBaseline = FMath::Max(BaselineAttackSpeed, kMinAttackSpeed);
	return FMath::Max(AttackSpeed / EffectiveBaseline, kMinAttackSpeed);
}

float UGA_PrimaryMeleeBasic::ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const
{
	float AttackRate = BaselineAttackSpeed;

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
        if (const UAeyerjiAttributeSet* Attr = ActorInfo->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>())
        {
            const float AttributeAttackSpeed = Attr->GetAttackSpeed();
            if (AttributeAttackSpeed > KINDA_SMALL_NUMBER)
            {
                // AttackSpeed is stored as a "rating" where 100 == 1 attack/sec. Convert to real APS.
                AttackRate = AttributeAttackSpeed / 100.f;
            }
            else
            {
                const float AttributeCooldownSeconds = Attr->GetAttackCooldown();
                if (AttributeCooldownSeconds > KINDA_SMALL_NUMBER)
                {
                    AttackRate = 1.f / AttributeCooldownSeconds;
                }
            }
        }
	}

	return FMath::Max(AttackRate, kMinAttackSpeed);
}

bool UGA_PrimaryMeleeBasic::StartComboStage(int32 ComboIndex, const FGameplayAbilityActorInfo* ActorInfo, float AttackSpeed)
{
	if (!ActorInfo)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartComboStage: ActorInfo invalid."));
		return false;
	}

	const int32 ComboCount = GetConfiguredComboCount(ActorInfo);
	if (ComboCount <= 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartComboStage: No valid combo montages available."));
		return false;
	}

	const int32 ClampedIndex = FMath::Clamp(ComboIndex, 0, ComboCount - 1);
	UAnimMontage* MontageToPlay = ResolveComboMontage(ActorInfo, ClampedIndex);
	if (!MontageToPlay)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartComboStage: Failed to resolve montage for combo index %d."), ClampedIndex);
		return false;
	}

	ClearComboResetTimer();

	const int32 PreviousStageCount = ComboStagesExecuted;
	CurrentComboIndex = ClampedIndex;

	DamagedActors.Reset();
	bComboInputBuffered = false;

	ClearCancelWindowTimer();
	ClearConeTraceTimer();
	SetAbilityPhase(EPrimaryMeleePhase::WindUp);
	SetMovementLock(true);
	SetCanBeCanceled(false);
	BeginCancelWindow();

	const float FinalAttackSpeed = FMath::Max(AttackSpeed, kMinAttackSpeed);
	CurrentMontagePlayRate = CalculateMontagePlayRate(FinalAttackSpeed);

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartComboStage: Stage=%d/%d AttackSpeed=%.3f Montage=%s"),
		ClampedIndex,
		ComboCount,
		FinalAttackSpeed,
		*GetNameSafe(MontageToPlay));

	if (!StartMontage(FinalAttackSpeed, MontageToPlay))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartComboStage: StartMontage failed for combo index %d."), ClampedIndex);
		CurrentComboIndex = INDEX_NONE;
		ComboStagesExecuted = PreviousStageCount;
		return false;
	}

	ComboStagesExecuted = PreviousStageCount + 1;
	NextComboIndex = (ClampedIndex + 1) % ComboCount;

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartComboStage: Success (ComboStagesExecuted=%d NextComboIndex=%d)."),
		ComboStagesExecuted,
		NextComboIndex);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[BossPrimaryAttack] MeleeGA StartComboStage: success stage=%d comboCount=%d montage=%s nextComboIndex=%d."),
		ClampedIndex,
		ComboCount,
		*GetNameSafe(MontageToPlay),
		NextComboIndex);

	return true;
}

bool UGA_PrimaryMeleeBasic::TryLaunchBufferedCombo()
{
	if (!bComboInputBuffered)
	{
		return false;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo)
	{
		bComboInputBuffered = false;
		return false;
	}

	const int32 ComboCount = GetConfiguredComboCount(ActorInfo);
	if (ComboCount <= 1)
	{
		bComboInputBuffered = false;
		return false;
	}

	if (ComboStagesExecuted >= ComboCount)
	{
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("TryLaunchBufferedCombo: Combo stages exhausted (%d/%d)."),
			ComboStagesExecuted,
			ComboCount);
		bComboInputBuffered = false;
		return false;
	}

	const int32 StageToStart = NextComboIndex;
	bComboInputBuffered = false;

	const float AttackSpeed = ResolveAttackSpeed(ActorInfo);
	if (!StartComboStage(StageToStart, ActorInfo, AttackSpeed))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("TryLaunchBufferedCombo: StartComboStage failed for index %d."), StageToStart);
		return false;
	}

	return true;
}

UAnimMontage* UGA_PrimaryMeleeBasic::SelectAttackMontage_Implementation(const FGameplayAbilityActorInfo& ActorInfo) const
{
	if (AActor* AvatarActor = ActorInfo.AvatarActor.Get())
	{
		if (UAeyerjiEnemyArchetypeComponent* ArchetypeComp = AvatarActor->FindComponentByClass<UAeyerjiEnemyArchetypeComponent>())
		{
			if (UAnimMontage* ArchetypeMontage = ArchetypeComp->GetAttackMontage())
			{
				return ArchetypeMontage;
			}
		}
	}

	UAnimMontage* Result = AttackMontage.IsValid() ? AttackMontage.Get() : nullptr;
	UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("SelectAttackMontage: Actor=%s Result=%s"),
		*GetNameSafe(ActorInfo.AvatarActor.Get()),
		*GetNameSafe(Result));
	return Result;
}
