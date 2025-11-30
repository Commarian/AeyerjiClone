// W_AeyerjiStatusBar.cpp

#include "GUI/W_AeyerjiStatusBar.h"
#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/TextBlock.h"

bool UW_AeyerjiStatusBar::BP_ShouldShowResource_Implementation(UAbilitySystemComponent* /*ASC*/)
{
    // Default: no extra rule; auto-detect + tag scan decide.
    return false;
}

void UW_AeyerjiStatusBar::BindToAttributes(UAbilitySystemComponent* InASC,
                                           FGameplayAttribute InHealth, FGameplayAttribute InMaxHealth,
                                           FGameplayAttribute InMana,   FGameplayAttribute InMaxMana)
{
    ASC            = InASC;
    HealthAttr     = InHealth;
    MaxHealthAttr  = InMaxHealth;
    ManaAttr       = InMana;
    MaxManaAttr    = InMaxMana;

    // Initial pulls
    RecalculateTargets();
    HealthMain = HealthGhost = HealthTarget;
    ManaMain   = ManaGhost   = ManaTarget;

    // Visibility pass for resource
    UpdateResourceVisibility();

    // Unbind previous (if any)
    if (ASC.IsValid())
    {
        if (HealthChangedHandle.IsValid())    { ASC->GetGameplayAttributeValueChangeDelegate(HealthAttr).Remove(HealthChangedHandle); }
        if (MaxHealthChangedHandle.IsValid()) { ASC->GetGameplayAttributeValueChangeDelegate(MaxHealthAttr).Remove(MaxHealthChangedHandle); }
        if (ManaChangedHandle.IsValid())      { ASC->GetGameplayAttributeValueChangeDelegate(ManaAttr).Remove(ManaChangedHandle); }
        if (MaxManaChangedHandle.IsValid())   { ASC->GetGameplayAttributeValueChangeDelegate(MaxManaAttr).Remove(MaxManaChangedHandle); }

        HealthChangedHandle    = ASC->GetGameplayAttributeValueChangeDelegate(HealthAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnHealthChanged);
        MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(MaxHealthAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxHealthChanged);
        ManaChangedHandle      = ASC->GetGameplayAttributeValueChangeDelegate(ManaAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnManaChanged);
        MaxManaChangedHandle   = ASC->GetGameplayAttributeValueChangeDelegate(MaxManaAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxManaChanged);
    }

    // Push initial percents
    if (HealthBar)       HealthBar->SetPercent(HealthMain);
    if (HealthBar_Ghost) HealthBar_Ghost->SetPercent(HealthGhost);
    if (ManaBar)         ManaBar->SetPercent(ManaMain);
    if (ManaBar_Ghost)   ManaBar_Ghost->SetPercent(ManaGhost);

    UpdateColors();

    // Initial numeric labels (optional)
    UpdateHPValueLabels();
    UpdateManaValueLabels();
}

void UW_AeyerjiStatusBar::BindToAttributesWithXP(UAbilitySystemComponent* InASC,
                                                 FGameplayAttribute InHealth,   FGameplayAttribute InMaxHealth,
                                                 FGameplayAttribute InMana,     FGameplayAttribute InMaxMana,
                                                 FGameplayAttribute InXP,       FGameplayAttribute InXPMax)
{
    BindToAttributes(InASC, InHealth, InMaxHealth, InMana, InMaxMana);

    XPAttr    = InXP;
    XPMaxAttr = InXPMax;

    if (ASC.IsValid())
    {
        if (XPChangedHandle.IsValid())      { ASC->GetGameplayAttributeValueChangeDelegate(XPAttr).Remove(XPChangedHandle); }
        if (MaxXPChangedHandle.IsValid())   { ASC->GetGameplayAttributeValueChangeDelegate(XPMaxAttr).Remove(MaxXPChangedHandle); }

        XPChangedHandle    = ASC->GetGameplayAttributeValueChangeDelegate(XPAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnXPChanged);
        MaxXPChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(XPMaxAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxXPChanged);
    }

    // Initialize XP visuals
    RecalculateTargets();
    XPMain = XPGhost = XPTarget;
    if (XPBar)       XPBar->SetPercent(XPMain);
    if (XPBar_Ghost) XPBar_Ghost->SetPercent(XPGhost);

    UpdateXPLabel();
}

