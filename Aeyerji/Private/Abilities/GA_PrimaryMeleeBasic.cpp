#include "Abilities/GA_PrimaryMeleeBasic.h"

#include "AbilitySystemComponent.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Combat/AeyerjiMeleeDeterministicStrike.h"
#include "Combat/PrimaryMeleeComboProviderInterface.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
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
#include "HAL/IConsoleManager.h"
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
	constexpr float kPrimaryMeleeDebugDuration = 0.2f;

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarPrimaryMeleeDebugDraw(
		TEXT("aeyerji.PrimaryMelee.DebugDraw"),
		0,
		TEXT("Draws primary melee target selection range/cone debug shapes when non-zero."),
		ECVF_Default);
#endif

	bool ShouldDrawPrimaryMeleeDebug()
	{
#if !UE_BUILD_SHIPPING
		return CVarPrimaryMeleeDebugDraw.GetValueOnGameThread() != 0;
#else
		return false;
#endif
	}

	AActor* ResolveExplicitEventTarget(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* TriggerEventData,
		const bool bAllowFriendlyDamage)
	{
		AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
		if (!IsValid(AvatarActor) || !TriggerEventData)
		{
			return nullptr;
		}

		auto IsValidEventTarget = [AvatarActor, bAllowFriendlyDamage](AActor* Candidate)
		{
			if (!IsValid(Candidate)
				|| Candidate == AvatarActor
				|| Candidate->GetWorld() != AvatarActor->GetWorld()
				|| (!bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(AvatarActor, Candidate)))
			{
				return false;
			}
			if (const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate, true))
			{
				if (TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
				{
					return false;
				}
			}
			return !Candidate->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName());
		};

		if (AActor* DirectTarget = const_cast<AActor*>(TriggerEventData->Target.Get()))
		{
			if (IsValidEventTarget(DirectTarget))
			{
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] ResolveExplicitEventTarget direct target accepted. Avatar=%s Target=%s EventTag=%s"),
					*GetNameSafe(AvatarActor),
					*GetNameSafe(DirectTarget),
					*TriggerEventData->EventTag.ToString());
				return DirectTarget;
			}
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] ResolveExplicitEventTarget direct target rejected. Avatar=%s Target=%s EventTag=%s"),
				*GetNameSafe(AvatarActor),
				*GetNameSafe(DirectTarget),
				*TriggerEventData->EventTag.ToString());
		}

		for (int32 Index = 0; Index < TriggerEventData->TargetData.Num(); ++Index)
		{
			const FGameplayAbilityTargetData* Data = TriggerEventData->TargetData.Get(Index);
			if (!Data)
			{
				continue;
			}

			if (const FHitResult* HitResult = Data->GetHitResult())
			{
				if (AActor* HitActor = HitResult->GetActor())
				{
					if (IsValidEventTarget(HitActor))
					{
						UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] ResolveExplicitEventTarget target-data hit accepted. Avatar=%s Target=%s EventTag=%s"),
							*GetNameSafe(AvatarActor),
							*GetNameSafe(HitActor),
							*TriggerEventData->EventTag.ToString());
						return HitActor;
					}
				}
			}

			const TArray<TWeakObjectPtr<AActor>> Actors = Data->GetActors();
			for (const TWeakObjectPtr<AActor>& WeakActor : Actors)
			{
				if (WeakActor.IsValid() && IsValidEventTarget(WeakActor.Get()))
				{
					UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] ResolveExplicitEventTarget target-data actor accepted. Avatar=%s Target=%s EventTag=%s"),
						*GetNameSafe(AvatarActor),
						*GetNameSafe(WeakActor.Get()),
						*TriggerEventData->EventTag.ToString());
					return WeakActor.Get();
				}
			}
		}

		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] ResolveExplicitEventTarget found no valid hostile target. Avatar=%s EventTag=%s TargetDataNum=%d"),
			*GetNameSafe(AvatarActor),
			*TriggerEventData->EventTag.ToString(),
			TriggerEventData->TargetData.Num());
		return nullptr;
	}

}

UGA_PrimaryMeleeBasic::UGA_PrimaryMeleeBasic()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	bRetriggerInstancedAbility = false;

	BaselineAttackSpeed              = 1.f;
	DamageScalar                     = 1.f;
	DefaultDamageTypeTag             = AeyerjiTags::DamageType_Physical;
	DefaultDamageRules.bUseVariance = true;
	DefaultDamageRules.bCanCrit = true;
	DefaultDamageRules.bCanBeDodged = true;
	DefaultDamageRules.bCanLifeSteal = true;
	DefaultDamageRules.bCanTriggerOnHit = true;
	DefaultDamageRules.bCanStagger = true;
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
	bExternalRetargetBlocksCombo     = false;
	ComboResetTimerHandle.Invalidate();
	DeterministicStrikeTimerHandle.Invalidate();
	DeterministicStrikeSequence      = 0;
	ActiveDeterministicStrikeId      = INDEX_NONE;
	ActiveDeterministicStrikeComboIndex = INDEX_NONE;
	bDeterministicStrikePending      = false;
	bDeterministicStrikeResolved     = false;
	DeterministicStrikeTarget.Reset();
	DeterministicStrikeTargetSource  = NAME_None;
	DeterministicStrikeImpactDelay   = 0.f;

	// Prime the asset/activation tags used to gate other abilities and expose state to other systems.
	{
		FGameplayTagContainer AbilityAssetTags = GetAssetTags();
		AbilityAssetTags.AddTag(AeyerjiTags::Ability_Primary);
		AbilityAssetTags.AddTag(AeyerjiTags::Ability_Primary_Melee_Basic);
		SetAssetTags(AbilityAssetTags);

		ActivationOwnedTags.AddTag(AeyerjiTags::Ability_Primary);
		ActivationBlockedTags.Reset();
		ActivationBlockedTags.AddTag(AeyerjiTags::State_Dead);
		ActivationBlockedTags.AddTag(AeyerjiTags::State_CrowdControl_Staggered);
		ActivationBlockedTags.AddTag(AeyerjiTags::State_CrowdControl_Stunned);
		ActivationBlockedTags.AddTag(AeyerjiTags::State_Ability_Casting);
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

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("Constructed %s (BaselineAttackSpeed=%.2f DamageScalar=%.2f)"),
		*GetNameSafe(this),
		BaselineAttackSpeed,
		DamageScalar);
}

