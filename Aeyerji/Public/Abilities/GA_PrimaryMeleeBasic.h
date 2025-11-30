// GA_PrimaryMeleeBasic.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Engine/EngineTypes.h"
#include "GA_PrimaryMeleeBasic.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UGameplayEffect;
struct FTimerHandle;

UENUM(BlueprintType)
enum class EPrimaryMeleePhase : uint8
{
    None,
    WindUp,
    HitWindow,
    Recovery,
    Cancelled
};

/**
 * Primary melee ability driven by AttackSpeed.
 * - Plays a montage at a rate derived from AttackSpeed / BaselineAttackSpeed
 * - Listens for melee trace windows via Gameplay Events (from AnimNotifyMeleeWindow)
 * - Processes hit results on the server and applies damage/cooldown
 */
UCLASS()
class AEYERJI_API UGA_PrimaryMeleeBasic : public UGA_AeyerjiBase
{
    GENERATED_BODY()

public:
    UGA_PrimaryMeleeBasic();

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                 const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
                              const FGameplayAbilityActorInfo* ActorInfo,
                              const FGameplayAbilityActivationInfo ActivationInfo) override;

    virtual void CancelAbility(const FGameplayAbilitySpecHandle Handle,
                               const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               bool bReplicateCancelAbility) override;

    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
                            const FGameplayAbilityActorInfo* ActorInfo,
                            const FGameplayAbilityActivationInfo ActivationInfo,
                            bool bReplicateEndAbility,
                            bool bWasCancelled) override;

    virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
                               const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo) const override;

    /** Gameplay Event tag emitted by UAnimNotifyMeleeWindow each tick of the active window. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee")
    FGameplayTag MeleeWindowEventTag;

    /** Montage played for the swing. Prefer setting this in BP or via data assets. (Soft reference to avoid hard loads in constructor.) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee")
    TSoftObjectPtr<UAnimMontage> AttackMontage;

    /** Baseline attacks-per-second that corresponds to Montage rate 1.0 (defaults to 1). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee", meta=(ClampMin="0.01"))
    float BaselineAttackSpeed;

    /** Gameplay Effect applied to hit targets. Optional if damage is handled in Blueprint. (Soft reference to avoid circular loads.) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
    TSoftClassPtr<UGameplayEffect> DamageEffectClass;

    /** SetByCaller tag used when building the damage spec (e.g. Data.Damage). Optional. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
    FGameplayTag DamageSetByCallerTag;

    /** Scalar applied to the AttackDamage attribute when populating the damage SetByCaller. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
    float DamageScalar;

    /** Minimum cooldown duration applied even if AttackSpeed is very high. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cooldown", meta=(ClampMin="0.0"))
    float MinCooldownDuration;

    /** Broadcast Event.PrimaryAttack.Completed when the montage ends (server only). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Events")
    bool bSendCompletionGameplayEvent;

    /** Allows melee swings to damage actors that share the same team as the instigator. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
    bool bAllowFriendlyDamage = false;

    /** Server hook: designers can implement damage logic in BP in addition to C++ handling. */
    UFUNCTION(BlueprintImplementableEvent, Category="Melee|Server", DisplayName="HandleMeleeDamage")
    void BP_HandleMeleeDamage(const FGameplayAbilityTargetDataHandle& TargetData);

    /** Local cosmetic hook: fires on autonomous proxies when prediction receives hits. */
    UFUNCTION(BlueprintImplementableEvent, Category="Melee|Cosmetic", DisplayName="HandlePredictedMeleeHit")
    void BP_HandlePredictedMeleeHit(const FGameplayAbilityTargetDataHandle& TargetData);

    /** Design hook for reacting to ability phase transitions. */
    UFUNCTION(BlueprintImplementableEvent, Category="Melee|State", DisplayName="OnAbilityPhaseChanged")
    void BP_OnAbilityPhaseChanged(EPrimaryMeleePhase NewPhase, EPrimaryMeleePhase PreviousPhase);

    /** Duration in seconds after activation during which manual cancel (movement/etc.) is permitted. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|State", meta=(ClampMin="0.0"))
    float CancelWindowDuration = 0.12f;

    /** Enable the cone-based fallback trace when the animation window provides unreliable results. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace")
    bool bUseConeTraceFallback = true;

    /** Default melee reach in centimetres if the AttackRange attribute is unset. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace", meta=(ClampMin="0.0"))
    float ConeTraceRangeFallback = 220.f;

    /** Default cone angle (degrees) if the AttackAngle attribute is unset. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace", meta=(ClampMin="1.0", ClampMax="360.0"))
    float ConeTraceAngleFallback = 75.f;

    /** Collision channel queried by the melee cone (defaults to Pawn to hit characters). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace")
    TEnumAsByte<ECollisionChannel> ConeTraceChannel = ECC_Pawn;

private:
    /** Active montage task for the current swing. */
    UPROPERTY()
    TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

    /** Gameplay event wait task. */
    UPROPERTY()
    TObjectPtr<UAbilityTask_WaitGameplayEvent> WindowEventTask;

    /** Actors already damaged during this activation. */
    TSet<TWeakObjectPtr<AActor>> DamagedActors;

    /** Tracks the current high-level state of the melee swing. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Melee|State", meta=(AllowPrivateAccess="true"))
    EPrimaryMeleePhase CurrentPhase;

    /** Cached gameplay tag mirroring the current phase so other systems can query the ASC. */
    FGameplayTag ActivePhaseTag;

    /** True once the ability has committed costs/cooldown after the hit window opens. */
    bool bHasCommittedAtImpact;

    /** Cached tag applied while manual cancels are disabled and movement must be locked. */
   FGameplayTag MovementLockTag;

    /** True when movement is currently locked for this ability instance. */
    bool bMovementLocked;

    /** Handle for the cancel window grace period timer. */
    FTimerHandle CancelWindowTimerHandle;

    /** Optional combo montages that will be cycled when subsequent inputs are buffered. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(AllowPrivateAccess="true"))
    TArray<TSoftObjectPtr<UAnimMontage>> ComboMontages;

    /** Max number of montages to pull from the avatar interface (first N entries are used). */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(ClampMin="1", UIMax="4", AllowPrivateAccess="true"))
    int32 MaxProviderComboMontages = 2;

    /** Delay before the combo sequence resets to the first montage when no further inputs are buffered. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(ClampMin="0.0", AllowPrivateAccess="true"))
    float ComboResetDelay = 0.65f;

    /** Index of the montage currently playing this activation (INDEX_NONE if idle). */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(AllowPrivateAccess="true"))
    int32 CurrentComboIndex = INDEX_NONE;

    /** Persistent pointer to the montage index that should be used on the next activation. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(AllowPrivateAccess="true"))
    int32 NextComboIndex = 0;

    /** Number of combo stages that have been executed during the current activation. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(AllowPrivateAccess="true"))
    int32 ComboStagesExecuted = 0;

    /** True when the player has buffered an additional combo input during the current stage. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Melee|Combo", meta=(AllowPrivateAccess="true"))
    bool bComboInputBuffered = false;

    /** Timer used to reset NextComboIndex when the player does not continue the chain. */
    FTimerHandle ComboResetTimerHandle;

    /** Runtime list of montages resolved from the avatar/interface for this activation. */
    TArray<TWeakObjectPtr<UAnimMontage>> RuntimeComboMontages;

    /** Optional: cached clicked target captured at activation to guarantee inclusion during the swing. */
    TWeakObjectPtr<AActor> StartupClickedTarget;

    /** Cached swing shape captured when the hit window first opens to avoid back-hits while turning. */
    FVector CachedHitOrigin;
    FVector CachedHitForward;
    bool bCachedHitShapeValid = false;

    /** Enables debug drawing for the cone trace fallback. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace", meta=(AllowPrivateAccess="true"))
    bool bDrawConeTraceDebug = false;

    /** Duration for the debug cone/points when enabled. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace", meta=(AllowPrivateAccess="true", EditCondition="bDrawConeTraceDebug", ClampMin="0.0"))
    float ConeTraceDebugDuration = 0.2f;

    /** Color for cone trace debug drawing. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Melee|ConeTrace", meta=(AllowPrivateAccess="true", EditCondition="bDrawConeTraceDebug"))
    FColor ConeTraceDebugColor = FColor::Red;

    bool bCompletionBroadcasted;

    bool StartMontage(float AttackSpeed, UAnimMontage* MontageToPlay);
    void StartWindowListener();
    void CleanupWindowListener();
    void StopMontageTask();

    void HandleServerDamage(const FGameplayAbilityTargetDataHandle& TargetData);
    void HandlePredictedFeedback(const FGameplayAbilityTargetDataHandle& TargetData);

    void BroadcastPrimaryAttackComplete();

    bool ShouldProcessServerLogic() const;
    bool IsLocallyPredicting() const;

    float ResolveAttackAngleDegrees() const;
    float ResolveAttackRange() const;
    float GetNumericAttributeOrDefault(const FGameplayAttribute& Attribute, float DefaultValue) const;
    void GatherConeTraceTargets(AActor* InstigatorActor, float Range, float AngleDegrees, TArray<FHitResult>& OutHits, const FVector* OverrideOrigin = nullptr, const FVector* OverrideForward = nullptr) const;
    AActor* ResolvePreferredClickedTarget(const FGameplayAbilityActorInfo* ActorInfo, float MaxAgeSeconds = 1.5f) const;
    bool TryBuildHitFromActor(AActor* InstigatorActor, AActor* TargetActor, float MaxRange, FHitResult& OutHit) const;

    FGameplayAbilityTargetDataHandle MakeUniqueTargetData(const TArray<FHitResult>& Hits);

    void BindMontageDelegates(UAbilityTask_PlayMontageAndWait* Task);

    UFUNCTION()
    void OnMeleeWindowGameplayEvent(FGameplayEventData Payload);

    UFUNCTION()
    void OnMontageCompleted();

    UFUNCTION()
    void OnMontageInterrupted();

    UFUNCTION()
    void OnMontageCancelled();

    void HandleMontageFinished(bool bWasCancelled);

    void SetAbilityPhase(EPrimaryMeleePhase NewPhase);
    void ClearAbilityPhase();
    FGameplayTag GetPhaseTag(EPrimaryMeleePhase Phase) const;
    void RemoveActivePhaseTag();
    bool EnsureAbilityCommitted();
    void BeginCancelWindow();
    void OnCancelWindowExpired();
    void ClearCancelWindowTimer();
    void SetMovementLock(bool bEnable);

protected:
    /** Override in Blueprint to select a montage per activation (e.g. via interfaces on the avatar). */
    UFUNCTION(BlueprintNativeEvent, Category="Melee")
    UAnimMontage* SelectAttackMontage(const FGameplayAbilityActorInfo& ActorInfo) const;
    virtual UAnimMontage* SelectAttackMontage_Implementation(const FGameplayAbilityActorInfo& ActorInfo) const;

private:
    void ResetComboRuntimeState();
    bool TryLaunchBufferedCombo();
    bool StartComboStage(int32 ComboIndex, const FGameplayAbilityActorInfo* ActorInfo, float AttackSpeed);
    void ScheduleComboReset();
    void ClearComboResetTimer();
    void OnComboResetTimerExpired();
    int32 GetConfiguredComboCount(const FGameplayAbilityActorInfo* ActorInfo);
    UAnimMontage* ResolveComboMontage(const FGameplayAbilityActorInfo* ActorInfo, int32 ComboIndex);
    UAnimMontage* ResolveSingleFallbackMontage(const FGameplayAbilityActorInfo* ActorInfo) const;
    void RefreshComboMontagesFromAvatar(const FGameplayAbilityActorInfo* ActorInfo);
    int32 CompactRuntimeComboMontages();
    float ResolveAttackSpeed(const FGameplayAbilityActorInfo* ActorInfo) const;
};
