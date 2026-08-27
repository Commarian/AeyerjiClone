// W_AeyerjiStatusBar.cpp

#include "GUI/W_AeyerjiStatusBar.h"
#include "AbilitySystemComponent.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Kismet/KismetMathLibrary.h"
#include "Components/TextBlock.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "AeyerjiStatusBar"

namespace
{
	constexpr float MaxStatusDisplayValue = 1000000000.f;

	float FiniteStatusValue(const float Value, const float DefaultValue = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}

	int32 StatusDisplayInteger(const float Value)
	{
		return FMath::FloorToInt(FMath::Clamp(
			FiniteStatusValue(Value), 0.f, MaxStatusDisplayValue));
	}

	FText FormatStatusPair(const FName Key, const FText& Fallback, const int32 Current, const int32 Maximum)
	{
		const FText Template = AeyerjiStringLibrary::GetGlobalStringTableText(Key);
		return FText::Format(Template.IsEmpty() ? Fallback : Template, FText::AsNumber(Current), FText::AsNumber(Maximum));
	}
}

bool UW_AeyerjiStatusBar::BP_ShouldShowResource_Implementation(UAbilitySystemComponent* /*ASC*/)
{
    // Default: no extra rule; auto-detect + tag scan decide.
    return false;
}

void UW_AeyerjiStatusBar::ConfigureHealthChunkDivisions(
    const bool bInEnabled,
    const bool bInRequireAnyEligibilityTag,
    const FGameplayTagContainer& InEligibilityTags,
    const int32 InTargetChunkCount,
    const int32 InMinPreferredChunkCount,
    const int32 InMaxPreferredChunkCount,
    const int32 InHardMaxChunkCount,
    const float InSeparatorThickness,
    const float InSeparatorVerticalInset,
    const FLinearColor& InSeparatorColor)
{
    bHealthChunkDivisionsEnabled = bInEnabled;
    bRequireAnyHealthChunkEligibilityTag = bInRequireAnyEligibilityTag;
    HealthChunkEligibilityTags = InEligibilityTags;
    HardMaxHealthChunkCount = FMath::Clamp(InHardMaxChunkCount, 1, 32);
    TargetHealthChunkCount = FMath::Clamp(InTargetChunkCount, 1, HardMaxHealthChunkCount);
    MinPreferredHealthChunkCount = FMath::Clamp(InMinPreferredChunkCount, 1, HardMaxHealthChunkCount);
    MaxPreferredHealthChunkCount = FMath::Clamp(
        InMaxPreferredChunkCount,
        MinPreferredHealthChunkCount,
        HardMaxHealthChunkCount);
    HealthChunkSeparatorThickness = FMath::Clamp(
        FMath::IsFinite(InSeparatorThickness) ? InSeparatorThickness : 1.f,
        0.25f,
        8.f);
    HealthChunkSeparatorVerticalInset = FMath::Clamp(
        FMath::IsFinite(InSeparatorVerticalInset) ? InSeparatorVerticalInset : 1.f,
        0.f,
        8.f);
    const bool bHasFiniteSeparatorColor =
        FMath::IsFinite(InSeparatorColor.R)
        && FMath::IsFinite(InSeparatorColor.G)
        && FMath::IsFinite(InSeparatorColor.B)
        && FMath::IsFinite(InSeparatorColor.A);
    HealthChunkSeparatorColor = bHasFiniteSeparatorColor
        ? InSeparatorColor
        : FLinearColor(0.01f, 0.01f, 0.01f, 0.9f);

    ChunkLayoutMaxHealth = -1.f;
    bHealthChunkTagEligible = false;
    const float MaxHealth = ASC.IsValid() && MaxHealthAttr.IsValid()
        ? ASC->GetNumericAttribute(MaxHealthAttr)
        : 0.f;
    UpdateHealthChunkLayout(MaxHealth);
    RefreshHealthChunkSeparatorWidgets();
    InvalidateLayoutAndVolatility();
}

void UW_AeyerjiStatusBar::SetOverlayWidthScale(const float InWidthScale)
{
    const float SafeWidthScale = FMath::Clamp(
        FMath::IsFinite(InWidthScale) ? InWidthScale : 1.f,
        0.05f,
        10.f);

    // The current floating widget uses a CanvasPanel with fixed-width bar slots.
    // Resize those slots directly so health and mana bars respond to owner size without
    // horizontally compressing the level text or any future labels/icons.
    bool bResizedCanvasSlot = false;
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(HealthBar, SafeWidthScale);
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(HealthBar_Ghost, SafeWidthScale);
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(ManaBar, SafeWidthScale);
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(ManaBar_Ghost, SafeWidthScale);
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(XPBar, SafeWidthScale);
    bResizedCanvasSlot |= SetOverlayCanvasSlotWidth(XPBar_Ghost, SafeWidthScale);
    if (bResizedCanvasSlot)
    {
        SetRenderScale(FVector2D(1.f, 1.f));
        RefreshHealthChunkSeparatorWidgets();
        return;
    }

    if (OverlaySizeBox)
    {
        if (OverlayBaseWidth <= KINDA_SMALL_NUMBER)
        {
            ForceLayoutPrepass();
            OverlayBaseWidth = OverlaySizeBox->IsWidthOverride()
                ? OverlaySizeBox->GetWidthOverride()
                : OverlaySizeBox->GetDesiredSize().X;
        }

        if (FMath::IsFinite(OverlayBaseWidth) && OverlayBaseWidth > KINDA_SMALL_NUMBER)
        {
            OverlaySizeBox->SetWidthOverride(OverlayBaseWidth * SafeWidthScale);
            SetRenderScale(FVector2D(1.f, 1.f));
            return;
        }
    }

    // Existing widgets remain compatible until their Blueprint adds OverlaySizeBox.
    // The fallback only changes width and preserves the authored vertical position/height.
    SetRenderScale(FVector2D(SafeWidthScale, 1.f));
    RefreshHealthChunkSeparatorWidgets();
}

