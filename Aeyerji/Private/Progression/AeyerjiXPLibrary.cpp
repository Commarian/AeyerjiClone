// File: Source/Aeyerji/Private/Progression/AeyerjiXPLibrary.cpp

#include "Progression/AeyerjiXPLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiRewardAttributeSet.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Progression/AeyerjiRewardTuning.h"
#include "Progression/AeyerjiRewardConfigComponent.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Enemy/EnemyParentNative.h"
#include "GenericTeamAgentInterface.h"
#include "EngineUtils.h"

namespace
{
	constexpr float MaxRewardXP = 1000000000000.f;
	constexpr float MaxRewardScalar = 1000000.f;
	constexpr float MaxRewardRadius = 10000000.f;
	constexpr int32 MaxRewardPlayers = 128;

	float SafeRewardValue(const float Value, const float DefaultValue, const float MinValue, const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}

	bool IsFiniteRewardVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

    static UAbilitySystemComponent* GetASCFromActor(const AActor* Actor)
    {
		if (!IsValid(Actor)) return nullptr;

        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
        {
            return ASI->GetAbilitySystemComponent();
        }

        if (const APawn* Pawn = Cast<APawn>(Actor))
        {
            if (const IAbilitySystemInterface* PawnASI = Cast<IAbilitySystemInterface>(Pawn))
            {
                return PawnASI->GetAbilitySystemComponent();
            }
            if (const APlayerState* PS = Pawn->GetPlayerState())
            {
                if (const IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
                {
                    return PSASI->GetAbilitySystemComponent();
                }
            }
        }
        return nullptr;
    }

    static const UAeyerjiRewardTuning* GetRewardTuningFromActor(const AActor* Actor)
    {
        if (!Actor) return nullptr;
        if (const UAeyerjiRewardConfigComponent* Comp = Actor->FindComponentByClass<UAeyerjiRewardConfigComponent>())
        {
            return Comp->RewardTuning;
        }
        return nullptr;
    }

    static int32 GetActorLevel_Safe(const AActor* Actor)
    {
        if (!Actor) return 1;
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
        {
            if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
            {
                if (const UAeyerjiAttributeSet* Set = ASC->GetSet<UAeyerjiAttributeSet>())
                {
					return UAeyerjiDifficultySettings::FloatToGameplayLevel(Set->GetLevel());
                }
            }
        }
        if (const APawn* Pawn = Cast<APawn>(Actor))
        {
            if (const APlayerState* PS = Pawn->GetPlayerState())
            {
                if (const IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
                {
                    if (UAbilitySystemComponent* ASC = PSASI->GetAbilitySystemComponent())
                    {
                        if (const UAeyerjiAttributeSet* Set = ASC->GetSet<UAeyerjiAttributeSet>())
                        {
							return UAeyerjiDifficultySettings::FloatToGameplayLevel(Set->GetLevel());
                        }
                    }
                }
            }
        }
        return 1;
    }
}

int32 UAeyerjiXPLibrary::GetHighestPlayerLevel(const UObject* WorldContextObject)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World) return 1;

    AGameStateBase* GS = World->GetGameState();
    if (!GS) return 1;

    int32 Highest = 1;
    for (APlayerState* PS : GS->PlayerArray)
    {
        if (!PS) continue;
        APawn* Pawn = PS->GetPawn();
        if (!Pawn) continue;

        UAbilitySystemComponent* ASC = nullptr;
        if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
        {
            ASC = ASI->GetAbilitySystemComponent();
        }
        if (!ASC) continue;

        const UAeyerjiAttributeSet* Set = ASC->GetSet<UAeyerjiAttributeSet>();
        if (!Set) continue;

		const int32 Lvl = UAeyerjiDifficultySettings::FloatToGameplayLevel(Set->GetLevel());
        if (Lvl > Highest)
        {
            Highest = Lvl;
        }
    }
    return UAeyerjiDifficultySettings::ClampGameplayLevel(Highest);
}

float UAeyerjiXPLibrary::GetBaseXPFromActor(const AActor* Actor)
{
    const UAbilitySystemComponent* ASC = GetASCFromActor(Actor);
    if (!ASC) return 0.f;

    const UAeyerjiRewardAttributeSet* Reward = ASC->GetSet<UAeyerjiRewardAttributeSet>();
    if (!Reward) return 0.f;

    // Use ASC to read the numeric in case GameplayEffects are modifying it.
	return SafeRewardValue(
		ASC->GetNumericAttribute(UAeyerjiRewardAttributeSet::GetXPRewardBaseAttribute()),
		0.f,
		0.f,
		MaxRewardXP);
}