bool UGA_PrimaryMeleeBasic::ShouldAbilityRespondToEvent(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	AActor* ExplicitTarget = ResolveExplicitEventTarget(ActorInfo, Payload, bAllowFriendlyDamage);
	if (!AvatarActor || !ExplicitTarget)
	{
		// Non-targeted activation paths still perform their normal cone attack.
		return true;
	}

	float AttackRange = ConeTraceRangeFallback;
	if (const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		const FGameplayAttribute RangeAttribute = UAeyerjiAttributeSet::GetAttackRangeAttribute();
		const float AttributeRange = RangeAttribute.IsValid()
			? ASC->GetNumericAttribute(RangeAttribute)
			: 0.f;
		if (FMath::IsFinite(AttributeRange) && AttributeRange > KINDA_SMALL_NUMBER)
		{
			AttackRange = AttributeRange;
		}
	}
	AttackRange = FMath::IsFinite(AttackRange) ? FMath::Max(0.f, AttackRange) : 0.f;

	FHitResult TargetHit;
	const bool bTargetInRange = TryBuildHitFromActor(AvatarActor, ExplicitTarget, AttackRange, TargetHit);
	if (!bTargetInRange)
	{
		UE_LOG(LogPrimaryMeleeGA, Log,
			TEXT("[MouseAttack] Primary melee event deferred: explicit target moved outside strike range. Avatar=%s Target=%s Range=%.1f."),
			*GetNameSafe(AvatarActor),
			*GetNameSafe(ExplicitTarget),
			AttackRange);
	}
	return bTargetInRange;
}

void UGA_PrimaryMeleeBasic::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
											const FGameplayAbilityActorInfo* ActorInfo,
											const FGameplayAbilityActivationInfo ActivationInfo,
											const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!ActorInfo || !IsValid(AvatarActor) || IsOwnerDead(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && ASC->HasMatchingGameplayTag(AeyerjiTags::State_Ability_Casting))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: blocked while ability cast lock is active."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	AEnemyAIController* EnemyAI = ResolveEnemyAIController(ActorInfo);
	AActor* RawTargetActor = EnemyAI ? EnemyAI->GetTargetActor() : nullptr;
	const bool bFriendlyTargetBlocked = RawTargetActor && !bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(AvatarActor, RawTargetActor);
	AActor* FilteredTargetActor = IsValidDeterministicTarget(AvatarActor, RawTargetActor) ? RawTargetActor : nullptr;
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
	ResetDeterministicStrikeState();
	SetMovementLock(false);
	SetCanBeCanceled(true);
	RefreshComboMontagesFromAvatar(ActorInfo);

	const float AttackSpeed = ResolveAttackSpeed(ActorInfo);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Final AttackSpeed=%.3f (Baseline=%.3f) DamagedActors cleared."),
		AttackSpeed,
		BaselineAttackSpeed);

	StartupClickedTarget = ResolveExplicitEventTarget(ActorInfo, TriggerEventData, bAllowFriendlyDamage);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MouseAttack] Melee activation explicit target. Avatar=%s StartupClickedTarget=%s TriggerTag=%s"),
		*GetNameSafe(AvatarActor),
		*GetNameSafe(StartupClickedTarget.Get()),
		TriggerEventData ? *TriggerEventData->EventTag.ToString() : TEXT("None"));
	if (ShouldProcessServerLogic() || IsLocallyPredicting())
	{
		AActor* FacingTarget = FilteredTargetActor ? FilteredTargetActor : StartupClickedTarget.Get();
		if (FacingTarget && FacingTarget != AvatarActor)
		{
			FVector ToTarget = FacingTarget->GetActorLocation() - AvatarActor->GetActorLocation();
			ToTarget.Z = 0.f;
			if (!ToTarget.ContainsNaN() && !ToTarget.IsNearlyZero())
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

void UGA_PrimaryMeleeBasic::NotifyExternalRetarget(AActor* NewTarget)
{
	if (!IsActive() || !IsValid(NewTarget))
	{
		return;
	}

	AActor* CapturedTarget = DeterministicStrikeTarget.Get();
	if (!CapturedTarget)
	{
		CapturedTarget = StartupClickedTarget.Get();
	}

	if (CapturedTarget == NewTarget)
	{
		return;
	}

	const bool bHadBufferedCombo = bComboInputBuffered;
	bComboInputBuffered = false;
	bExternalRetargetBlocksCombo = true;

	UE_LOG(LogPrimaryMeleeGA, Log,
		TEXT("[MouseAttack] External retarget suppresses stale combo follow-up. CapturedTarget=%s NewTarget=%s HadBufferedCombo=%s Stage=%d Phase=%d."),
		*GetNameSafe(CapturedTarget),
		*GetNameSafe(NewTarget),
		bHadBufferedCombo ? TEXT("true") : TEXT("false"),
		CurrentComboIndex,
		static_cast<int32>(CurrentPhase));
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
	CancelDeterministicStrike(TEXT("CancelAbility"));
	StopMontageTask();
	ClearCancelWindowTimer();
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

	if (!bWasCancelled && FAeyerjiMeleeDeterministicStrikePolicy::ShouldResolveOnMontageFinish(false, bDeterministicStrikePending, bDeterministicStrikeResolved))
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("[MeleeDeterministic] Normal EndAbility is flushing pending strike Id=%d before cleanup."),
			ActiveDeterministicStrikeId);
		ResolveDeterministicStrike();
	}
	else if (bWasCancelled)
	{
		CancelDeterministicStrike(TEXT("EndAbilityCancelled"));
	}

	ClearMontageFailsafeTimer();
	StopMontageTask();
	ResetDeterministicStrikeState();

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

	FGameplayEffectSpecHandle SpecHandle =
		MakeOutgoingGameplayEffectSpec(CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));

	if (SpecHandle.IsValid())
	{
		ApplyResolvedCooldownTagsToSpec(SpecHandle);
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
		const float SafePlayRate = FMath::IsFinite(PlayRate) ? FMath::Max(PlayRate, KINDA_SMALL_NUMBER) : 1.f;
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

void UGA_PrimaryMeleeBasic::ClearDeterministicStrikeTimer()
{
	if (UWorld* World = GetWorld())
	{
		if (DeterministicStrikeTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(DeterministicStrikeTimerHandle);
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("[MeleeDeterministic] Cleared pending strike timer Id=%d."),
				ActiveDeterministicStrikeId);
		}
	}

	DeterministicStrikeTimerHandle.Invalidate();
}