void UW_AeyerjiStatusBar::BindToAttributesWithXPAndLevel(UAbilitySystemComponent* InASC,
                                                         FGameplayAttribute InHealth,   FGameplayAttribute InMaxHealth,
                                                         FGameplayAttribute InMana,     FGameplayAttribute InMaxMana,
                                                         FGameplayAttribute InXP,       FGameplayAttribute InXPMax,
                                                         FGameplayAttribute InLevel)
{
    BindToAttributesWithXP(InASC, InHealth, InMaxHealth, InMana, InMaxMana, InXP, InXPMax);

    LevelAttr = InLevel;
    if (ASC.IsValid())
    {
        if (LevelChangedHandle.IsValid()) { ASC->GetGameplayAttributeValueChangeDelegate(LevelAttr).Remove(LevelChangedHandle); }
        LevelChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(LevelAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnLevelChanged);
    }
    UpdateLevelLabel();
}

void UW_AeyerjiStatusBar::NativeDestruct()
{
    if (ASC.IsValid())
    {
        if (HealthChangedHandle.IsValid())    ASC->GetGameplayAttributeValueChangeDelegate(HealthAttr).Remove(HealthChangedHandle);
        if (MaxHealthChangedHandle.IsValid()) ASC->GetGameplayAttributeValueChangeDelegate(MaxHealthAttr).Remove(MaxHealthChangedHandle);
        if (ManaChangedHandle.IsValid())      ASC->GetGameplayAttributeValueChangeDelegate(ManaAttr).Remove(ManaChangedHandle);
        if (MaxManaChangedHandle.IsValid())   ASC->GetGameplayAttributeValueChangeDelegate(MaxManaAttr).Remove(MaxManaChangedHandle);
        if (XPChangedHandle.IsValid())        ASC->GetGameplayAttributeValueChangeDelegate(XPAttr).Remove(XPChangedHandle);
        if (MaxXPChangedHandle.IsValid())     ASC->GetGameplayAttributeValueChangeDelegate(XPMaxAttr).Remove(MaxXPChangedHandle);
        if (LevelChangedHandle.IsValid())     ASC->GetGameplayAttributeValueChangeDelegate(LevelAttr).Remove(LevelChangedHandle);
        if (HPRegenChangedHandle.IsValid())    ASC->GetGameplayAttributeValueChangeDelegate(HPRegenAttr).Remove(HPRegenChangedHandle);
        if (ManaRegenChangedHandle.IsValid())  ASC->GetGameplayAttributeValueChangeDelegate(ManaRegenAttr).Remove(ManaRegenChangedHandle);
    }
    Super::NativeDestruct();
}

void UW_AeyerjiStatusBar::OnHealthChanged(const FOnAttributeChangeData& /*Data*/)
{
    const float OldNorm = HealthTarget;
    RecalculateTargets();

    const bool bDamage = HealthTarget < OldNorm;
    if (bDamage)
    {
        // Start damage flash and ghost hold
        DmgFlash = 1.f;
        HealthGhostHold = ChipHoldTime;
    }
    else
    {
        // Heal flash
        HealFlash = 1.f;
    }

    UpdateColors();
    UpdateHPValueLabels();
}

void UW_AeyerjiStatusBar::OnMaxHealthChanged(const FOnAttributeChangeData& /*Data*/)
{
    RecalculateTargets();
    UpdateColors();
    UpdateHPValueLabels();
}

void UW_AeyerjiStatusBar::OnManaChanged(const FOnAttributeChangeData& /*Data*/)
{
    const float OldNorm = ManaTarget;
    RecalculateTargets();

    const bool bSpend = ManaTarget < OldNorm;
    if (bSpend)
    {
        if (ManaGhost < ManaMain) ManaGhost = ManaMain;
        ManaGhostHold = ChipHoldTime;
    }
    // (heals/refund handled by smoothing)
    UpdateManaValueLabels();
}

