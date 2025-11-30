#include "Abilities/GA_PrimaryMeleeBasic.h"

#include "AbilitySystemComponent.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystemGlobals.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Combat/PrimaryMeleeComboProviderInterface.h"
#include "GameplayEffect.h"
#include "MouseNavBlueprintLibrary.h"
#include "DrawDebugHelpers.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Logging/LogMacros.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
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
		ActivationBlockedTags.AddTag(AeyerjiTags::Cooldown_PrimaryAttack);

		MeleeWindowEventTag = AeyerjiTags::Event_Combat_Melee_TraceWindow;
	}

	if (!DamageSetByCallerTag.IsValid())
	{
		static const FName DamageTagName(TEXT("Data.Damage"));
		DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(DamageTagName, /*ErrorIfNotFound=*/false);
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
	const FString HandleStr      = Handle.ToString();
	const FString TriggerTagStr  = TriggerEventData ? TriggerEventData->EventTag.ToString() : FString(TEXT("None"));
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility -> Handle=%s Avatar=%s ASC=%s TriggerTag=%s PredictionKey=%d"),
		*HandleStr,
		*GetNameSafe(AvatarActor),
		ASC ? *GetNameSafe(ASC) : TEXT("None"),
		*TriggerTagStr,
		ActivationInfo.GetActivationPredictionKey().Current);

	if (!CheckCost(Handle, ActorInfo))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ActivateAbility: CheckCost failed. Ending ability before execution."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	if (!CheckCooldown(Handle, ActorInfo))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ActivateAbility: CheckCooldown failed. Ending ability before execution."));
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
	SetMovementLock(false);
	SetCanBeCanceled(true);
	RefreshComboMontagesFromAvatar(ActorInfo);

	const float AttackSpeed = ResolveAttackSpeed(ActorInfo);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Final AttackSpeed=%.3f (Baseline=%.3f) DamagedActors cleared."),
		AttackSpeed,
		BaselineAttackSpeed);

	StartupClickedTarget = ResolvePreferredClickedTarget(ActorInfo, /*MaxAgeSeconds=*/2.5f);

	const int32 ComboCount = GetConfiguredComboCount(ActorInfo);
	if (ComboCount <= 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ActivateAbility: No valid combo montages configured. Ending ability."));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	if (NextComboIndex < 0 || NextComboIndex >= ComboCount)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: NextComboIndex %d out of range, clamping to zero."), NextComboIndex);
		NextComboIndex = 0;
	}

	const int32 StageIndexToPlay = NextComboIndex;
	StartWindowListener();

	if (!StartComboStage(StageIndexToPlay, ActorInfo, AttackSpeed))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("ActivateAbility: StartComboStage failed. Ability will end."));
		if (ShouldProcessServerLogic())
		{
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("ActivateAbility: Server logic active, broadcasting completion before ending."));
			BroadcastPrimaryAttackComplete();
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
	}
	else
	{
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
	CleanupWindowListener();
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
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("EndAbility -> Handle=%s Cancelled=%s MontageTask=%s WindowEventTask=%s DamagedActors=%d"),
		*HandleStr,
		bWasCancelled ? TEXT("true") : TEXT("false"),
		MontageTask ? *GetNameSafe(MontageTask) : TEXT("None"),
		WindowEventTask ? *GetNameSafe(WindowEventTask) : TEXT("None"),
		DamagedActors.Num());

	StopMontageTask();
	CleanupWindowListener();

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

bool UGA_PrimaryMeleeBasic::StartMontage(float AttackSpeed, UAnimMontage* MontageToPlay)
{
	UAnimMontage* Montage = MontageToPlay;
	if (!Montage)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartMontage: Montage pointer null (AttackMontage asset missing)."));
		return false;
	}

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	UAnimInstance* AnimInst = Info ? Info->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartMontage: No AnimInstance on avatar %s."),
			*GetNameSafe(GetAvatarActorFromActorInfo()));
		return false;
	}

	if (Montage->GetPlayLength() <= 0.f)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartMontage: Montage %s has zero length, refusing to play."),
			*GetNameSafe(Montage));
		return false;
	}

	const float Rate = FMath::Max(AttackSpeed / FMath::Max(BaselineAttackSpeed, kMinAttackSpeed), kMinAttackSpeed);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartMontage: Montage=%s AttackSpeed=%.3f Baseline=%.3f PlayRate=%.3f"),
		*GetNameSafe(Montage),
		AttackSpeed,
		BaselineAttackSpeed,
		Rate);

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, Rate);
	if (!MontageTask)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartMontage: Failed to create montage task."));
		return false;
	}

	BindMontageDelegates(MontageTask);
	MontageTask->ReadyForActivation();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartMontage: MontageTask %s ready for activation."), *GetNameSafe(MontageTask));
	return true;
}