void UGA_PrimaryMeleeBasic::ResetDeterministicStrikeState()
{
	ClearDeterministicStrikeTimer();
	ActiveDeterministicStrikeId = INDEX_NONE;
	ActiveDeterministicStrikeComboIndex = INDEX_NONE;
	bDeterministicStrikePending = false;
	bDeterministicStrikeResolved = false;
	DeterministicStrikeTarget.Reset();
	DeterministicStrikeTargetSource = NAME_None;
	DeterministicStrikeImpactDelay = 0.f;
}

void UGA_PrimaryMeleeBasic::CancelDeterministicStrike(const TCHAR* Reason)
{
	const bool bShouldLogCancel = FAeyerjiMeleeDeterministicStrikePolicy::ShouldCancelOnHardCancel(
		bDeterministicStrikePending,
		bDeterministicStrikeResolved);

	if (bShouldLogCancel && ShouldProcessServerLogic())
	{
		UE_LOG(LogPrimaryMeleeGA, Display,
			TEXT("[MeleeDeterministic] Strike cancelled Id=%d Stage=%d Reason=%s Target=%s."),
			ActiveDeterministicStrikeId,
			ActiveDeterministicStrikeComboIndex,
			Reason ? Reason : TEXT("Unknown"),
			*GetNameSafe(DeterministicStrikeTarget.Get()));
	}

	ClearDeterministicStrikeTimer();
	bDeterministicStrikePending = false;
	bDeterministicStrikeResolved = true;
	DeterministicStrikeTarget.Reset();
	DeterministicStrikeTargetSource = NAME_None;
	DeterministicStrikeImpactDelay = 0.f;
}

bool UGA_PrimaryMeleeBasic::ScheduleDeterministicStrike(int32 ComboIndex, float AttackSpeed)
{
	if (!IsActive())
	{
		return false;
	}

	if (!EnsureAbilityCommitted())
	{
		UE_LOG(LogPrimaryMeleeGA, Warning,
			TEXT("[MeleeDeterministic] Commit failed before scheduling deterministic strike Stage=%d."),
			ComboIndex);
		return false;
	}

	ClearDeterministicStrikeTimer();
	++DeterministicStrikeSequence;
	ActiveDeterministicStrikeId = DeterministicStrikeSequence;
	ActiveDeterministicStrikeComboIndex = ComboIndex;
	bDeterministicStrikePending = true;
	bDeterministicStrikeResolved = false;
	DeterministicStrikeImpactDelay = FAeyerjiMeleeDeterministicStrikePolicy::CalculateImpactDelay(
		CancelWindowDuration,
		ConeStrikeDelay,
		CurrentMontagePlayRate);
	DeterministicStrikeTarget.Reset();
	DeterministicStrikeTargetSource = NAME_None;

	AActor* InstigatorActor = GetAvatarActorFromActorInfo();
	const float StartupAttackRange = ResolveAttackRange();
	auto TryLockStartupTarget = [&](AActor* CandidateTarget, const FName SourceName)
	{
		if (!CandidateTarget || !InstigatorActor || DeterministicStrikeTarget.IsValid())
		{
			return;
		}

		if (!IsValidDeterministicTarget(InstigatorActor, CandidateTarget))
		{
			return;
		}

		FHitResult StartupHit;
		if (!TryBuildHitFromActor(InstigatorActor, CandidateTarget, StartupAttackRange, StartupHit))
		{
			if (ShouldProcessServerLogic())
			{
				UE_LOG(LogPrimaryMeleeGA, Verbose,
					TEXT("[MeleeDeterministic] Startup target not locked Id=%d Stage=%d Target=%s Source=%s Reason=OutsideAttackRange Range=%.1f."),
					ActiveDeterministicStrikeId,
					ActiveDeterministicStrikeComboIndex,
					*GetNameSafe(CandidateTarget),
					*SourceName.ToString(),
					StartupAttackRange);
			}
			return;
		}

		DeterministicStrikeTarget = CandidateTarget;
		DeterministicStrikeTargetSource = SourceName;
	};

	TryLockStartupTarget(ResolveEnemyTargetActor(CurrentActorInfo), FName(TEXT("EnemyTarget")));
	if (!DeterministicStrikeTarget.IsValid() && StartupClickedTarget.IsValid())
	{
		TryLockStartupTarget(StartupClickedTarget.Get(), FName(TEXT("ClickedTarget_Activation")));
	}

	if (ShouldProcessServerLogic())
	{
		UE_LOG(LogPrimaryMeleeGA, Display,
			TEXT("[MeleeDeterministic] Strike scheduled Id=%d Stage=%d AttackSpeed=%.3f PlayRate=%.3f ImpactDelay=%.4f Target=%s Source=%s."),
			ActiveDeterministicStrikeId,
			ActiveDeterministicStrikeComboIndex,
			AttackSpeed,
			CurrentMontagePlayRate,
			DeterministicStrikeImpactDelay,
			*GetNameSafe(DeterministicStrikeTarget.Get()),
			*DeterministicStrikeTargetSource.ToString());
	}
	else
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose,
			TEXT("[MeleeDeterministic] Predictive strike scheduled Id=%d Stage=%d ImpactDelay=%.4f Target=%s Source=%s."),
			ActiveDeterministicStrikeId,
			ActiveDeterministicStrikeComboIndex,
			DeterministicStrikeImpactDelay,
			*GetNameSafe(DeterministicStrikeTarget.Get()),
			*DeterministicStrikeTargetSource.ToString());
	}

	if (DeterministicStrikeImpactDelay <= KINDA_SMALL_NUMBER)
	{
		ResolveDeterministicStrike();
		return true;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DeterministicStrikeTimerHandle,
			this,
			&UGA_PrimaryMeleeBasic::ResolveDeterministicStrike,
			DeterministicStrikeImpactDelay,
			false);
		return true;
	}

	ResolveDeterministicStrike();
	return true;
}

