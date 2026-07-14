#include "Progression/AeyerjiLevelingComponent.h"
#include "Progression/AeyerjiProgressionLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"

#include "Aeyerji/AeyerjiSaveGame.h"
#include "Engine/CurveTable.h"           // FCurveTableRowHandle (5.6)
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"

UAeyerjiLevelingComponent::UAeyerjiLevelingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(false); // attributes themselves replicate
}

void UAeyerjiLevelingComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return; // server-only
    }

    if (bInitializeXPMaxFromCurve)
    {
        if (const UAeyerjiAttributeSet* Attr = GetAttr())
        {
            const int32 Level = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
            if (!FMath::IsNearlyEqual(Attr->GetLevel(), static_cast<float>(Level)))
            {
                ServerSetLevel(Level);
            }
            const float NewMax = ComputeXPMaxForLevel(Level);
            if (NewMax > 0.f)
            {
                ServerSetXPMax(NewMax);
                ServerSetXP(FMath::Clamp(Attr->GetXP(), 0.f, NewMax));
            }
        }
    }

    if (bGrantAbilitiesOnBeginPlay)
    {
        RefreshOwnedAbilities();
    }

    ReapplyInfiniteEffects();
}

/* ---------- Public API ---------- */

void UAeyerjiLevelingComponent::AddXP(float DeltaXP)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || FMath::IsNearlyZero(DeltaXP))
    {
        return;
    }
    UAbilitySystemComponent* ASC = GetASC();
    const UAeyerjiAttributeSet* Attr = GetAttr();
    if (!ASC || !Attr) return;

    int32 Level = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
    const int32 OldLevel = Level;
    float XP    = Attr->GetXP() + DeltaXP;
    float XPMax = Attr->GetXPMax();

    bool bLeveled = false;
    while (XP >= XPMax && Level < UAeyerjiDifficultySettings::GetMaxGameplayLevel())
    {
        XP    -= XPMax;
        Level += 1;
        XPMax  = ComputeXPMaxForLevel(Level);
        bLeveled = true;
    }

    if (bLeveled)
    {
        ServerSetLevel(Level);
        ServerSetXPMax(XPMax);
    }

    ServerSetXP(FMath::Clamp(XP, 0.f, XPMax));

    if (bLeveled)
    {
        if (APawn* Pawn = Cast<APawn>(GetOwner()))
        {
            if (AAeyerjiPlayerState* AeyerjiPS = Pawn->GetPlayerState<AAeyerjiPlayerState>())
            {
                AeyerjiPS->GrantAbilityPoints(Level - OldLevel);
            }
        }

        RefreshOwnedAbilities();
        ReapplyInfiniteEffects();
        OnLevelUp.Broadcast(OldLevel, Level);
    }

    SyncProfileProgressionCache(TEXT("AddXP"));
}

void UAeyerjiLevelingComponent::SetLevel(int32 NewLevel)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    NewLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(NewLevel);

    const UAeyerjiAttributeSet* Attr = GetAttr();
    const int32 OldLevel = Attr ? UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel())) : 1;

    ServerSetLevel(NewLevel);

    const float NewMax = ComputeXPMaxForLevel(NewLevel);
    if (NewMax > 0.f)
    {
        ServerSetXPMax(NewMax);
        if (Attr)
        {
            ServerSetXP(FMath::Clamp(Attr->GetXP(), 0.f, NewMax));
        }
    }

    RefreshOwnedAbilities();
    ReapplyInfiniteEffects();

    OnLevelUp.Broadcast(OldLevel, NewLevel);
    SyncProfileProgressionCache(TEXT("SetLevel"));
}

/* ---------- Internals ---------- */

UAbilitySystemComponent* UAeyerjiLevelingComponent::GetASC() const
{
    if (CachedASC.IsValid())
        return CachedASC.Get();

    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;

    if (Owner->GetClass()->ImplementsInterface(UAbilitySystemInterface::StaticClass()))
    {
        if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner))
        {
            CachedASC = ASI->GetAbilitySystemComponent();
        }
    }
    else
    {
        CachedASC = Owner->FindComponentByClass<UAbilitySystemComponent>();
    }
    return CachedASC.Get();
}