void UGA_PrimaryMeleeBasic::StartWindowListener()
{
	// Listen for the trace window notify so the ability can react to animation-driven hit events.
	if (!MeleeWindowEventTag.IsValid())
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartWindowListener: MeleeWindowEventTag invalid."));
		return;
	}
	const FString WindowTagStr = MeleeWindowEventTag.ToString();

	WindowEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, MeleeWindowEventTag, nullptr, false, false);
	if (!WindowEventTask)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartWindowListener: Failed to create WaitGameplayEvent task for %s."),
			*WindowTagStr);
		return;
	}

	WindowEventTask->EventReceived.AddDynamic(this, &UGA_PrimaryMeleeBasic::OnMeleeWindowGameplayEvent);
	WindowEventTask->ReadyForActivation();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartWindowListener: Listening for tag %s with task %s."),
		*WindowTagStr,
		*GetNameSafe(WindowEventTask));
}

void UGA_PrimaryMeleeBasic::CleanupWindowListener()
{
	if (!WindowEventTask)
	{
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("CleanupWindowListener: Ending WindowEventTask %s."),
		*GetNameSafe(WindowEventTask));
	WindowEventTask->EndTask();
	WindowEventTask = nullptr;
}

void UGA_PrimaryMeleeBasic::StopMontageTask()
{
	if (!MontageTask)
	{
		return;
	}

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StopMontageTask: Ending MontageTask %s."),
		*GetNameSafe(MontageTask));
	MontageTask->EndTask();
	MontageTask = nullptr;
}