bool UW_AeyerjiStatusBar::SetOverlayCanvasSlotWidth(UWidget* Widget, const float WidthScale)
{
    if (!Widget)
    {
        return false;
    }

    UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
    if (!CanvasSlot)
    {
        return false;
    }

    FOverlayCanvasSlotSize* CachedSize = OverlayCanvasSlotSizes.FindByPredicate(
        [Widget](const FOverlayCanvasSlotSize& Entry)
        {
            return Entry.Widget.Get() == Widget;
        });
    if (!CachedSize)
    {
        FOverlayCanvasSlotSize NewEntry;
        NewEntry.Widget = Widget;
        NewEntry.BaseSize = CanvasSlot->GetSize();
        NewEntry.BasePosition = CanvasSlot->GetPosition();
        CachedSize = &OverlayCanvasSlotSizes.Add_GetRef(NewEntry);
    }

    if (CachedSize->BaseSize.ContainsNaN()
        || CachedSize->BasePosition.ContainsNaN()
        || !FMath::IsFinite(CachedSize->BaseSize.X)
        || CachedSize->BaseSize.X <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float ScaledWidth = CachedSize->BaseSize.X * WidthScale;
    const float CenteringOffset = (CachedSize->BaseSize.X - ScaledWidth) * 0.5f;
    CanvasSlot->SetSize(FVector2D(ScaledWidth, CachedSize->BaseSize.Y));
    CanvasSlot->SetPosition(FVector2D(
        CachedSize->BasePosition.X + CenteringOffset,
        CachedSize->BasePosition.Y));
    return true;
}

void UW_AeyerjiStatusBar::BindToAttributes(UAbilitySystemComponent* InASC,
                                           FGameplayAttribute InHealth, FGameplayAttribute InMaxHealth,
                                           FGameplayAttribute InMana,   FGameplayAttribute InMaxMana)
{
	if (InASC && (!IsValid(InASC) || (GetWorld() && InASC->GetWorld() != GetWorld())))
	{
		InASC = nullptr;
	}

    UAbilitySystemComponent* OldASC = ASC.Get();
    const FGameplayAttribute OldHealthAttr = HealthAttr;
    const FGameplayAttribute OldMaxHealthAttr = MaxHealthAttr;
    const FGameplayAttribute OldManaAttr = ManaAttr;
    const FGameplayAttribute OldMaxManaAttr = MaxManaAttr;
    const FGameplayAttribute OldXPAttr = XPAttr;
    const FGameplayAttribute OldXPMaxAttr = XPMaxAttr;
    const FGameplayAttribute OldLevelAttr = LevelAttr;
    const FGameplayAttribute OldHPRegenAttr = HPRegenAttr;
    const FGameplayAttribute OldManaRegenAttr = ManaRegenAttr;

    if (OldASC)
    {
        if (HealthChangedHandle.IsValid() && OldHealthAttr.IsValid())       { OldASC->GetGameplayAttributeValueChangeDelegate(OldHealthAttr).Remove(HealthChangedHandle); }
        if (MaxHealthChangedHandle.IsValid() && OldMaxHealthAttr.IsValid()) { OldASC->GetGameplayAttributeValueChangeDelegate(OldMaxHealthAttr).Remove(MaxHealthChangedHandle); }
        if (ManaChangedHandle.IsValid() && OldManaAttr.IsValid())           { OldASC->GetGameplayAttributeValueChangeDelegate(OldManaAttr).Remove(ManaChangedHandle); }
        if (MaxManaChangedHandle.IsValid() && OldMaxManaAttr.IsValid())     { OldASC->GetGameplayAttributeValueChangeDelegate(OldMaxManaAttr).Remove(MaxManaChangedHandle); }
        if (XPChangedHandle.IsValid() && OldXPAttr.IsValid())               { OldASC->GetGameplayAttributeValueChangeDelegate(OldXPAttr).Remove(XPChangedHandle); }
        if (MaxXPChangedHandle.IsValid() && OldXPMaxAttr.IsValid())         { OldASC->GetGameplayAttributeValueChangeDelegate(OldXPMaxAttr).Remove(MaxXPChangedHandle); }
        if (LevelChangedHandle.IsValid() && OldLevelAttr.IsValid())         { OldASC->GetGameplayAttributeValueChangeDelegate(OldLevelAttr).Remove(LevelChangedHandle); }
        if (HPRegenChangedHandle.IsValid() && OldHPRegenAttr.IsValid())     { OldASC->GetGameplayAttributeValueChangeDelegate(OldHPRegenAttr).Remove(HPRegenChangedHandle); }
        if (ManaRegenChangedHandle.IsValid() && OldManaRegenAttr.IsValid()) { OldASC->GetGameplayAttributeValueChangeDelegate(OldManaRegenAttr).Remove(ManaRegenChangedHandle); }
    }

    HealthChangedHandle.Reset();
    MaxHealthChangedHandle.Reset();
    ManaChangedHandle.Reset();
    MaxManaChangedHandle.Reset();
    XPChangedHandle.Reset();
    MaxXPChangedHandle.Reset();
    LevelChangedHandle.Reset();
    HPRegenChangedHandle.Reset();
    ManaRegenChangedHandle.Reset();
    ResyncAccumulator = 0.f;
    bHasObservedHealthUpdate = false;
    bHasObservedManaUpdate = false;

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

    // Bind delegates for current ASC.
    if (ASC.IsValid())
    {
        if (HealthAttr.IsValid())    { HealthChangedHandle    = ASC->GetGameplayAttributeValueChangeDelegate(HealthAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnHealthChanged); }
        if (MaxHealthAttr.IsValid()) { MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(MaxHealthAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxHealthChanged); }
        if (ManaAttr.IsValid())      { ManaChangedHandle      = ASC->GetGameplayAttributeValueChangeDelegate(ManaAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnManaChanged); }
        if (MaxManaAttr.IsValid())   { MaxManaChangedHandle   = ASC->GetGameplayAttributeValueChangeDelegate(MaxManaAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxManaChanged); }
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
    ScheduleDelayedInitialSync();
}

void UW_AeyerjiStatusBar::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Start from a deterministic full state before ASC binding/replication catches up.
    HealthTarget = HealthMain = HealthGhost = 1.f;
    ManaTarget = ManaMain = ManaGhost = 1.f;
    XPTarget = XPMain = XPGhost = 1.f;
    HealthGhostHold = 0.f;
    ManaGhostHold = 0.f;
    XPGhostHold = 0.f;
    HealFlash = 0.f;
    DmgFlash = 0.f;
    ResyncAccumulator = 0.f;
    bHasObservedHealthUpdate = false;
    bHasObservedManaUpdate = false;

    if (HealthBar)       HealthBar->SetPercent(HealthMain);
    if (HealthBar_Ghost) HealthBar_Ghost->SetPercent(HealthGhost);
    if (ManaBar)         ManaBar->SetPercent(ManaMain);
    if (ManaBar_Ghost)   ManaBar_Ghost->SetPercent(ManaGhost);
    if (XPBar)           XPBar->SetPercent(XPMain);
    if (XPBar_Ghost)     XPBar_Ghost->SetPercent(XPGhost);
    if (ImgHealFlash)    ImgHealFlash->SetRenderOpacity(0.f);
    if (ImgDamageFlash)  ImgDamageFlash->SetRenderOpacity(0.f);

    UpdateColors();
    UpdateResourceVisibility();
    UpdateHPValueLabels();
    UpdateManaValueLabels();
    UpdateXPLabel();
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
        if (XPAttr.IsValid())    { XPChangedHandle    = ASC->GetGameplayAttributeValueChangeDelegate(XPAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnXPChanged); }
        if (XPMaxAttr.IsValid()) { MaxXPChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(XPMaxAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnMaxXPChanged); }
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
        if (LevelAttr.IsValid())
        {
            LevelChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(LevelAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnLevelChanged);
        }
    }
    UpdateLevelLabel();
}