void UW_AeyerjiStatusBar::OnMaxManaChanged(const FOnAttributeChangeData& /*Data*/)
{
    RecalculateTargets();
    UpdateResourceVisibility();
    UpdateManaValueLabels();
}

void UW_AeyerjiStatusBar::OnXPChanged(const FOnAttributeChangeData& /*Data*/)
{
    const float Old = XPTarget;
    RecalculateTargets();
    const bool bXpDown = XPTarget < Old; // happens during level-ups
    if (bXpDown)
    {
        if (XPGhost < XPMain) XPGhost = XPMain;
        XPGhostHold = ChipHoldTime;
    }
}

void UW_AeyerjiStatusBar::OnMaxXPChanged(const FOnAttributeChangeData& /*Data*/)
{
    RecalculateTargets();
    UpdateXPLabel();
}

void UW_AeyerjiStatusBar::OnLevelChanged(const FOnAttributeChangeData& /*Data*/)
{
    UpdateLevelLabel();
}

void UW_AeyerjiStatusBar::OnHPRegenChanged(const FOnAttributeChangeData& /*Data*/)
{
    UpdateRegenLabels();
}

void UW_AeyerjiStatusBar::OnManaRegenChanged(const FOnAttributeChangeData& /*Data*/)
{
    UpdateRegenLabels();
}

void UW_AeyerjiStatusBar::RecalculateTargets()
{
    if (!ASC.IsValid())
    {
        HealthTarget = ManaTarget = XPTarget = 1.f;
        return;
    }

    const float CurHP  = ASC->GetNumericAttribute(HealthAttr);
    const float MaxHP  = ASC->GetNumericAttribute(MaxHealthAttr);
    const float CurMP  = ASC->GetNumericAttribute(ManaAttr);
    const float MaxMP  = ASC->GetNumericAttribute(MaxManaAttr);
    const bool bHasXP  = XPAttr.IsValid() && XPMaxAttr.IsValid();
    const float CurXP  = bHasXP ? ASC->GetNumericAttribute(XPAttr)    : 0.f;
    const float MaxXP  = bHasXP ? ASC->GetNumericAttribute(XPMaxAttr) : 1.f;

    HealthTarget = FMath::Clamp(SafeDiv(CurHP, MaxHP), 0.f, 1.f);
    ManaTarget   = FMath::Clamp(SafeDiv(CurMP, MaxMP), 0.f, 1.f);
    XPTarget     = FMath::Clamp(SafeDiv(CurXP, MaxXP), 0.f, 1.f);
    UpdateXPLabel();
    UpdateHPValueLabels();
    UpdateManaValueLabels();
    UpdateRegenLabels();
}

float UW_AeyerjiStatusBar::SafeDiv(float Numerator, float Denominator)
{
    return (FMath::IsNearlyZero(Denominator)) ? 1.f : Numerator / Denominator;
}

void UW_AeyerjiStatusBar::NativeTick(const FGeometry& MyGeometry, float Dt)
{
    Super::NativeTick(MyGeometry, Dt);

    // HEALTH smoothing
    const bool bHealthDamage = HealthTarget < HealthMain;
    TickBar(Dt, HealthTarget, HealthMain, HealthGhost, HealthGhostHold, HealthBar, HealthBar_Ghost, bHealthDamage);

    // MANA smoothing (only if visible; but UpdateResourceVisibility may have hidden it - null checks are fine)
    const bool bManaSpend = ManaTarget < ManaMain;
    TickBar(Dt, ManaTarget, ManaMain, ManaGhost, ManaGhostHold, ManaBar, ManaBar_Ghost, bManaSpend);

    // XP smoothing (optional)
    const bool bXPDrop = XPTarget < XPMain;
    TickBar(Dt, XPTarget, XPMain, XPGhost, XPGhostHold, XPBar, XPBar_Ghost, bXPDrop);

    ApplyFX(Dt);
}