void UGA_PrimaryMeleeBasic::CaptureDeterministicStrikeShape(AActor* InstigatorActor)
{
	bCachedHitShapeValid = false;
	CachedHitOrigin = FVector::ZeroVector;
	CachedHitForward = FVector::ZeroVector;

	if (!InstigatorActor)
	{
		return;
	}

	CachedHitOrigin = InstigatorActor->GetActorLocation();
	CachedHitForward = InstigatorActor->GetActorForwardVector();
	if (CachedHitOrigin.ContainsNaN() || CachedHitForward.ContainsNaN())
	{
		return;
	}

	if (const ACharacter* Character = Cast<ACharacter>(InstigatorActor))
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			static const FName StartSocket(TEXT("WeaponRHandSocket"));
			static const FName EndSocket(TEXT("WeaponTip"));
			if (Mesh->DoesSocketExist(StartSocket) && Mesh->DoesSocketExist(EndSocket))
			{
				const FVector Start = Mesh->GetSocketLocation(StartSocket);
				const FVector End = Mesh->GetSocketLocation(EndSocket);
				CachedHitOrigin = Start;
				CachedHitForward = End - Start;
			}
			else if (CachedHitForward.IsNearlyZero())
			{
				CachedHitForward = Mesh->GetForwardVector();
			}
		}
	}

	CachedHitForward.Z = 0.f;
	if (!CachedHitForward.Normalize())
	{
		CachedHitForward = InstigatorActor->GetActorForwardVector().GetSafeNormal();
	}

	bCachedHitShapeValid = !CachedHitOrigin.ContainsNaN()
		&& !CachedHitForward.ContainsNaN()
		&& !CachedHitForward.IsNearlyZero();
}

bool UGA_PrimaryMeleeBasic::IsDeadForDeterministicStrike(AActor* TargetActor) const
{
	if (!IsValid(TargetActor))
	{
		return true;
	}
	const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor, true);
	return (TargetASC && TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		|| TargetActor->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName());
}