float UAeyerjiXPLibrary::ComputeScaledXPReward(const UObject* WorldContextObject,
                                               float BaseXP,
                                               float PerLevelScalar)
{
    const int32 Highest = GetHighestPlayerLevel(WorldContextObject);
    const int32 Delta   = FMath::Max(0, Highest - 1);
	const double Factor = 1.0 + static_cast<double>(Delta) * SafeRewardValue(
		PerLevelScalar, 0.f, 0.f, MaxRewardScalar);
	const double Result = static_cast<double>(SafeRewardValue(BaseXP, 0.f, 0.f, MaxRewardXP)) * Factor;
	return static_cast<float>(FMath::Clamp(Result, 0.0, static_cast<double>(MaxRewardXP)));
}

float UAeyerjiXPLibrary::GetScaledXPRewardForEnemy(const UObject* WorldContextObject,
                                                   const AActor* EnemyActor,
                                                   float PerLevelScalar,
                                                   float DifficultyScale,
                                                   float DifficultyMinMultiplier,
                                                   float DifficultyMaxMultiplier)
{
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || !IsValid(EnemyActor) || EnemyActor->GetWorld() != World)
	{
		return 0.f;
	}

    const float Base = GetBaseXPFromActor(EnemyActor);
    const int32 EnemyLevel = GetActorLevel_Safe(EnemyActor);
    const int32 Delta = FMath::Max(0, EnemyLevel - 1);
	const double LevelFactor = 1.0 + static_cast<double>(Delta) * SafeRewardValue(
		PerLevelScalar, 0.f, 0.f, MaxRewardScalar);

	const float ClampedDiff = SafeRewardValue(DifficultyScale, 0.f, 0.f, 1.f);
	const float DiffMin = SafeRewardValue(DifficultyMinMultiplier, 1.f, 0.f, MaxRewardScalar);
	const float DiffMax = SafeRewardValue(DifficultyMaxMultiplier, 1.f, 0.f, MaxRewardScalar);
	const double DifficultyFactor = DiffMax > 0.f ? FMath::Lerp(DiffMin, DiffMax, ClampedDiff) : 1.0;

	return static_cast<float>(FMath::Clamp(
		static_cast<double>(Base) * LevelFactor * DifficultyFactor,
		0.0,
		static_cast<double>(MaxRewardXP)));
}

void UAeyerjiXPLibrary::SetBaseXPOnActor(AActor* Actor, float BaseXP)
{
	if (!IsValid(Actor) || !Actor->HasAuthority()) return;
    if (UAbilitySystemComponent* ASC = GetASCFromActor(Actor))
    {
        // Ensure the Reward set exists at runtime (in case actor didn't add it yet)
        if (!ASC->GetSet<UAeyerjiRewardAttributeSet>())
        {
            // Create attribute set with the actor as Outer to satisfy GetOwningActor() expectations.
            AActor* OwningActor = nullptr;
            if (const UActorComponent* AsComp = Cast<UActorComponent>(ASC))
            {
                OwningActor = AsComp->GetOwner();
            }
            UObject* SetOuter = OwningActor ? static_cast<UObject*>(OwningActor) : static_cast<UObject*>(ASC);
            const FName RewardSetName = MakeUniqueObjectName(SetOuter, UAeyerjiRewardAttributeSet::StaticClass(), TEXT("AeyerjiRewardAttributeSet"));
            UAeyerjiRewardAttributeSet* NewSet = NewObject<UAeyerjiRewardAttributeSet>(SetOuter, UAeyerjiRewardAttributeSet::StaticClass(), RewardSetName);
            if (NewSet)
            {
                ASC->AddAttributeSetSubobject(NewSet);
            }
        }
		ASC->SetNumericAttributeBase(
			UAeyerjiRewardAttributeSet::GetXPRewardBaseAttribute(),
			SafeRewardValue(BaseXP, 0.f, 0.f, MaxRewardXP));
    }
}

// Resolve value from a tuning, walking up the Parent chain until a value is found or root.
static bool Resolve_BaseRewardXP(const UAeyerjiRewardTuning* Tuning, float& OutBaseXP)
{
    const UAeyerjiRewardTuning* Cur = Tuning;
    int32 Guard = 64; // prevent accidental cycles
    while (Cur && Guard-- > 0)
    {
        if (Cur->bOverride_BaseRewardXP)
        {
			OutBaseXP = SafeRewardValue(Cur->BaseRewardXP, 0.f, 0.f, MaxRewardXP);
            return true;
        }
        Cur = Cur->Parent;
    }
    return false;
}