void UW_AeyerjiStatusBar::TickBar(float Dt, float Target, float& Main, float& Ghost, float& Hold,
                                  UProgressBar* MainPB, UProgressBar* GhostPB,
                                  bool bWasDamage)
{
    if (!MainPB) return;

    // Move main toward target
    const float FillSpeed = (Target > Main) ? FillLerpSpeed_HealUp : FillLerpSpeed_DmgDown;
    Main = UKismetMathLibrary::FInterpTo(Main, Target, Dt, FillSpeed);
    MainPB->SetPercent(Main);

    // Guard: ghost always >= main
    if (Ghost < Main) Ghost = Main;

    // Ghost behavior
    if (GhostPB)
    {
        if (bWasDamage)
        {
            // linger at old value for a moment, then slide down
            Hold = FMath::Max(0.f, Hold - Dt);
            if (Hold <= 0.f)
            {
                Ghost = UKismetMathLibrary::FInterpTo(Ghost, Target, Dt, ChipLerpSpeedDown);
            }
        }
        else
        {
            // heal/refund: ghost catches up more quickly upward
            Ghost = UKismetMathLibrary::FInterpTo(Ghost, Target, Dt, ChipLerpSpeedDown * 1.5f);
        }
        GhostPB->SetPercent(Ghost);
    }
}

void UW_AeyerjiStatusBar::ApplyFX(float Dt)
{
    // Fade flashes
    if (ImgHealFlash && HealFlash > 0.f)
    {
        HealFlash = FMath::Max(0.f, HealFlash - FlashFadePerSec * Dt);
        ImgHealFlash->SetRenderOpacity(HealFlash);
    }
    if (ImgDamageFlash && DmgFlash > 0.f)
    {
        DmgFlash = FMath::Max(0.f, DmgFlash - FlashFadePerSec * Dt);
        ImgDamageFlash->SetRenderOpacity(DmgFlash);
    }
}

void UW_AeyerjiStatusBar::UpdateColors()
{
    if (!HealthBar) return;

    // Pulse color when low HP
    const float H = HealthMain;
    const bool bLow = (H <= LowHPThreshold);
    FLinearColor C = HealthColor_Normal;

    if (bLow)
    {
        // soft pulse between Low and Normal
        const float Pulse = 0.5f + 0.5f * FMath::Sin(GetWorld()->TimeSeconds * 6.0f);
        C = FMath::Lerp(HealthColor_Low, HealthColor_Normal, Pulse);
    }
    HealthBar->SetFillColorAndOpacity(C);
}

void UW_AeyerjiStatusBar::UpdateResourceVisibility()
{
    ESlateVisibility Vis = ESlateVisibility::Collapsed;

    switch (ResourceVisibility)
    {
    case EResourceVisibilityPolicy::ForceHide:
        Vis = ESlateVisibility::Collapsed;
        break;

    case EResourceVisibilityPolicy::ForceShow:
        Vis = ESlateVisibility::HitTestInvisible;
        break;

    case EResourceVisibilityPolicy::Auto:
    default:
    {
        const bool bShow = AutoDetectUsesResource() || BP_ShouldShowResource(ASC.Get());
        Vis = bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
        break;
    }
    }

    if (ManaBar)       ManaBar->SetVisibility(Vis);
    if (ManaBar_Ghost) ManaBar_Ghost->SetVisibility(Vis);
    if (ImgHealFlash)  {} // heal flash usually sits on HP only, leave as is
}

bool UW_AeyerjiStatusBar::AutoDetectUsesResource() const
{
    if (!ASC.IsValid())
        return false;

    // Simple, reliable heuristic: if MaxMana is meaningful, show resource
    const float MaxRes = ASC->GetNumericAttribute(MaxManaAttr);
    if (MaxRes > MinResourceToShow)
    {
        return true;
    }

    // Optional: if any granted ability has the specified tag, treat as mana user
    if (ManaAbilityTag.IsValid())
    {
        for (TArray<FGameplayAbilitySpec> Specs = ASC->GetActivatableAbilities(); const FGameplayAbilitySpec& Spec : Specs)
        {
            const UGameplayAbility* GA = Spec.Ability;
            if (!GA) continue;

            const FGameplayTagContainer& AbilityTags = GA->GetAssetTags();
            if (AbilityTags.HasTag(ManaAbilityTag))
            {
                return true;
            }
        }
    }

    return false;
}