const UAeyerjiAttributeSet* UAeyerjiLevelingComponent::GetAttr() const
{
    if (CachedAttr.IsValid())
        return CachedAttr.Get();

    if (UAbilitySystemComponent* ASC = GetASC())
    {
        // 5.6: this overload is const, so it yields const UAeyerjiAttributeSet*
        CachedAttr = ASC->GetSet<UAeyerjiAttributeSet>();
    }
    return CachedAttr.Get();
}

float UAeyerjiLevelingComponent::ComputeXPMaxForLevel(int32 Level) const
{
    const float SharedXPRequirement = UAeyerjiProgressionLibrary::GetXPRequiredForLevel(Level);
    if (SharedXPRequirement > 0.f)
    {
        return SharedXPRequirement;
    }

    // Legacy fallback for projects that still carry a per-component curve override.
    if (!XPToReachLevelRow.IsNull())
    {
        const float Y =
            XPToReachLevelRow.Eval(
                (float)Level,
                TEXT("UAeyerjiLevelingComponent::XPToReachLevelRow")); // context for warnings

        if (Y > 0.f)
        {
            return FMath::Max(1.f, Y);
        }
    }

    // Fallback to current XPMax if no curve or bad data.
    if (const UAeyerjiAttributeSet* Attr = GetAttr())
    {
        return FMath::Max(1.f, Attr->GetXPMax());
    }
    return 100.f;
}

void UAeyerjiLevelingComponent::TryProcessLevelUps()
{
    const UAeyerjiAttributeSet* Attr = GetAttr();
    UAbilitySystemComponent* ASC     = GetASC();
    if (!Attr || !ASC) return;

    int32 Level = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
    float XP    = Attr->GetXP();
    float XPMax = Attr->GetXPMax();

    bool bLeveled = false;

    while (XP >= XPMax && Level < UAeyerjiDifficultySettings::GetMaxGameplayLevel())
    {
        XP    -= XPMax;
        Level += 1;
        XPMax  = ComputeXPMaxForLevel(Level);
        bLeveled = true;
    }

    if (bLeveled)
    {
        const int32 OldLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));

        ServerSetLevel(Level);
        ServerSetXPMax(XPMax);
        ServerSetXP(FMath::Clamp(XP, 0.f, XPMax));

        if (APawn* Pawn = Cast<APawn>(GetOwner()))
        {
            if (AAeyerjiPlayerState* AeyerjiPS = Pawn->GetPlayerState<AAeyerjiPlayerState>())
            {
                AeyerjiPS->GrantAbilityPoints(Level - OldLevel);
            }
        }

        RefreshOwnedAbilities();
        ReapplyInfiniteEffects();

        OnLevelUp.Broadcast(OldLevel, Level);
        SyncProfileProgressionCache(TEXT("ProcessLevelUps"));
    }
}

void UAeyerjiLevelingComponent::RefreshOwnedAbilities() const
{
    UAbilitySystemComponent* ASC         = GetASC();
    const UAeyerjiAttributeSet* Attr     = GetAttr();
    if (!ASC || !Attr) return;

    const int32 CurrentLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));

    for (const FLevelScaledAbility& Def : AbilitiesToOwn)
    {
        if (!Def.Ability) continue;

        // Remove existing specs for that class.
        TArray<FGameplayAbilitySpecHandle> ToClear;
        for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
        {
            if (Spec.Ability && Spec.Ability->GetClass() == Def.Ability)
            {
                ToClear.Add(Spec.Handle);
            }
        }
        for (const FGameplayAbilitySpecHandle& H : ToClear)
        {
            ASC->ClearAbility(H);
        }

        const int32 SpecLevel = Def.bScaleWithLevel ? CurrentLevel : 1;
        FGameplayAbilitySpec NewSpec(Def.Ability, SpecLevel, Def.InputID, GetOwner());
        ASC->GiveAbility(NewSpec);
    }
}