void UW_AeyerjiStatusBar::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(InitialDelayedSyncTimer);
    }

    if (ASC.IsValid())
    {
        if (HealthChangedHandle.IsValid() && HealthAttr.IsValid())       ASC->GetGameplayAttributeValueChangeDelegate(HealthAttr).Remove(HealthChangedHandle);
        if (MaxHealthChangedHandle.IsValid() && MaxHealthAttr.IsValid()) ASC->GetGameplayAttributeValueChangeDelegate(MaxHealthAttr).Remove(MaxHealthChangedHandle);
        if (ManaChangedHandle.IsValid() && ManaAttr.IsValid())           ASC->GetGameplayAttributeValueChangeDelegate(ManaAttr).Remove(ManaChangedHandle);
        if (MaxManaChangedHandle.IsValid() && MaxManaAttr.IsValid())     ASC->GetGameplayAttributeValueChangeDelegate(MaxManaAttr).Remove(MaxManaChangedHandle);
        if (XPChangedHandle.IsValid() && XPAttr.IsValid())               ASC->GetGameplayAttributeValueChangeDelegate(XPAttr).Remove(XPChangedHandle);
        if (MaxXPChangedHandle.IsValid() && XPMaxAttr.IsValid())         ASC->GetGameplayAttributeValueChangeDelegate(XPMaxAttr).Remove(MaxXPChangedHandle);
        if (LevelChangedHandle.IsValid() && LevelAttr.IsValid())         ASC->GetGameplayAttributeValueChangeDelegate(LevelAttr).Remove(LevelChangedHandle);
        if (HPRegenChangedHandle.IsValid() && HPRegenAttr.IsValid())     ASC->GetGameplayAttributeValueChangeDelegate(HPRegenAttr).Remove(HPRegenChangedHandle);
        if (ManaRegenChangedHandle.IsValid() && ManaRegenAttr.IsValid()) ASC->GetGameplayAttributeValueChangeDelegate(ManaRegenAttr).Remove(ManaRegenChangedHandle);
    }
    ResyncAccumulator = 0.f;
    Super::NativeDestruct();
}