void UGA_PrimaryMeleeBasic::ResolveDeterministicStrike()
{
	if (!bDeterministicStrikePending || bDeterministicStrikeResolved)
	{
		ClearDeterministicStrikeTimer();
		return;
	}

	const int32 StrikeId = ActiveDeterministicStrikeId;
	const int32 StrikeStage = ActiveDeterministicStrikeComboIndex;
	const bool bServerLogic = ShouldProcessServerLogic();
	const bool bPredicting = IsLocallyPredicting();
	AActor* CapturedTarget = DeterministicStrikeTarget.Get();
	const FName CapturedTargetSource = DeterministicStrikeTargetSource;

	ClearDeterministicStrikeTimer();
	bDeterministicStrikePending = false;
	bDeterministicStrikeResolved = true;

	if (!IsActive())
	{
		if (bServerLogic)
		{
			UE_LOG(LogPrimaryMeleeGA, Display,
				TEXT("[MeleeDeterministic] Strike skipped Id=%d Stage=%d Reason=AbilityInactive Target=%s."),
				StrikeId,
				StrikeStage,
				*GetNameSafe(CapturedTarget));
		}
		return;
	}

	if (CurrentPhase == EPrimaryMeleePhase::Cancelled)
	{
		if (bServerLogic)
		{
			UE_LOG(LogPrimaryMeleeGA, Display,
				TEXT("[MeleeDeterministic] Strike skipped Id=%d Stage=%d Reason=Cancelled Target=%s."),
				StrikeId,
				StrikeStage,
				*GetNameSafe(CapturedTarget));
		}
		return;
	}

	if (!EnsureAbilityCommitted())
	{
		if (bServerLogic)
		{
			UE_LOG(LogPrimaryMeleeGA, Warning,
				TEXT("[MeleeDeterministic] Strike skipped Id=%d Stage=%d Reason=CommitFailed Target=%s."),
				StrikeId,
				StrikeStage,
				*GetNameSafe(CapturedTarget));
		}
		return;
	}

	AActor* InstigatorActor = GetAvatarActorFromActorInfo();
	if (!InstigatorActor)
	{
		if (bServerLogic)
		{
			UE_LOG(LogPrimaryMeleeGA, Warning,
				TEXT("[MeleeDeterministic] Strike skipped Id=%d Stage=%d Reason=InvalidInstigator."),
				StrikeId,
				StrikeStage);
		}
		return;
	}

	const float AttackRange = ResolveAttackRange();
	const float LockedTargetGraceRange = FAeyerjiMeleeDeterministicStrikePolicy::CalculateLockedTargetGraceRange(
		AttackRange,
		LockedTargetGraceRangeMultiplier);
	const float ConeAngle = ResolveAttackAngleDegrees();
	const bool bHasCleaveAngle = FAeyerjiMeleeDeterministicStrikePolicy::IsCleaveAngle(ConeAngle);

	if (CapturedTarget && IsValidDeterministicTarget(InstigatorActor, CapturedTarget))
	{
		RefaceInstigatorTowardTargetIfNeeded(InstigatorActor, CapturedTarget);
	}

	SetAbilityPhase(EPrimaryMeleePhase::HitWindow);
	SetMovementLock(true);
	SetCanBeCanceled(false);
	CaptureDeterministicStrikeShape(InstigatorActor);

	TArray<FHitResult> UniqueHits;
	UniqueHits.Reserve(8);
	FString MissReason = CapturedTarget ? TEXT("NoQualifiedTargets") : TEXT("NoPreferredTarget");

	auto TryRegisterHit = [&](const FHitResult& Hit, const TCHAR* SourceLabel)
	{
		AActor* TargetActor = Hit.GetActor();
		if (!TargetActor)
		{
			MissReason = TEXT("InvalidActor");
			return false;
		}

		if (TargetActor == InstigatorActor)
		{
			MissReason = TEXT("SelfTarget");
			return false;
		}

		if (IsDeadForDeterministicStrike(TargetActor))
		{
			MissReason = TEXT("DeadTarget");
			UE_LOG(LogPrimaryMeleeGA, Verbose,
				TEXT("[MeleeDeterministic] Candidate skipped Id=%d Target=%s Source=%s Reason=DeadTarget."),
				StrikeId,
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		if (!bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(InstigatorActor, TargetActor))
		{
			MissReason = TEXT("FriendlyTarget");
			UE_LOG(LogPrimaryMeleeGA, Verbose,
				TEXT("[MeleeDeterministic] Candidate skipped Id=%d Target=%s Source=%s Reason=FriendlyTarget."),
				StrikeId,
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		if (DamagedActors.Contains(WeakTarget))
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose,
				TEXT("[MeleeDeterministic] Candidate skipped Id=%d Target=%s Source=%s Reason=AlreadyDamaged."),
				StrikeId,
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		DamagedActors.Add(WeakTarget);
		UniqueHits.Add(Hit);
		UE_LOG(LogPrimaryMeleeGA, Verbose,
			TEXT("[MeleeDeterministic] Candidate accepted Id=%d Target=%s Source=%s."),
			StrikeId,
			*GetNameSafe(TargetActor),
			SourceLabel);
		return true;
	};

	if (CapturedTarget)
	{
		const FString SourceString = CapturedTargetSource.IsNone()
			? FString(TEXT("CapturedTarget"))
			: CapturedTargetSource.ToString();

		if (!IsValidDeterministicTarget(InstigatorActor, CapturedTarget))
		{
			MissReason = TEXT("InvalidPreferredTarget");
			if (bServerLogic)
			{
				UE_LOG(LogPrimaryMeleeGA, Display,
					TEXT("[MeleeDeterministic] Locked target skipped Id=%d Target=%s Reason=InvalidTarget Source=%s."),
					StrikeId,
					*GetNameSafe(CapturedTarget),
					*SourceString);
			}
		}
		else
		{
			FHitResult LockedHit;
			if (TryBuildHitFromActor(InstigatorActor, CapturedTarget, LockedTargetGraceRange, LockedHit))
			{
				if (TryRegisterHit(LockedHit, *SourceString) && bServerLogic)
				{
					UE_LOG(LogPrimaryMeleeGA, Display,
						TEXT("[MeleeDeterministic] Locked target registered Id=%d Target=%s Source=%s GraceRange=%.1f."),
						StrikeId,
						*GetNameSafe(CapturedTarget),
						*SourceString,
						LockedTargetGraceRange);
				}
			}
			else
			{
				MissReason = TEXT("OutOfRange");
				const float Dist2D = FVector::Dist2D(InstigatorActor->GetActorLocation(), CapturedTarget->GetActorLocation());
				if (bServerLogic)
				{
					UE_LOG(LogPrimaryMeleeGA, Display,
						TEXT("[MeleeDeterministic] Locked target outside grace range Id=%d Target=%s Dist2D=%.1f GraceRange=%.1f Source=%s."),
						StrikeId,
						*GetNameSafe(CapturedTarget),
						Dist2D,
						LockedTargetGraceRange,
						*SourceString);
				}
			}
		}
	}

	const float ConeRange = AttackRange;
	if (ConeRange > KINDA_SMALL_NUMBER && bHasCleaveAngle)
	{
		TArray<FHitResult> ConeHits;
		GatherConeTraceTargets(
			InstigatorActor,
			ConeRange,
			ConeAngle,
			ConeHits,
			bCachedHitShapeValid ? &CachedHitOrigin : nullptr,
			bCachedHitShapeValid ? &CachedHitForward : nullptr);

		for (const FHitResult& ConeHit : ConeHits)
		{
			TryRegisterHit(ConeHit, TEXT("ConeTrace"));
		}
	}
	else if (UniqueHits.Num() == 0 && ConeRange > KINDA_SMALL_NUMBER)
	{
		FHitResult ForwardHit;
		if (TryFindNearestForwardTarget(InstigatorActor, ConeRange, ForwardHit))
		{
			TryRegisterHit(ForwardHit, TEXT("NearestForward"));
		}
		else if (MissReason == TEXT("NoPreferredTarget"))
		{
			MissReason = TEXT("NoForwardTargetInRange");
		}
	}
	else if (MissReason == TEXT("NoPreferredTarget"))
	{
		MissReason = TEXT("NoPreferredTargetOrCone");
	}

	if (UniqueHits.Num() == 0)
	{
		if (bServerLogic)
		{
			UE_LOG(LogPrimaryMeleeGA, Display,
				TEXT("[MeleeDeterministic] Strike resolved Id=%d Stage=%d HitCount=0 MissReason=%s Target=%s GraceRange=%.1f ConeRange=%.1f ConeAngle=%.1f."),
				StrikeId,
				StrikeStage,
				*MissReason,
				*GetNameSafe(CapturedTarget),
				LockedTargetGraceRange,
				ConeRange,
				ConeAngle);
		}
		return;
	}

	const FGameplayAbilityTargetDataHandle TargetData = MakeUniqueTargetData(UniqueHits);
	if (bServerLogic)
	{
		UE_LOG(LogPrimaryMeleeGA, Display,
			TEXT("[MeleeDeterministic] Strike resolved Id=%d Stage=%d HitCount=%d Target=%s Source=%s."),
			StrikeId,
			StrikeStage,
			TargetData.Num(),
			*GetNameSafe(CapturedTarget),
			*CapturedTargetSource.ToString());
		HandleServerDamage(TargetData);
	}

	if (bPredicting)
	{
		HandlePredictedFeedback(TargetData);
	}
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
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	return IsValidDeterministicTarget(AvatarActor, Target) ? Target : nullptr;
}

void UGA_PrimaryMeleeBasic::HandleServerDamage(const FGameplayAbilityTargetDataHandle& TargetData)
{
	if (!ShouldProcessServerLogic() || TargetData.Num() == 0)
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
			ApplyDefaultDamageRulesToSpec(DamageSpec);

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
				const float RawFinalDamage = FMath::IsFinite(AttackDamageValue) && FMath::IsFinite(DamageScalar)
					? AttackDamageValue * DamageScalar
					: 0.f;
				const float FinalDamage = FMath::IsFinite(RawFinalDamage) ? FMath::Max(0.f, RawFinalDamage) : 0.f;
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: AttackDamage attribute %.2f scalar %.2f final %.2f."),
					AttackDamageValue,
					DamageScalar,
					FinalDamage);

				if (AttackDamageValue <= 0.f)
				{
					UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("HandleServerDamage: AttackDamage is %.2f; outgoing damage will be zero."), AttackDamageValue);
				}

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
		if (!ResolveAilmentMagnitudes(AilmentTag, AilmentAmount, AilmentDuration)
			|| !FMath::IsFinite(AilmentAmount)
			|| !FMath::IsFinite(AilmentDuration)
			|| AilmentAmount <= KINDA_SMALL_NUMBER
			|| AilmentDuration <= KINDA_SMALL_NUMBER)
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

	if (FAeyerjiMeleeDeterministicStrikePolicy::ShouldResolveOnMontageFinish(
		bWasCancelled,
		bDeterministicStrikePending,
		bDeterministicStrikeResolved))
	{
		if (ShouldProcessServerLogic())
		{
			UE_LOG(LogPrimaryMeleeGA, Display,
				TEXT("[MeleeDeterministic] Montage finished before impact; flushing strike Id=%d Stage=%d."),
				ActiveDeterministicStrikeId,
				ActiveDeterministicStrikeComboIndex);
		}
		ResolveDeterministicStrike();
	}
	else if (bWasCancelled)
	{
		CancelDeterministicStrike(TEXT("MontageCancelled"));
	}

	ClearCancelWindowTimer();
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
	return (FMath::IsFinite(Value) && Value > KINDA_SMALL_NUMBER) ? Value : DefaultValue;
}

float UGA_PrimaryMeleeBasic::ResolveAttackAngleDegrees() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		return FAeyerjiMeleeDeterministicStrikePolicy::ResolveAttackAngle(
			/*AttributeAngle=*/0.f,
			/*bHasAttribute=*/false,
			ConeTraceAngleFallback);
	}

	const float Angle = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackAngleAttribute());
	const bool bHasAttackAngleAttribute = ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetAttackAngleAttribute());
	return FAeyerjiMeleeDeterministicStrikePolicy::ResolveAttackAngle(
		Angle,
		bHasAttackAngleAttribute,
		ConeTraceAngleFallback);
}