static bool Resolve_PerLevelScalar(const UAeyerjiRewardTuning* Tuning, float& OutScalar)
{
    const UAeyerjiRewardTuning* Cur = Tuning; int32 Guard = 64;
    while (Cur && Guard-- > 0)
    {
        if (Cur->bOverride_PerLevelScalar)
        {
			OutScalar = SafeRewardValue(Cur->PerLevelScalar, 0.f, 0.f, MaxRewardScalar);
            return true;
        }
        Cur = Cur->Parent;
    }
    return false;
}

static bool Resolve_DifficultyMultiplierRange(const UAeyerjiRewardTuning* Tuning, float& OutMin, float& OutMax)
{
    const UAeyerjiRewardTuning* Cur = Tuning; int32 Guard = 64;
    while (Cur && Guard-- > 0)
    {
        if (Cur->bOverride_DifficultyMultiplierRange)
        {
			OutMin = SafeRewardValue(Cur->DifficultyMinMultiplier, 1.f, 0.f, MaxRewardScalar);
			OutMax = SafeRewardValue(Cur->DifficultyMaxMultiplier, 1.f, 0.f, MaxRewardScalar);
            return true;
        }
        Cur = Cur->Parent;
    }
    return false;
}

static bool Resolve_KillerBonusPercent(const UAeyerjiRewardTuning* Tuning, float& OutPercent)
{
    const UAeyerjiRewardTuning* Cur = Tuning; int32 Guard = 64;
    while (Cur && Guard-- > 0)
    {
        if (Cur->bOverride_KillerBonusPercent)
        {
			OutPercent = SafeRewardValue(Cur->KillerBonusPercent, 0.f, 0.f, 100.f);
            return true;
        }
        Cur = Cur->Parent;
    }
    return false;
}

void UAeyerjiXPLibrary::ApplyRewardTuningToActor(AActor* Actor, const UAeyerjiRewardTuning* RewardTuning)
{
	if (!IsValid(Actor) || !Actor->HasAuthority() || !RewardTuning) return;
    float BaseXP = 0.f;
    if (Resolve_BaseRewardXP(RewardTuning, BaseXP))
    {
        SetBaseXPOnActor(Actor, BaseXP);
    }
}

void UAeyerjiXPLibrary::AwardXPOnEnemyDeath(const UObject* WorldContextObject,
                                            const AActor* EnemyActor,
                                            AActor* Killer)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !IsValid(EnemyActor) || EnemyActor->GetWorld() != World || !EnemyActor->HasAuthority()) return;

    // Server authority only
    if (World->GetNetMode() == NM_Client)
    {
        return;
    }
	if (!IsValid(Killer) || Killer->GetWorld() != World)
	{
		Killer = nullptr;
	}

    // If a RewardTuning is bound to the enemy, prefer its scalars when set
    float EffectiveScalar = 0.5f;
    float EffectiveBonus  = 1.0f;
    float EffectiveDiffMin = 1.0f;
    float EffectiveDiffMax = 1.0f;
    if (const UAeyerjiRewardTuning* Tuning = GetRewardTuningFromActor(EnemyActor))
    {
        float V;
        if (Resolve_PerLevelScalar(Tuning, V))       EffectiveScalar = V;
        if (Resolve_KillerBonusPercent(Tuning, V))   EffectiveBonus  = V;
        float MinV, MaxV;
        if (Resolve_DifficultyMultiplierRange(Tuning, MinV, MaxV))
        {
            EffectiveDiffMin = MinV;
            EffectiveDiffMax = MaxV;
        }
    }
    else
    {
        ensureMsgf(false, TEXT("AwardXPOnEnemyDeath: %s has no RewardTuning (AeyerjiRewardConfigComponent missing?). Aborting award."), *GetNameSafe(EnemyActor));
        return;
    }

    float DifficultyScale = 0.f;
    if (const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyActor))
    {
        DifficultyScale = Enemy->GetScaledDifficulty();
    }

    const float ScaledXP = GetScaledXPRewardForEnemy(WorldContextObject, EnemyActor, EffectiveScalar, DifficultyScale, EffectiveDiffMin, EffectiveDiffMax);
    if (ScaledXP <= 0.f)
    {
        return;
    }

	const float BonusFrac = SafeRewardValue(EffectiveBonus, 0.f, 0.f, 100.f) * 0.01f; // 1.0 -> 0.01

    // Try to resolve killer's PlayerState for comparison
    const APlayerState* KillerPS = nullptr;
    if (const APawn* KillerPawn = Cast<APawn>(Killer))
    {
        KillerPS = KillerPawn->GetPlayerState();
    }
    else if (const AController* KillerPC = Cast<AController>(Killer))
    {
        KillerPS = KillerPC->PlayerState;
    }

    const bool bHasKillerPS = (KillerPS != nullptr);

    AGameStateBase* GS = World->GetGameState();
    if (!GS) return;

	int32 ProcessedPlayers = 0;
	for (APlayerState* PS : GS->PlayerArray)
    {
		if (ProcessedPlayers >= MaxRewardPlayers)
		{
			break;
		}
		if (!IsValid(PS) || PS->GetWorld() != World) continue;
		++ProcessedPlayers;

        // Active player heuristic: must currently possess a pawn in this world
        APawn* Pawn = PS->GetPawn();
        if (!Pawn || Pawn->GetWorld() != World)
        {
            continue;
        }

        UAeyerjiLevelingComponent* Leveling = Pawn->FindComponentByClass<UAeyerjiLevelingComponent>();
        if (!Leveling) continue;

        const bool bIsKiller = (PS == KillerPS);
        const float Mult = bHasKillerPS ? (bIsKiller ? (1.f + BonusFrac) : (1.f - BonusFrac)) : 1.f;
		const float ToGive = SafeRewardValue(ScaledXP * Mult, 0.f, 0.f, MaxRewardXP);
        Leveling->AddXP(ToGive);
    }
}