void UAeyerjiLevelingComponent::ReapplyInfiniteEffects() const
{
    UAbilitySystemComponent* ASC         = GetASC();
    const UAeyerjiAttributeSet* Attr     = GetAttr();
    if (!ASC || !Attr) return;

    const int32 CurrentLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));

    for (TSubclassOf<UGameplayEffect> GEClass : ReapplyInfiniteEffectsOnLevelUp)
    {
        if (!GEClass) continue;

        // Remove any existing instance(s) of this GE on self so we do not stack duplicates.
        {
            FGameplayEffectQuery Query;
            Query.CustomMatchDelegate.BindLambda([GEClass](const FActiveGameplayEffect& Active)
            {
                return Active.Spec.Def && Active.Spec.Def->GetClass() == GEClass;
            });
            ASC->RemoveActiveEffects(Query);
        }

        // Apply fresh at current level; include source object for clearer auditing
        FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
        Ctx.AddSourceObject(GetOwner());
        FGameplayEffectSpecHandle    SH  = ASC->MakeOutgoingSpec(GEClass, CurrentLevel, Ctx);
        if (SH.IsValid())
        {
            ASC->ApplyGameplayEffectSpecToSelf(*SH.Data.Get());
        }
    }}

/* ---------- Numeric writes ---------- */

void UAeyerjiLevelingComponent::ServerSetXP(float NewXP) const
{
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetXPAttribute(), NewXP);
    }
}

void UAeyerjiLevelingComponent::ServerSetXPMax(float NewXPMax) const
{
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetXPMaxAttribute(), NewXPMax);
    }
}

void UAeyerjiLevelingComponent::ServerSetLevel(int32 NewLevel) const
{
    if (UAbilitySystemComponent* ASC = GetASC())
    {
        ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), (float)NewLevel);
    }
}

void UAeyerjiLevelingComponent::SyncProfileProgressionCache(const TCHAR* Reason) const
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority())
    {
        return;
    }

    const UAeyerjiAttributeSet* Attr = GetAttr();
    if (!Attr)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ProfileXP] CacheSync skipped Reason=%s Owner=%s Detail=MissingAttributeSet"),
            Reason ? Reason : TEXT("Unknown"),
            *GetNameSafe(Owner));
        return;
    }

    const APawn* PawnOwner = Cast<APawn>(Owner);
    const APlayerState* PlayerState = PawnOwner ? PawnOwner->GetPlayerState() : Cast<APlayerState>(Owner);
    if (!PlayerState)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ProfileXP] CacheSync skipped Reason=%s Owner=%s Detail=NoPlayerState Level=%d XP=%.2f"),
            Reason ? Reason : TEXT("Unknown"),
            *GetNameSafe(Owner),
            UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel())),
            Attr->GetXP());
        return;
    }

    UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    UAeyerjiSaveManagerSubsystem* SaveManager =
        GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
    if (!SaveManager)
    {
        return;
    }

    UAeyerjiSaveGame* CachedProfile = nullptr;
    if (!SaveManager->GetServerCachedProfile(PlayerState, CachedProfile) || !CachedProfile)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ProfileXP] CacheSync skipped Reason=%s PlayerState=%s Detail=NoServerCachedProfile Level=%d XP=%.2f"),
            Reason ? Reason : TEXT("Unknown"),
            *GetNameSafe(PlayerState),
            UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel())),
            Attr->GetXP());
        return;
    }

    CachedProfile->Attributes.Level = UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
    CachedProfile->Attributes.XP = FMath::Max(0.f, Attr->GetXP());
    UE_LOG(LogTemp, Display,
        TEXT("[ProfileXP] CacheSync Reason=%s PlayerState=%s Level=%d XP=%.2f XPMax=%.2f Revision=%lld"),
        Reason ? Reason : TEXT("Unknown"),
        *GetNameSafe(PlayerState),
        CachedProfile->Attributes.Level,
        CachedProfile->Attributes.XP,
        Attr->GetXPMax(),
        CachedProfile->Revision);
}

void UAeyerjiLevelingComponent::AddReapplyInfiniteEffect(TSubclassOf<UGameplayEffect> GEClass)
{
    if (!GEClass)
    {
        return;
    }
    // Only store unique classes
    for (const TSubclassOf<UGameplayEffect>& Existing : ReapplyInfiniteEffectsOnLevelUp)
    {
        if (Existing == GEClass)
        {
            return;
        }
    }
    ReapplyInfiniteEffectsOnLevelUp.Add(GEClass);
}

void UAeyerjiLevelingComponent::ForceRefreshForCurrentLevel()
{
    RefreshOwnedAbilities();
    ReapplyInfiniteEffects();
}