void UW_AeyerjiStatusBar::OnHealthChanged(const FOnAttributeChangeData& /*Data*/)
{
    bHasObservedHealthUpdate = true;
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
    bHasObservedManaUpdate = true;
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
        UpdateHealthChunkLayout(0.f);
        return;
    }

	const float CurHP = HealthAttr.IsValid() ? ASC->GetNumericAttribute(HealthAttr) : 0.f;
	const float MaxHP = MaxHealthAttr.IsValid() ? ASC->GetNumericAttribute(MaxHealthAttr) : 0.f;
	const float CurMP = ManaAttr.IsValid() ? ASC->GetNumericAttribute(ManaAttr) : 0.f;
	const float MaxMP = MaxManaAttr.IsValid() ? ASC->GetNumericAttribute(MaxManaAttr) : 0.f;
    const bool bHasXP  = XPAttr.IsValid() && XPMaxAttr.IsValid();
    const float CurXP  = bHasXP ? ASC->GetNumericAttribute(XPAttr)    : 0.f;
    const float MaxXP  = bHasXP ? ASC->GetNumericAttribute(XPMaxAttr) : 1.f;

    HealthTarget = FMath::Clamp(SafeDiv(GetDisplayedHealthValue(CurHP, MaxHP), MaxHP), 0.f, 1.f);
    ManaTarget   = FMath::Clamp(SafeDiv(GetDisplayedManaValue(CurMP, MaxMP), MaxMP), 0.f, 1.f);
    XPTarget     = FMath::Clamp(SafeDiv(CurXP, MaxXP), 0.f, 1.f);
    UpdateHealthChunkLayout(MaxHP);
    UpdateXPLabel();
    UpdateHPValueLabels();
    UpdateManaValueLabels();
    UpdateRegenLabels();
}

float UW_AeyerjiStatusBar::SelectNiceHealthPerChunk(
    const float MaxHealth,
    const int32 TargetCount,
    const int32 MinPreferredCount,
    const int32 MaxPreferredCount,
    const int32 HardMaxCount,
    int32& OutChunkCount)
{
    OutChunkCount = 0;
    if (!FMath::IsFinite(MaxHealth) || MaxHealth <= KINDA_SMALL_NUMBER)
    {
        return 0.f;
    }

    // Extra intermediate values close the large 2.5-to-5 and 5-to-10 gaps while
    // retaining the same clean, quickly readable decimal vocabulary.
    static constexpr float NiceMultipliers[] =
    {
        1.f, 1.25f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 5.f, 7.5f, 10.f
    };

    const int32 SafeHardMax = FMath::Max(1, HardMaxCount);
    const int32 SafeTarget = FMath::Clamp(TargetCount, 1, SafeHardMax);
    const int32 SafeMinPreferred = FMath::Clamp(MinPreferredCount, 1, SafeHardMax);
    const int32 SafeMaxPreferred = FMath::Clamp(
        MaxPreferredCount,
        SafeMinPreferred,
        SafeHardMax);
    const float DesiredChunkHP = MaxHealth / static_cast<float>(SafeTarget);
    const float Magnitude = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, DesiredChunkHP)));
    const float NormalizedDesired = DesiredChunkHP / Magnitude;

    int32 UpwardIndex = UE_ARRAY_COUNT(NiceMultipliers) - 1;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(NiceMultipliers); ++Index)
    {
        if (NiceMultipliers[Index] + UE_KINDA_SMALL_NUMBER >= NormalizedDesired)
        {
            UpwardIndex = Index;
            break;
        }
    }

    float SelectedChunkHP = NiceMultipliers[UpwardIndex] * Magnitude;
    int32 SelectedCount = FMath::CeilToInt((MaxHealth / SelectedChunkHP) - UE_KINDA_SMALL_NUMBER);

    // Rounding upward is the default. If it undershoots the preferred visual density,
    // use the adjacent lower clean value when that restores the requested 10-14 range.
    if (SelectedCount < SafeMinPreferred)
    {
        const float LowerChunkHP = UpwardIndex > 0
            ? NiceMultipliers[UpwardIndex - 1] * Magnitude
            : NiceMultipliers[UE_ARRAY_COUNT(NiceMultipliers) - 2] * (Magnitude * 0.1f);
        const int32 LowerCount = FMath::CeilToInt((MaxHealth / LowerChunkHP) - UE_KINDA_SMALL_NUMBER);
        if (LowerCount >= SafeMinPreferred
            && LowerCount <= SafeMaxPreferred
            && LowerCount <= SafeHardMax)
        {
            SelectedChunkHP = LowerChunkHP;
            SelectedCount = LowerCount;
        }
    }

    // This is normally already satisfied because selected chunk HP starts at MaxHP/target.
    // Keep the hard limit authoritative if designer settings are configured unusually.
    while (SelectedCount > SafeHardMax)
    {
        SelectedChunkHP *= 2.f;
        SelectedCount = FMath::CeilToInt((MaxHealth / SelectedChunkHP) - UE_KINDA_SMALL_NUMBER);
    }

    OutChunkCount = FMath::Clamp(SelectedCount, 1, SafeHardMax);
    return SelectedChunkHP;
}