void UGA_PrimaryMeleeBasic::OnMeleeWindowGameplayEvent(FGameplayEventData Payload)
{
	// Animation notifies drive this event to time the strike. We enforce the hit window rules here and then
	// gather potential targets via both the animation payload and a cone trace fallback.
	if (!IsActive())
	{
		const FString PayloadTagStr = Payload.EventTag.ToString();
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Ability inactive, ignoring tag %s."),
			*PayloadTagStr);
		return;
	}

	if (CurrentPhase == EPrimaryMeleePhase::Cancelled || CurrentPhase == EPrimaryMeleePhase::Recovery)
	{
		const FString PayloadTagStr = Payload.EventTag.ToString();
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Phase=%d, ignoring tag %s."),
			static_cast<int32>(CurrentPhase),
			*PayloadTagStr);
		return;
	}

	if (CurrentPhase == EPrimaryMeleePhase::WindUp)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Entering HitWindow phase."));
		SetAbilityPhase(EPrimaryMeleePhase::HitWindow);
	}

	SetMovementLock(true);
	SetCanBeCanceled(false);
	ClearCancelWindowTimer();

	if (!EnsureAbilityCommitted())
	{
		const FString PayloadTagStr = Payload.EventTag.ToString();
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("OnMeleeWindowGameplayEvent: Failed to commit ability for tag %s. Ending ability."),
			*PayloadTagStr);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	AActor* InstigatorActor = GetAvatarActorFromActorInfo();
	if (!InstigatorActor)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("OnMeleeWindowGameplayEvent: Avatar actor invalid, aborting hit processing."));
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
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("OnMeleeWindowGameplayEvent: Ignoring same-team actor %s (Source=%s)."),
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		if (DamagedActors.Contains(WeakTarget))
		{
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Skipping already damaged actor %s (Source=%s)."),
				*GetNameSafe(TargetActor),
				SourceLabel);
			return false;
		}

		DamagedActors.Add(WeakTarget);
		UniqueHits.Add(Hit);

		const FString ImpactNormalStr = Hit.ImpactNormal.ToString();
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Added hit on %s (Source=%s Normal=%s)."),
			*GetNameSafe(TargetActor),
			SourceLabel,
			*ImpactNormalStr);
		return true;
	};

	const FString PayloadTagStr = Payload.EventTag.ToString();
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Received tag %s with %d target data entries."),
		*PayloadTagStr,
		Payload.TargetData.Num());

	const int32 DataCount = Payload.TargetData.Num();
	for (int32 Index = 0; Index < DataCount; ++Index)
	{
		const FGameplayAbilityTargetData* Data = Payload.TargetData.Get(Index);
		if (!Data)
		{
			continue;
		}

		const FHitResult* Hit = Data->GetHitResult();
		if (!Hit)
		{
			continue;
		}

		TryRegisterHit(*Hit, TEXT("AnimWindow"));
	}

	// If the player clicked a pawn, prioritize it aggressively (activation capture + freshest cache).
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
				// Once we successfully add the preferred target, we do not need further preferred attempts.
				StartupClickedTarget = nullptr;
			}
		}
		else
		{
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("OnMeleeWindowGameplayEvent: Preferred target %s outside range %.1f (Source=%s)."),
				*GetNameSafe(PreferredTarget),
				PreferredRange,
				Source);
		}
	};

	// Check the freshest cache first, then fall back to what we captured at activation.
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
		CachedHitForward.Z = 0.f;
		if (!CachedHitForward.Normalize())
		{
			CachedHitForward = InstigatorActor->GetActorForwardVector().GetSafeNormal();
		}
		bCachedHitShapeValid = !CachedHitForward.IsNearlyZero();
	}

	if (bUseConeTraceFallback)
	{
		const float ConeRange = AttackRange;
		const float ConeAngle = ResolveAttackAngleDegrees();

		if (ConeRange > KINDA_SMALL_NUMBER)
		{
			TArray<FHitResult> ConeHits;
			GatherConeTraceTargets(
				InstigatorActor,
				ConeRange,
				ConeAngle,
				ConeHits,
				bCachedHitShapeValid ? &CachedHitOrigin : nullptr,
				bCachedHitShapeValid ? &CachedHitForward : nullptr);
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Cone trace produced %d candidates (Range=%.1f Angle=%.1f)."),
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
			UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("OnMeleeWindowGameplayEvent: Cone trace skipped (Range=%.1f)."), ConeRange);
		}
	}

	if (UniqueHits.Num() == 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: No unique hits after filtering (PayloadEntries=%d)."),
			DataCount);
		return;
	}

	const FGameplayAbilityTargetDataHandle TargetData = MakeUniqueTargetData(UniqueHits);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Generated target data with %d entries (ServerLogic=%s LocalPredict=%s)."),
		TargetData.Num(),
		ShouldProcessServerLogic() ? TEXT("true") : TEXT("false"),
		IsLocallyPredicting() ? TEXT("true") : TEXT("false"));

	if (ShouldProcessServerLogic())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Invoking HandleServerDamage."));
		HandleServerDamage(TargetData);
	}

	if (IsLocallyPredicting())
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnMeleeWindowGameplayEvent: Invoking HandlePredictedFeedback."));
		HandlePredictedFeedback(TargetData);
	}
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

	if (DamageGEClass)
	{
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Using GE class %s at ability level %d."),
			*GetNameSafe(DamageGEClass.GetDefaultObject()),
			GetAbilityLevel());
		FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(DamageGEClass, GetAbilityLevel());
		if (DamageSpec.IsValid())
		{
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

				const float FinalDamage = -AttackDamageValue * DamageScalar;

				DamageSpec.Data->SetSetByCallerMagnitude(DamageSetByCallerTag, FinalDamage);
				UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: SetByCaller %s = %.2f (neg value = damage)."),
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
			UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: Applying damage spec to %d targets."), TargetData.Num());
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

	BP_HandleMeleeDamage(TargetData);
	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("HandleServerDamage: BP_HandleMeleeDamage dispatched."));
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
	return ActorInfo && ActorInfo->IsLocallyControlled() && !ActorInfo->IsNetAuthority();
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
	const float Angle = GetNumericAttributeOrDefault(UAeyerjiAttributeSet::GetAttackAngleAttribute(), ConeTraceAngleFallback);
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

	// Flatten to ground plane so a downhill swing still fans horizontally.
	FVector Forward = OverrideForward ? *OverrideForward : InstigatorActor->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward = Forward.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return;
	}

	const FVector Origin = OverrideOrigin ? *OverrideOrigin : InstigatorActor->GetActorLocation();
	const float HalfAngleRadians = FMath::DegreesToRadians(FMath::Clamp(AngleDegrees * 0.5f, 0.f, 180.f));

	// Sweep a shallow arc from left to right to approximate a half-moon slash.
	const int32 ArcSegments = FMath::Clamp(FMath::CeilToInt(FMath::Max(AngleDegrees, 40.f) / 15.f), 4, 16);
	const float StepRadians = (ArcSegments > 1) ? (HalfAngleRadians * 2.f / (ArcSegments - 1)) : 0.f;
	const float InnerRadius = Range * 0.15f; // start the sweep slightly ahead of the feet
	const float SweepRadius = FMath::Max(55.f, Range * 0.18f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PrimaryMeleeCone), false, InstigatorActor);
	QueryParams.bTraceComplex = false;
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.AddIgnoredActor(InstigatorActor);

	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(SweepRadius);
	const FCollisionObjectQueryParams ObjectParams = FCollisionObjectQueryParams::AllObjects;
	TSet<TWeakObjectPtr<AActor>> SeenActors;

	for (int32 SegmentIdx = 0; SegmentIdx < ArcSegments; ++SegmentIdx)
	{
		const float AngleOffset = -HalfAngleRadians + (SegmentIdx * StepRadians);
		const FVector SampleDir = Forward.RotateAngleAxis(FMath::RadiansToDegrees(AngleOffset), FVector::UpVector).GetSafeNormal();
		if (SampleDir.IsNearlyZero())
		{
			continue;
		}

		const FVector Start = Origin + SampleDir * InnerRadius;
		const FVector End   = Origin + SampleDir * Range;

		TArray<FHitResult> SweepHits;
		bool bSweepHit = false;
		if (ConeTraceChannel == ECC_OverlapAll_Deprecated)
		{
			bSweepHit = World->SweepMultiByObjectType(
				SweepHits,
				Start,
				End,
				FQuat::Identity,
				ObjectParams,
				SweepShape,
				QueryParams);
		}
		else
		{
			const ECollisionChannel TraceChannel = static_cast<ECollisionChannel>(ConeTraceChannel.GetValue());
			bSweepHit = World->SweepMultiByChannel(
				SweepHits,
				Start,
				End,
				FQuat::Identity,
				TraceChannel,
				SweepShape,
				QueryParams);
		}

		if (bDrawConeTraceDebug)
		{
			DrawDebugLine(World, Start, End, ConeTraceDebugColor, false, ConeTraceDebugDuration, 0, 1.f);
		}

		if (!bSweepHit)
		{
			continue;
		}

		for (const FHitResult& Hit : SweepHits)
		{
			AActor* TargetActor = Hit.GetActor();
			if (!TargetActor || TargetActor == InstigatorActor)
			{
				continue;
			}

			if (SeenActors.Contains(TargetActor))
			{
				continue;
			}

			SeenActors.Add(TargetActor);
			OutHits.Add(Hit);

			if (bDrawConeTraceDebug)
			{
				DrawDebugPoint(World, Hit.ImpactPoint, 12.f, ConeTraceDebugColor, false, ConeTraceDebugDuration);
			}
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
	const FVector TargetLocation = TargetActor->GetActorLocation();
	const float DistanceSq = FVector::DistSquared(Origin, TargetLocation);
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

	UPrimitiveComponent* TargetComponent = TargetActor->FindComponentByClass<UPrimitiveComponent>();
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
		ASC->RemoveLooseGameplayTag(ActivePhaseTag);
	}

	ActivePhaseTag = FGameplayTag();
}