float UGA_PrimaryMeleeBasic::ResolveAttackRange() const
{
	const float Range = GetNumericAttributeOrDefault(UAeyerjiAttributeSet::GetAttackRangeAttribute(), ConeTraceRangeFallback);
	return FMath::IsFinite(Range) ? FMath::Max(Range, 0.f) : 0.f;
}

void UGA_PrimaryMeleeBasic::GatherConeTraceTargets(AActor* InstigatorActor, float Range, float AngleDegrees, TArray<FHitResult>& OutHits, const FVector* OverrideOrigin, const FVector* OverrideForward) const
{
	OutHits.Reset();

	if (!IsValid(InstigatorActor)
		|| !FMath::IsFinite(Range)
		|| Range <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(AngleDegrees))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector Forward = OverrideForward ? *OverrideForward : InstigatorActor->GetActorForwardVector();
	if (Forward.ContainsNaN())
	{
		return;
	}
	Forward.Z = 0.f;
	Forward = Forward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	const FVector Origin = OverrideOrigin ? *OverrideOrigin : InstigatorActor->GetActorLocation();
	if (Origin.ContainsNaN())
	{
		return;
	}
	const float HalfAngleDegrees = FMath::Clamp(AngleDegrees * 0.5f, 0.f, 180.f);
	const float MaxRangeSq = FMath::Square(Range);
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PrimaryMeleeCone), false, InstigatorActor);
	QueryParams.bTraceComplex = false;
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActor(InstigatorActor);

	FCollisionObjectQueryParams ObjectParams;
	const ECollisionChannel ObjectChannel = ConeTraceChannel.GetValue();
	ObjectParams.AddObjectTypesToQuery(static_cast<int32>(ObjectChannel) < static_cast<int32>(ECC_MAX)
		? ObjectChannel
		: ECC_Pawn);

	TSet<TWeakObjectPtr<AActor>> SeenActors;

	const bool bDrawDebug = ShouldDrawPrimaryMeleeDebug();
	if (bDrawDebug)
	{
		const FColor ConeTraceDebugColor = FColor::Red;
		const FVector DebugEnd = Origin + Forward * Range;
		DrawDebugSphere(World, Origin, 8.f, 12, ConeTraceDebugColor, false, kPrimaryMeleeDebugDuration, 0, 1.f);
		DrawDebugDirectionalArrow(World, Origin, DebugEnd, 30.f, ConeTraceDebugColor, false, kPrimaryMeleeDebugDuration, 0, 1.5f);

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

			DrawDebugLine(World, Origin, EdgePoint, ConeTraceDebugColor, false, kPrimaryMeleeDebugDuration, 0, 0.75f);
			DrawDebugLine(World, PreviousEdge, EdgePoint, ConeTraceDebugColor, false, kPrimaryMeleeDebugDuration, 0, 0.75f);
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
		const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		if (!IsValid(TargetActor)
			|| TargetActor == InstigatorActor
			|| TargetActor->GetWorld() != World
			|| SeenActors.Contains(WeakTarget))
		{
			continue;
		}

		FVector TargetPoint = TargetActor->GetActorLocation();
		UPrimitiveComponent* TargetComponent = Overlap.Component.Get();
		TryResolveTargetCollisionPoint(TargetActor, Origin, TargetPoint, TargetComponent);
		if (TargetPoint.ContainsNaN())
		{
			continue;
		}

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

		SeenActors.Add(WeakTarget);

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

		if (bDrawDebug)
		{
			DrawDebugPoint(World, TargetPoint, 12.f, FColor::Red, false, kPrimaryMeleeDebugDuration);
		}
	}
}