void UW_AeyerjiStatusBar::UpdateHealthChunkLayout(const float MaxHealth)
{
    const float SafeMaxHealth = FMath::IsFinite(MaxHealth) ? FMath::Max(0.f, MaxHealth) : 0.f;
    const bool bIsTagEligible = IsHealthChunkTagEligible();
    if (FMath::IsNearlyEqual(ChunkLayoutMaxHealth, SafeMaxHealth)
        && bHealthChunkTagEligible == bIsTagEligible)
    {
        return;
    }

    ChunkLayoutMaxHealth = SafeMaxHealth;
    bHealthChunkTagEligible = bIsTagEligible;
    if (!bHealthChunkDivisionsEnabled
        || !bHealthChunkTagEligible
        || SafeMaxHealth <= KINDA_SMALL_NUMBER)
    {
        HealthChunkCount = 0;
        HealthPerChunk = 0.f;
        HideHealthChunkSeparatorWidgets();
        return;
    }

    HealthPerChunk = SelectNiceHealthPerChunk(
        SafeMaxHealth,
        TargetHealthChunkCount,
        MinPreferredHealthChunkCount,
        MaxPreferredHealthChunkCount,
        HardMaxHealthChunkCount,
        HealthChunkCount);
    RefreshHealthChunkSeparatorWidgets();
}

bool UW_AeyerjiStatusBar::IsHealthChunkTagEligible() const
{
    if (!bHealthChunkDivisionsEnabled)
    {
        return false;
    }

    if (!bRequireAnyHealthChunkEligibilityTag)
    {
        return true;
    }

    // An enabled requirement with an empty container deliberately matches nothing,
    // avoiding an accidental return to segmented trash-enemy bars.
    return ASC.IsValid()
        && !HealthChunkEligibilityTags.IsEmpty()
        && ASC->HasAnyMatchingGameplayTags(HealthChunkEligibilityTags);
}