bool UGA_PrimaryMeleeBasic::EnsureAbilityCommitted()
{
	if (bHasCommittedAtImpact)
	{
		return true;
	}

	if (!ShouldProcessServerLogic() && !IsLocallyPredicting())
	{
		bHasCommittedAtImpact = true;
		return true;
	}

	if (!CurrentSpecHandle.IsValid() || !CurrentActorInfo)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("EnsureAbilityCommitted: Missing spec handle or actor info."));
		return false;
	}

	if (CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo))
	{
		bHasCommittedAtImpact = true;
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("EnsureAbilityCommitted: Commit succeeded."));
		return true;
	}

	UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("EnsureAbilityCommitted: CommitAbility failed."));
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
		// The cancel window lets the player bail out early during wind-up; once it expires we treat the attack as locked-in.
		World->GetTimerManager().SetTimer(CancelWindowTimerHandle, this, &UGA_PrimaryMeleeBasic::OnCancelWindowExpired, CancelWindowDuration, false);
		UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("BeginCancelWindow: Armed cancel window for %.3fs."), CancelWindowDuration);
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

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("OnCancelWindowExpired: Cancel window elapsed, locking movement."));
	SetCanBeCanceled(false);
	SetMovementLock(true);
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
			if (bMovementLocked || ASC->HasMatchingGameplayTag(MovementLockTag))
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

