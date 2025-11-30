#include "Progression/AeyerjiLevelingComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"

#include "Engine/CurveTable.h"           // FCurveTableRowHandle (5.6)
#include "GameFramework/Actor.h"

#include "Attributes/AeyerjiAttributeSet.h"

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
            const int32 Level  = FMath::RoundToInt(Attr->GetLevel());
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

    int32 Level = FMath::RoundToInt(Attr->GetLevel());
    const int32 OldLevel = Level;
    float XP    = Attr->GetXP() + DeltaXP;
    float XPMax = Attr->GetXPMax();

    bool bLeveled = false;
    while (XP >= XPMax && Level < 999)
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
        RefreshOwnedAbilities();
        ReapplyInfiniteEffects();
        OnLevelUp.Broadcast(OldLevel, Level);
    }
}

void UAeyerjiLevelingComponent::SetLevel(int32 NewLevel)
{
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        return;
    }

    NewLevel = FMath::Clamp(NewLevel, 1, 999);

    const UAeyerjiAttributeSet* Attr = GetAttr();
    const int32 OldLevel = Attr ? FMath::RoundToInt(Attr->GetLevel()) : 1;

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
    // Best practice in 5.6: use the RowHandle's Eval() instead of grabbing the FRealCurve*
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

    int32 Level = FMath::RoundToInt(Attr->GetLevel());
    float XP    = Attr->GetXP();
    float XPMax = Attr->GetXPMax();

    bool bLeveled = false;

    while (XP >= XPMax && Level < 999)
    {
        XP    -= XPMax;
        Level += 1;
        XPMax  = ComputeXPMaxForLevel(Level);
        bLeveled = true;
    }

    if (bLeveled)
    {
        const int32 OldLevel = FMath::RoundToInt(Attr->GetLevel());

        ServerSetLevel(Level);
        ServerSetXPMax(XPMax);
        ServerSetXP(FMath::Clamp(XP, 0.f, XPMax));

        RefreshOwnedAbilities();
        ReapplyInfiniteEffects();

        OnLevelUp.Broadcast(OldLevel, Level);
    }
}

void UAeyerjiLevelingComponent::RefreshOwnedAbilities() const
{
    UAbilitySystemComponent* ASC         = GetASC();
    const UAeyerjiAttributeSet* Attr     = GetAttr();
    if (!ASC || !Attr) return;

    const int32 CurrentLevel = FMath::RoundToInt(Attr->GetLevel());

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

    const int32 CurrentLevel = FMath::RoundToInt(Attr->GetLevel());

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