void UW_AeyerjiStatusBar::HideHealthChunkSeparatorWidgets()
{
    for (UBorder* Separator : HealthChunkSeparatorWidgets)
    {
        if (Separator)
        {
            Separator->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void UW_AeyerjiStatusBar::RefreshHealthChunkSeparatorWidgets()
{
    if (!bHealthChunkDivisionsEnabled
        || !bHealthChunkTagEligible
        || HealthChunkCount <= 1
        || HealthPerChunk <= KINDA_SMALL_NUMBER
        || ChunkLayoutMaxHealth <= KINDA_SMALL_NUMBER
        || !HealthBar
        || !WidgetTree)
    {
        HideHealthChunkSeparatorWidgets();
        return;
    }

    UCanvasPanelSlot* HealthCanvasSlot = Cast<UCanvasPanelSlot>(HealthBar->Slot);
    UCanvasPanel* HealthCanvas = HealthCanvasSlot
        ? Cast<UCanvasPanel>(HealthBar->GetParent())
        : nullptr;
    if (!HealthCanvasSlot || !HealthCanvas)
    {
        HideHealthChunkSeparatorWidgets();
        return;
    }

    const FVector2D BarSize = HealthCanvasSlot->GetSize();
    const FVector2D BarPosition = HealthCanvasSlot->GetPosition();
    const FVector2D BarAlignment = HealthCanvasSlot->GetAlignment();
    if (BarSize.ContainsNaN()
        || BarPosition.ContainsNaN()
        || BarAlignment.ContainsNaN()
        || BarSize.X <= KINDA_SMALL_NUMBER
        || BarSize.Y <= KINDA_SMALL_NUMBER)
    {
        HideHealthChunkSeparatorWidgets();
        return;
    }

    const FVector2D BarTopLeft = BarPosition - (BarSize * BarAlignment);
    const float SeparatorHeight = FMath::Max(
        0.f,
        BarSize.Y - (HealthChunkSeparatorVerticalInset * 2.f));
    if (SeparatorHeight <= KINDA_SMALL_NUMBER)
    {
        HideHealthChunkSeparatorWidgets();
        return;
    }

    int32 SeparatorZOrder = HealthCanvasSlot->GetZOrder() + 1;
    if (HealthBar_Ghost)
    {
        if (const UCanvasPanelSlot* GhostCanvasSlot = Cast<UCanvasPanelSlot>(HealthBar_Ghost->Slot);
            GhostCanvasSlot && HealthBar_Ghost->GetParent() == HealthCanvas)
        {
            SeparatorZOrder = FMath::Max(SeparatorZOrder, GhostCanvasSlot->GetZOrder() + 1);
        }
    }

    int32 VisibleSeparatorCount = 0;
    for (int32 ChunkIndex = 1; ChunkIndex < HealthChunkCount; ++ChunkIndex)
    {
        const float BoundaryFraction =
            (HealthPerChunk * static_cast<float>(ChunkIndex)) / ChunkLayoutMaxHealth;
        if (BoundaryFraction >= 1.f - UE_KINDA_SMALL_NUMBER)
        {
            break;
        }

        UBorder* Separator = HealthChunkSeparatorWidgets.IsValidIndex(VisibleSeparatorCount)
            ? HealthChunkSeparatorWidgets[VisibleSeparatorCount]
            : nullptr;
        if (!Separator)
        {
            const FName SeparatorName(*FString::Printf(
                TEXT("NativeHealthChunkSeparator_%d"),
                VisibleSeparatorCount));
            Separator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), SeparatorName);
            if (!Separator)
            {
                continue;
            }

            const FSlateColorBrush SolidBrush(FLinearColor::White);
            Separator->SetBrush(SolidBrush);
            Separator->SetPadding(FMargin(0.f));
            if (HealthChunkSeparatorWidgets.IsValidIndex(VisibleSeparatorCount))
            {
                HealthChunkSeparatorWidgets[VisibleSeparatorCount] = Separator;
            }
            else
            {
                HealthChunkSeparatorWidgets.Add(Separator);
            }
        }

        UCanvasPanelSlot* SeparatorSlot = Cast<UCanvasPanelSlot>(Separator->Slot);
        if (Separator->GetParent() != HealthCanvas || !SeparatorSlot)
        {
            Separator->RemoveFromParent();
            SeparatorSlot = HealthCanvas->AddChildToCanvas(Separator);
        }
        if (!SeparatorSlot)
        {
            Separator->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }

        const FVector2D SeparatorPosition(
            BarTopLeft.X + (BarSize.X * BoundaryFraction) - (HealthChunkSeparatorThickness * 0.5f),
            BarTopLeft.Y + HealthChunkSeparatorVerticalInset);
        SeparatorSlot->SetAnchors(HealthCanvasSlot->GetAnchors());
        SeparatorSlot->SetAlignment(FVector2D::ZeroVector);
        SeparatorSlot->SetAutoSize(false);
        SeparatorSlot->SetPosition(SeparatorPosition);
        SeparatorSlot->SetSize(FVector2D(HealthChunkSeparatorThickness, SeparatorHeight));
        SeparatorSlot->SetZOrder(SeparatorZOrder);

        Separator->SetBrushColor(HealthChunkSeparatorColor);
        Separator->SetVisibility(ESlateVisibility::HitTestInvisible);
        ++VisibleSeparatorCount;
    }

    for (int32 Index = VisibleSeparatorCount; Index < HealthChunkSeparatorWidgets.Num(); ++Index)
    {
        if (UBorder* Separator = HealthChunkSeparatorWidgets[Index])
        {
            Separator->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

float UW_AeyerjiStatusBar::SafeDiv(float Numerator, float Denominator)
{
	Numerator = FiniteStatusValue(Numerator);
	Denominator = FiniteStatusValue(Denominator);
	const float Result = FMath::IsNearlyZero(Denominator) ? 1.f : Numerator / Denominator;
	return FiniteStatusValue(Result, 1.f);
}

void UW_AeyerjiStatusBar::NativeTick(const FGeometry& MyGeometry, float Dt)
{
    Super::NativeTick(MyGeometry, Dt);
	Dt = FMath::Clamp(FiniteStatusValue(Dt), 0.f, 1.f);

    if (ASC.IsValid())
    {
        // Always pull live values so late replication or missed initial delegates cannot leave stale bars on screen.
        RecalculateTargets();

		const float SafeResyncInterval = FMath::Clamp(
			FiniteStatusValue(PeriodicResyncInterval), 0.f, 60.f);
		if (SafeResyncInterval > 0.f)
        {
            ResyncAccumulator += Dt;
			if (ResyncAccumulator >= SafeResyncInterval)
            {
				ResyncAccumulator = FMath::Fmod(ResyncAccumulator, SafeResyncInterval);
                UpdateResourceVisibility();
                UpdateColors();
            }
        }
    }

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
	Target = FMath::Clamp(FiniteStatusValue(Target, 1.f), 0.f, 1.f);
	Main = FMath::Clamp(FiniteStatusValue(Main, Target), 0.f, 1.f);
	Ghost = FMath::Clamp(FiniteStatusValue(Ghost, Main), 0.f, 1.f);
	Hold = FMath::Clamp(FiniteStatusValue(Hold), 0.f, 60.f);
	const float FillSpeed = FMath::Clamp(FiniteStatusValue(
		Target > Main ? FillLerpSpeed_HealUp : FillLerpSpeed_DmgDown), 0.f, 1000.f);
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
				Ghost = UKismetMathLibrary::FInterpTo(Ghost, Target, Dt, FMath::Clamp(
					FiniteStatusValue(ChipLerpSpeedDown), 0.f, 1000.f));
            }
        }
        else
        {
            // heal/refund: ghost catches up more quickly upward
			Ghost = UKismetMathLibrary::FInterpTo(Ghost, Target, Dt, FMath::Clamp(
				FiniteStatusValue(ChipLerpSpeedDown), 0.f, 1000.f) * 1.5f);
        }
        GhostPB->SetPercent(Ghost);
    }
}

void UW_AeyerjiStatusBar::ApplyFX(float Dt)
{
    // Fade flashes
    if (ImgHealFlash && HealFlash > 0.f)
    {
		HealFlash = FMath::Clamp(HealFlash - FMath::Clamp(
			FiniteStatusValue(FlashFadePerSec), 0.f, 1000.f) * Dt, 0.f, 1.f);
        ImgHealFlash->SetRenderOpacity(HealFlash);
    }
    if (ImgDamageFlash && DmgFlash > 0.f)
    {
		DmgFlash = FMath::Clamp(DmgFlash - FMath::Clamp(
			FiniteStatusValue(FlashFadePerSec), 0.f, 1000.f) * Dt, 0.f, 1.f);
        ImgDamageFlash->SetRenderOpacity(DmgFlash);
    }
}

void UW_AeyerjiStatusBar::UpdateColors()
{
    if (!HealthBar) return;

    // Pulse color when low HP
    const float H = HealthMain;
	const bool bLow = H <= FMath::Clamp(FiniteStatusValue(LowHPThreshold, 0.25f), 0.f, 1.f);
    FLinearColor C = HealthColor_Normal;

    if (bLow)
    {
        // soft pulse between Low and Normal
		const UWorld* World = GetWorld();
		const float TimeSeconds = World ? World->GetTimeSeconds() : 0.f;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(FiniteStatusValue(TimeSeconds) * 6.0f);
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
	const float MaxRes = MaxManaAttr.IsValid() ? FiniteStatusValue(ASC->GetNumericAttribute(MaxManaAttr)) : 0.f;
	if (MaxRes > FMath::Clamp(FiniteStatusValue(MinResourceToShow, 0.5f), 0.f, MaxStatusDisplayValue))
    {
        return true;
    }

    // Optional: if any granted ability has the specified tag, treat as mana user
    if (ManaAbilityTag.IsValid())
    {
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
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
    if (!(XPAttr.IsValid() && XPMaxAttr.IsValid()))
    {
        XPText->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    if (LevelAttr.IsValid())
    {
		const int32 CurrentLevel = UAeyerjiDifficultySettings::FloatToGameplayLevel(ASC->GetNumericAttribute(LevelAttr));
        if (CurrentLevel >= UAeyerjiDifficultySettings::GetMaxGameplayLevel())
        {
            XPText->SetVisibility(ESlateVisibility::Collapsed);
            return;
        }
    }

	const int32 Cur = StatusDisplayInteger(ASC->GetNumericAttribute(XPAttr));
	const int32 Max = FMath::Max(1, StatusDisplayInteger(ASC->GetNumericAttribute(XPMaxAttr)));

    XPText->SetVisibility(ESlateVisibility::Visible);
	XPText->SetText(FormatStatusPair(
		TEXT("StatusXPFormat"), LOCTEXT("StatusXPFormatFallback", "{0}/{1} XP"), Cur, Max));
}

void UW_AeyerjiStatusBar::UpdateLevelLabel()
{
    if (!ASC.IsValid() || !LevelText) return;
    if (!LevelAttr.IsValid()) return;

	const int32 Lvl = UAeyerjiDifficultySettings::FloatToGameplayLevel(ASC->GetNumericAttribute(LevelAttr));
    LevelText->SetText(FText::AsNumber(Lvl));
}

void UW_AeyerjiStatusBar::UpdateHPValueLabels()
{
    if (!ASC.IsValid()) return;
    const float RawCurHP = ASC->GetNumericAttribute(HealthAttr);
    const float RawMaxHP = ASC->GetNumericAttribute(MaxHealthAttr);
	const int32 CurHP = StatusDisplayInteger(GetDisplayedHealthValue(RawCurHP, RawMaxHP));
	const int32 MaxHP = StatusDisplayInteger(RawMaxHP);
    if (HPValueText)
    {
		HPValueText->SetText(FormatStatusPair(
			TEXT("StatusHPFormat"), LOCTEXT("StatusHPFormatFallback", "{0}/{1} HP"), CurHP, MaxHP));
    }
	if (HPMaxValueText) HPMaxValueText->SetText(FText::AsNumber(MaxHP));
}

void UW_AeyerjiStatusBar::UpdateManaValueLabels()
{
    if (!ASC.IsValid()) return;
    const float RawCurMana = ASC->GetNumericAttribute(ManaAttr);
    const float RawMaxMana = ASC->GetNumericAttribute(MaxManaAttr);
	const int32 CurMana = StatusDisplayInteger(GetDisplayedManaValue(RawCurMana, RawMaxMana));
	const int32 MaxMana = StatusDisplayInteger(RawMaxMana);
    if (ManaValueText)
    {
		ManaValueText->SetText(FormatStatusPair(
			TEXT("StatusManaFormat"), LOCTEXT("StatusManaFormatFallback", "{0}/{1} Mana"), CurMana, MaxMana));
    }
	if (ManaMaxValueText) ManaMaxValueText->SetText(FText::AsNumber(MaxMana));
}

void UW_AeyerjiStatusBar::UpdateRegenLabels()
{
    if (!ASC.IsValid()) return;
    if (HPRegenText && HPRegenAttr.IsValid())
    {
		const float Regen = FMath::Clamp(FiniteStatusValue(ASC->GetNumericAttribute(HPRegenAttr)), 0.f, MaxStatusDisplayValue);
		const FText Template = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatusRegenFormat"));
		HPRegenText->SetText(FText::Format(
			Template.IsEmpty() ? LOCTEXT("StatusRegenFormatFallback", "+{0}") : Template,
			FText::AsNumber(Regen)));
    }
    if (ManaRegenText && ManaRegenAttr.IsValid())
    {
		const float Regen = FMath::Clamp(FiniteStatusValue(ASC->GetNumericAttribute(ManaRegenAttr)), 0.f, MaxStatusDisplayValue);
		const FText Template = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatusRegenFormat"));
		ManaRegenText->SetText(FText::Format(
			Template.IsEmpty() ? LOCTEXT("StatusRegenFormatFallback", "+{0}") : Template,
			FText::AsNumber(Regen)));
    }
}

float UW_AeyerjiStatusBar::GetDisplayedHealthValue(float CurrentHP, float MaxHP) const
{
    if (!ASC.IsValid())
    {
        return CurrentHP;
    }

    if (bHasObservedHealthUpdate || FMath::IsNearlyEqual(CurrentHP, MaxHP) || FMath::IsNearlyEqual(MaxHP, 100.f))
    {
        return CurrentHP;
    }

    if (!FMath::IsNearlyEqual(CurrentHP, 100.f))
    {
        return CurrentHP;
    }
    // Treat the constructor default HP as stale until the first real health update arrives.
    return MaxHP;
}

float UW_AeyerjiStatusBar::GetDisplayedManaValue(float CurrentMana, float MaxMana) const
{
    if (!ASC.IsValid())
    {
        return CurrentMana;
    }

    if (bHasObservedManaUpdate || FMath::IsNearlyEqual(CurrentMana, MaxMana))
    {
        return CurrentMana;
    }

    if (FMath::IsNearlyEqual(CurrentMana, 0.f) && MaxMana > 0.f)
    {
        // Many startup paths leave mana at the attribute-set default (0) until a later update.
        return MaxMana;
    }

    if (FMath::IsNearlyEqual(CurrentMana, 100.f) && !FMath::IsNearlyEqual(MaxMana, 100.f))
    {
        // Mirror the HP stale-default guard for mana setups that initialize current mana to 100.
        return MaxMana;
    }

    return CurrentMana;
}

void UW_AeyerjiStatusBar::ScheduleDelayedInitialSync()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    World->GetTimerManager().ClearTimer(InitialDelayedSyncTimer);

	const float SafeDelay = FMath::Clamp(FiniteStatusValue(InitialDelayedSyncSeconds), 0.f, 60.f);
	if (SafeDelay <= 0.f)
    {
        RunDelayedInitialSync();
        return;
    }

    World->GetTimerManager().SetTimer(
        InitialDelayedSyncTimer,
        this,
        &UW_AeyerjiStatusBar::RunDelayedInitialSync,
		SafeDelay,
        false);
}

void UW_AeyerjiStatusBar::RunDelayedInitialSync()
{
    if (!ASC.IsValid())
    {
        return;
    }

    RecalculateTargets();
    HealthMain = HealthGhost = HealthTarget;
    ManaMain = ManaGhost = ManaTarget;
    XPMain = XPGhost = XPTarget;

    if (HealthBar)       HealthBar->SetPercent(HealthMain);
    if (HealthBar_Ghost) HealthBar_Ghost->SetPercent(HealthGhost);
    if (ManaBar)         ManaBar->SetPercent(ManaMain);
    if (ManaBar_Ghost)   ManaBar_Ghost->SetPercent(ManaGhost);
    if (XPBar)           XPBar->SetPercent(XPMain);
    if (XPBar_Ghost)     XPBar_Ghost->SetPercent(XPGhost);

    UpdateResourceVisibility();
    UpdateColors();
    UpdateHPValueLabels();
    UpdateManaValueLabels();
    UpdateXPLabel();
}

void UW_AeyerjiStatusBar::BindRegenAttributes(UAbilitySystemComponent* InASC,
                                              FGameplayAttribute InHPRegen,
                                              FGameplayAttribute InManaRegen)
{
	if (InASC && (!IsValid(InASC) || (GetWorld() && InASC->GetWorld() != GetWorld())))
	{
		return;
	}
	if (ASC.IsValid() && InASC != ASC.Get())
	{
		return;
	}

    UAbilitySystemComponent* OldASC = ASC.Get();
    const FGameplayAttribute OldHPRegenAttr = HPRegenAttr;
    const FGameplayAttribute OldManaRegenAttr = ManaRegenAttr;

    if (OldASC)
    {
        if (HPRegenChangedHandle.IsValid() && OldHPRegenAttr.IsValid())
        {
            OldASC->GetGameplayAttributeValueChangeDelegate(OldHPRegenAttr).Remove(HPRegenChangedHandle);
        }
        if (ManaRegenChangedHandle.IsValid() && OldManaRegenAttr.IsValid())
        {
            OldASC->GetGameplayAttributeValueChangeDelegate(OldManaRegenAttr).Remove(ManaRegenChangedHandle);
        }
    }

    HPRegenChangedHandle.Reset();
    ManaRegenChangedHandle.Reset();

    ASC = InASC;
    HPRegenAttr  = InHPRegen;
    ManaRegenAttr= InManaRegen;

    if (ASC.IsValid())
    {
        if (HPRegenAttr.IsValid())
        {
            HPRegenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(HPRegenAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnHPRegenChanged);
        }
        if (ManaRegenAttr.IsValid())
        {
            ManaRegenChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(ManaRegenAttr).AddUObject(this, &UW_AeyerjiStatusBar::OnManaRegenChanged);
        }
    }

    UpdateRegenLabels();
}

#undef LOCTEXT_NAMESPACE