float UGA_PrimaryMeleeBasic::ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const
{
	float AttackRate = BaselineAttackSpeed;

	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		if (const UAeyerjiAttributeSet* Attr = ActorInfo->AbilitySystemComponent->GetSet<UAeyerjiAttributeSet>())
		{
			const float AttributeCooldownSeconds = Attr->GetAttackCooldown();
			if (AttributeCooldownSeconds > KINDA_SMALL_NUMBER)
			{
				AttackRate = 1.f / AttributeCooldownSeconds;
			}
			else
			{
				const float AttributeAttackSpeed = Attr->GetAttackSpeed();
				if (AttributeAttackSpeed > KINDA_SMALL_NUMBER)
				{
					// AttackSpeed is stored as a "rating" where 100 == 1 attack/sec. Convert to real APS.
					AttackRate = AttributeAttackSpeed / 100.f;
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
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartComboStage: ActorInfo invalid."));
		return false;
	}

	const int32 ComboCount = GetConfiguredComboCount(ActorInfo);
	if (ComboCount <= 0)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartComboStage: No valid combo montages available."));
		return false;
	}

	const int32 ClampedIndex = FMath::Clamp(ComboIndex, 0, ComboCount - 1);
	UAnimMontage* MontageToPlay = ResolveComboMontage(ActorInfo, ClampedIndex);
	if (!MontageToPlay)
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartComboStage: Failed to resolve montage for combo index %d."), ClampedIndex);
		return false;
	}

	ClearComboResetTimer();

	const int32 PreviousStageCount = ComboStagesExecuted;
	CurrentComboIndex = ClampedIndex;

	DamagedActors.Reset();
	bComboInputBuffered = false;

	ClearCancelWindowTimer();
	SetAbilityPhase(EPrimaryMeleePhase::WindUp);
	SetMovementLock(false);
	SetCanBeCanceled(true);
	BeginCancelWindow();

	const float FinalAttackSpeed = FMath::Max(AttackSpeed, kMinAttackSpeed);

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartComboStage: Stage=%d/%d AttackSpeed=%.3f Montage=%s"),
		ClampedIndex,
		ComboCount,
		FinalAttackSpeed,
		*GetNameSafe(MontageToPlay));

	if (!StartMontage(FinalAttackSpeed, MontageToPlay))
	{
		UE_LOG(LogPrimaryMeleeGA, Warning, TEXT("StartComboStage: StartMontage failed for combo index %d."), ClampedIndex);
		CurrentComboIndex = INDEX_NONE;
		ComboStagesExecuted = PreviousStageCount;
		return false;
	}

	ComboStagesExecuted = PreviousStageCount + 1;
	NextComboIndex = (ClampedIndex + 1) % ComboCount;

	UE_LOG(LogPrimaryMeleeGA, Verbose, TEXT("StartComboStage: Success (ComboStagesExecuted=%d NextComboIndex=%d)."),
		ComboStagesExecuted,
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
	UAnimMontage* Result = AttackMontage.IsValid() ? AttackMontage.Get() : nullptr;
	UE_LOG(LogPrimaryMeleeGA, VeryVerbose, TEXT("SelectAttackMontage: Actor=%s Result=%s"),
		*GetNameSafe(ActorInfo.AvatarActor.Get()),
		*GetNameSafe(Result));
	return Result;
}