bool UGA_PrimaryMeleeBasic::TryBuildHitFromActor(AActor* InstigatorActor, AActor* TargetActor, float MaxRange, FHitResult& OutHit) const
{
	if (!IsValidDeterministicTarget(InstigatorActor, TargetActor)
		|| !FMath::IsFinite(MaxRange)
		|| MaxRange < 0.f)
	{
		return false;
	}

	const FVector Origin = bCachedHitShapeValid ? CachedHitOrigin : InstigatorActor->GetActorLocation();
	FVector TargetLocation = TargetActor->GetActorLocation();
	if (Origin.ContainsNaN() || TargetLocation.ContainsNaN())
	{
		return false;
	}
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

	if (!IsValid(TargetActor) || QueryOrigin.ContainsNaN())
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
		if (!FMath::IsFinite(DistanceSq) || ClosestPoint.ContainsNaN())
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

bool UGA_PrimaryMeleeBasic::IsValidDeterministicTarget(AActor* InstigatorActor, AActor* TargetActor) const
{
	if (!IsValid(InstigatorActor)
		|| !IsValid(TargetActor)
		|| TargetActor == InstigatorActor
		|| TargetActor->GetWorld() != InstigatorActor->GetWorld()
		|| InstigatorActor->GetActorLocation().ContainsNaN()
		|| TargetActor->GetActorLocation().ContainsNaN())
	{
		return false;
	}

	if (IsDeadForDeterministicStrike(TargetActor))
	{
		return false;
	}

	if (!bAllowFriendlyDamage && AbilityTeamUtils::AreOnSameTeam(InstigatorActor, TargetActor))
	{
		return false;
	}

	return true;
}

bool UGA_PrimaryMeleeBasic::TryFindNearestForwardTarget(AActor* InstigatorActor, float Range, FHitResult& OutHit) const
{
	OutHit = FHitResult();

	if (!IsValid(InstigatorActor)
		|| !FMath::IsFinite(Range)
		|| Range <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	TArray<FHitResult> ForwardHits;
	GatherConeTraceTargets(
		InstigatorActor,
		Range,
		/*AngleDegrees=*/180.f,
		ForwardHits,
		bCachedHitShapeValid ? &CachedHitOrigin : nullptr,
		bCachedHitShapeValid ? &CachedHitForward : nullptr);

	float BestDistanceSq = TNumericLimits<float>::Max();
	bool bFoundTarget = false;
	for (const FHitResult& CandidateHit : ForwardHits)
	{
		AActor* TargetActor = CandidateHit.GetActor();
		if (!IsValidDeterministicTarget(InstigatorActor, TargetActor))
		{
			continue;
		}

		if (DamagedActors.Contains(TWeakObjectPtr<AActor>(TargetActor)))
		{
			continue;
		}

		const FVector Origin = bCachedHitShapeValid ? CachedHitOrigin : InstigatorActor->GetActorLocation();
		const float DistanceSq = FVector::DistSquared2D(Origin, CandidateHit.ImpactPoint);
		if (!bFoundTarget || DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			OutHit = CandidateHit;
			bFoundTarget = true;
		}
	}

	return bFoundTarget;
}

void UGA_PrimaryMeleeBasic::RefaceInstigatorTowardTargetIfNeeded(AActor* InstigatorActor, AActor* TargetActor) const
{
	if (!IsValidDeterministicTarget(InstigatorActor, TargetActor))
	{
		return;
	}

	if (!ShouldProcessServerLogic() && !IsLocallyPredicting())
	{
		return;
	}

	FVector Forward = InstigatorActor->GetActorForwardVector();
	Forward.Z = 0.f;
	if (!Forward.Normalize())
	{
		return;
	}

	FVector TargetPoint = TargetActor->GetActorLocation();
	UPrimitiveComponent* TargetComponent = nullptr;
	TryResolveTargetCollisionPoint(TargetActor, InstigatorActor->GetActorLocation(), TargetPoint, TargetComponent);

	FVector ToTarget = TargetPoint - InstigatorActor->GetActorLocation();
	ToTarget.Z = 0.f;
	if (!ToTarget.Normalize())
	{
		return;
	}

	const float DotToTarget = FVector::DotProduct(Forward, ToTarget);
	if (!FAeyerjiMeleeDeterministicStrikePolicy::ShouldRefaceLockedTarget(DotToTarget, LockedTargetRefacingThresholdDegrees))
	{
		return;
	}

	const FRotator DesiredYaw(0.f, ToTarget.Rotation().Yaw, 0.f);
	InstigatorActor->SetActorRotation(DesiredYaw);
	if (APawn* Pawn = Cast<APawn>(InstigatorActor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(DesiredYaw);
		}
	}
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
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(ActorInfo);

	RemoveActivePhaseTag();
	CurrentPhase = NewPhase;

	// Mirror the current phase to loose tags so external systems (like HUD/AI) can react without polling the ability.
	const FGameplayTag NewPhaseTag = GetPhaseTag(NewPhase);
	if (NewPhaseTag.IsValid())
	{
		if (ASC)
		{
			ASC->AddLooseGameplayTag(NewPhaseTag);
			ActivePhaseTag = NewPhaseTag;
		}
		else
		{
			ActivePhaseTag = FGameplayTag();
		}
	}
	else
	{
		ActivePhaseTag = FGameplayTag();
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("SetAbilityPhase: Previous=%d New=%d"),
		static_cast<int32>(PreviousPhase),
		static_cast<int32>(NewPhase));

	// During forced run cleanup the ASC can be tearing down while CancelAbility/EndAbility still clean local state.
	// Avoid dispatching Blueprint phase events when GAS actor info is no longer valid.
	if (ActorInfo && ASC)
	{
		BP_OnAbilityPhaseChanged(NewPhase, PreviousPhase);
	}
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

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (UAbilitySystemComponent* ASC = GetAeyerjiAbilitySystem(ActorInfo))
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

	const float RateScale = (FMath::IsFinite(CurrentMontagePlayRate) && CurrentMontagePlayRate > KINDA_SMALL_NUMBER)
		? (1.f / CurrentMontagePlayRate)
		: 1.f;
	const float SafeCancelWindowDuration = FMath::IsFinite(CancelWindowDuration)
		? FMath::Max(0.f, CancelWindowDuration)
		: 0.f;
	const float ScaledCancelWindowDuration = SafeCancelWindowDuration * RateScale;

	if (ScaledCancelWindowDuration <= 0.f)
	{
		OnCancelWindowExpired();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// Keep the startup/hit-window timing aligned with the montage rate so fast attacks still trace before the montage ends.
		World->GetTimerManager().SetTimer(CancelWindowTimerHandle, this, &UGA_PrimaryMeleeBasic::OnCancelWindowExpired, ScaledCancelWindowDuration, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BeginCancelWindow: Armed locked startup window for %.3fs (Base=%.3f Rate=%.3f)."),
			ScaledCancelWindowDuration,
			CancelWindowDuration,
			CurrentMontagePlayRate);
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
	bExternalRetargetBlocksCombo = false;
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

	const float SafeResetDelay = FMath::IsFinite(ComboResetDelay) ? FMath::Max(0.f, ComboResetDelay) : 0.f;
	if (SafeResetDelay <= KINDA_SMALL_NUMBER)
	{
		NextComboIndex = 0;
		UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("ScheduleComboReset: Immediate reset (delay <= 0)."));
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ComboResetTimerHandle, this, &UGA_PrimaryMeleeBasic::OnComboResetTimerExpired, SafeResetDelay, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ScheduleComboReset: Armed combo reset in %.3fs (NextComboIndex=%d)."),
			SafeResetDelay,
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
	const float EffectiveBaseline = FMath::IsFinite(BaselineAttackSpeed)
		? FMath::Max(BaselineAttackSpeed, kMinAttackSpeed)
		: 1.f;
	const float EffectiveAttackSpeed = FMath::IsFinite(AttackSpeed)
		? FMath::Max(AttackSpeed, kMinAttackSpeed)
		: EffectiveBaseline;
	return FMath::Max(EffectiveAttackSpeed / EffectiveBaseline, kMinAttackSpeed);
}

float UGA_PrimaryMeleeBasic::ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const
{
	float AttackRate = BaselineAttackSpeed;

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
        if (const UAeyerjiAttributeSet* Attr = ActorInfo->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>())
        {
            const float AttributeAttackSpeed = Attr->GetAttackSpeed();
            if (FMath::IsFinite(AttributeAttackSpeed) && AttributeAttackSpeed > KINDA_SMALL_NUMBER)
            {
                // AttackSpeed is stored as a "rating" where 100 == 1 attack/sec. Convert to real APS.
                AttackRate = AttributeAttackSpeed / 100.f;
            }
            else
            {
                const float AttributeCooldownSeconds = Attr->GetAttackCooldown();
                if (FMath::IsFinite(AttributeCooldownSeconds) && AttributeCooldownSeconds > KINDA_SMALL_NUMBER)
                {
                    AttackRate = 1.f / AttributeCooldownSeconds;
                }
            }
        }
	}

	return FMath::IsFinite(AttackRate) ? FMath::Max(AttackRate, kMinAttackSpeed) : 1.f;
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

	const float FinalAttackSpeed = FMath::Max(AttackSpeed, kMinAttackSpeed);
	CurrentMontagePlayRate = CalculateMontagePlayRate(FinalAttackSpeed);

	ClearCancelWindowTimer();
	ResetDeterministicStrikeState();
	SetAbilityPhase(EPrimaryMeleePhase::WindUp);
	SetMovementLock(false);
	SetCanBeCanceled(true);

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

	if (!ScheduleDeterministicStrike(ClampedIndex, FinalAttackSpeed))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning,
			TEXT("[MeleeDeterministic] StartComboStage failed to schedule deterministic strike for stage=%d."),
			ClampedIndex);
		StopMontageTask();
		CurrentComboIndex = INDEX_NONE;
		ComboStagesExecuted = PreviousStageCount;
		return false;
	}
	BeginCancelWindow();

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
	if (bExternalRetargetBlocksCombo)
	{
		if (bComboInputBuffered)
		{
			UE_LOG(LogPrimaryMeleeGA, Log,
				TEXT("[MouseAttack] Ignoring buffered combo because the controller selected a different target."));
		}
		bComboInputBuffered = false;
		return false;
	}

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