void UW_AeyerjiStatusBar::UpdateXPLabel()
{
    if (!ASC.IsValid() || !XPText) return;
    if (!(XPAttr.IsValid() && XPMaxAttr.IsValid())) return;

    const float CurXP = ASC->GetNumericAttribute(XPAttr);
    const float MaxXP = ASC->GetNumericAttribute(XPMaxAttr);
    const int32 Cur = FMath::FloorToInt(CurXP);
    const int32 Max = FMath::Max(1, FMath::FloorToInt(MaxXP));

    XPText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d XP"), Cur, Max)));
}

void UW_AeyerjiStatusBar::UpdateLevelLabel()
{
    if (!ASC.IsValid() || !LevelText) return;
    if (!LevelAttr.IsValid()) return;

    const int32 Lvl = FMath::Max(1, FMath::RoundToInt(ASC->GetNumericAttribute(LevelAttr)));
    LevelText->SetText(FText::AsNumber(Lvl));
}

void UW_AeyerjiStatusBar::UpdateHPValueLabels()
{
    if (!ASC.IsValid()) return;
    const int32 CurHP = FMath::FloorToInt(ASC->GetNumericAttribute(HealthAttr));
    const int32 MaxHP = FMath::FloorToInt(ASC->GetNumericAttribute(MaxHealthAttr));
    if (HPValueText)
    {
        HPValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d HP"), CurHP, MaxHP)));
    }
}

void UW_AeyerjiStatusBar::UpdateManaValueLabels()
{
    if (!ASC.IsValid()) return;
    const int32 CurMana = FMath::FloorToInt(ASC->GetNumericAttribute(ManaAttr));
    const int32 MaxMana = FMath::FloorToInt(ASC->GetNumericAttribute(MaxManaAttr));
    if (ManaValueText)
    {
        ManaValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d Mana"), CurMana, MaxMana)));
    }
}

void UW_AeyerjiStatusBar::UpdateRegenLabels()
{
    if (!ASC.IsValid()) return;
    if (HPRegenText && HPRegenAttr.IsValid())
    {
        const float Regen = ASC->GetNumericAttribute(HPRegenAttr);
        HPRegenText->SetText(FText::FromString(FString::Printf(TEXT("+%.1f"), Regen)));
    }
    if (ManaRegenText && ManaRegenAttr.IsValid())
    {
        const float Regen = ASC->GetNumericAttribute(ManaRegenAttr);
        ManaRegenText->SetText(FText::FromString(FString::Printf(TEXT("+%.1f"), Regen)));
    }
}

void UW_AeyerjiStatusBar::BindRegenAttributes(UAbilitySystemComponent* InASC,
                                              FGameplayAttribute InHPRegen,
                                              FGameplayAttribute InManaRegen)
{
    ASC = InASC;
    HPRegenAttr  = InHPRegen;
    ManaRegenAttr= InManaRegen;

    if (ASC.IsValid())
    {
        if (HPRegenChangedHandle.IsValid())   { ASC->GetGameplayAttributeValueChangeDelegate(HPRegenAttr).Remove(HPRegenChangedHandle); }
        if (ManaRegenChangedHandle.IsValid()) { ASC->GetGameplayAttributeValueChangeDelegate(ManaRegenAttr).Remove(ManaRegenChangedHandle); }

        HPRegenChangedHandle   = ASC->GetGameplayAttributeValueChangeDelegate(HPRegenAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnHPRegenChanged);
        ManaRegenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(ManaRegenAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnManaRegenChanged);
    }

    UpdateRegenLabels();
}