void UAeyerjiXPLibrary::AwardXPToEnemiesOnPlayerDeath(const UObject* WorldContextObject,
                                                      const AActor* DeadPlayer,
                                                      AActor* Killer,
                                                      float Radius)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !IsValid(DeadPlayer) || DeadPlayer->GetWorld() != World || !DeadPlayer->HasAuthority()) return;
    if (World->GetNetMode() == NM_Client) return; // server only
	if (!IsValid(Killer) || Killer->GetWorld() != World)
	{
		Killer = nullptr;
	}

	const FVector Origin = DeadPlayer->GetActorLocation();
	if (!IsFiniteRewardVector(Origin))
	{
		return;
	}
	const float SafeRadius = SafeRewardValue(Radius, 2500.f, 0.f, MaxRewardRadius);

    const float BaseXP = GetBaseXPFromActor(DeadPlayer);
    if (BaseXP <= 0.f) return;

    // Prefer scalars from the dead player's RewardTuning when set
    float EffectiveScalar = 0.5f;
    float EffectiveBonus  = 1.0f;
    if (const UAeyerjiRewardTuning* Tuning = GetRewardTuningFromActor(DeadPlayer))
    {
        float V;
        if (Resolve_PerLevelScalar(Tuning, V))       EffectiveScalar = V;
        if (Resolve_KillerBonusPercent(Tuning, V))   EffectiveBonus  = V;
    }
    else
    {
        ensureMsgf(false, TEXT("AwardXPToEnemiesOnPlayerDeath: %s has no RewardTuning (AeyerjiRewardConfigComponent missing?). Aborting award."), *GetNameSafe(DeadPlayer));
        return;
    }

    const int32 PlayerLevel = GetActorLevel_Safe(DeadPlayer);
	const double Factor = 1.0 + static_cast<double>(FMath::Max(0, PlayerLevel - 1))
		* SafeRewardValue(EffectiveScalar, 0.f, 0.f, MaxRewardScalar);
	const float ScaledXP = static_cast<float>(FMath::Clamp(
		static_cast<double>(BaseXP) * Factor, 0.0, static_cast<double>(MaxRewardXP)));

	const float BonusFrac = SafeRewardValue(EffectiveBonus, 0.f, 0.f, 100.f) * 0.01f;

    const APawn* KillerPawn = Cast<APawn>(Killer);

    // Iterate all pawns; give XP to AI enemies within radius
    for (TActorIterator<APawn> It(World); It; ++It)
    {
        APawn* Pawn = *It;
		if (!IsValid(Pawn) || Pawn->IsPlayerControlled() || !Cast<AEnemyParentNative>(Pawn)) continue;
		const FVector PawnLocation = Pawn->GetActorLocation();
		if (!IsFiniteRewardVector(PawnLocation)) continue;
		if (SafeRadius > 0.f && FVector::DistSquared(PawnLocation, Origin) > FMath::Square(SafeRadius))
        {
            continue;
        }

        UAeyerjiLevelingComponent* Leveling = Pawn->FindComponentByClass<UAeyerjiLevelingComponent>();
        if (!Leveling) continue;

        const bool bIsKiller = (KillerPawn && (Pawn == KillerPawn));
        const float Mult = bIsKiller ? (1.f + BonusFrac) : (1.f - BonusFrac);
		const float ToGive = SafeRewardValue(ScaledXP * Mult, 0.f, 0.f, MaxRewardXP);
        Leveling->AddXP(ToGive);
    }
}
