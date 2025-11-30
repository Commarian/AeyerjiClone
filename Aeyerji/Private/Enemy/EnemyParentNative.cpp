// EnemyParentNative.cpp
#include "Enemy/EnemyParentNative.h"
#include "AbilitySystemGlobals.h"
#include "Logging/AeyerjiLog.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiRewardAttributeSet.h"
#include "Components/OutlineHighlightComponent.h"
#include "Components/PrimitiveComponent.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif
AEnemyParentNative::AEnemyParentNative()
{
	PrimaryActorTick.bCanEverTick = false;          // Creeps usually tick via AI only
	/* Network */
	bReplicates = true;

	OutlineHighlight = CreateDefaultSubobject<UOutlineHighlightComponent>(TEXT("OutlineHighlight"));
	if (OutlineHighlight)
	{
		OutlineHighlight->bAffectAllPrimitivesIfNoExplicitTargets = false;
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // interact traces
		MeshComp->SetGenerateOverlapEvents(false);
	}
}

void AEnemyParentNative::PostLoad()
{
	Super::PostLoad();

	if (HighlightChannel == 20 && HighlightStencilValue_DEPRECATED != 0)
	{
		HighlightChannel = FMath::Clamp(HighlightStencilValue_DEPRECATED, 0, 255);
		HighlightStencilValue_DEPRECATED = 0;
	}
}

#if WITH_EDITOR
void AEnemyParentNative::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AEnemyParentNative, HighlightChannel))
	{
		HighlightChannel = FMath::Clamp(HighlightChannel, 0, 255);

		// Refresh targets so the channel update propagates to the stencil map immediately in editor.
		RefreshEnemyHighlightTargets();
	}
}
#endif
/* ------------------------------------------------------------------ */

void AEnemyParentNative::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshEnemyHighlightTargets();
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::BeginPlay()
{
	Super::BeginPlay();
	InitAbilityActorInfo();
	GiveStartupAbilitiesAndEffects();
	RefreshEnemyHighlightTargets();
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->OnBeginCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshBeginCursorOver);
		MeshComp->OnEndCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshEndCursorOver);
	}
	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (IsValid(Primitive))
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->OnBeginCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshBeginCursorOver);
			Primitive->OnEndCursorOver.AddDynamic(this, &AEnemyParentNative::HandleMeshEndCursorOver);
		}
	}
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::NotifyActorBeginCursorOver()
{
	Super::NotifyActorBeginCursorOver();
	++HoverHighlightRefCount;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::NotifyActorEndCursorOver()
{
	Super::NotifyActorEndCursorOver();
	HoverHighlightRefCount = FMath::Max(0, HoverHighlightRefCount - 1);
	UpdateEnemyHighlightState();
}

/* ------------------------------------------------------------------ */

void AEnemyParentNative::OnDeath_Implementation()
{
	// Default native behaviour: stop AI, then destroy the actor.
	// Blueprints can override and call the parent, adding animations/VFX etc.
	OnEnemyDied.Broadcast(this);
	DetachFromControllerPendingDestroy();
	SetLifeSpan(5.0f);          // give replication time before GC
}

void AEnemyParentNative::InitAbilityActorInfo()
{
	if (!AbilitySystemAeyerji)
	{
		UE_LOG(LogTemp, Warning, TEXT("APlayerParentNative::InitAbilityActorInfo AbilitySystemAeyerji is null"));
		return;
	}
	if (bASCInitialised)
	{
		UE_LOG(LogTemp, Warning, TEXT("APlayerParentNative::InitAbilityActorInfo() bASCInitialised already true"));
		return;
	}
    AbilitySystemAeyerji->InitAbilityActorInfo(this, this);
    // Ensure the AttributeSet instance exists so AI can read/write attributes.
    if (!AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>())
    {
        UAeyerjiAttributeSet* NewSet = NewObject<UAeyerjiAttributeSet>(this);
        AbilitySystemAeyerji->AddAttributeSetSubobject(NewSet);
    }

    // Ensure Reward AttributeSet exists so we can read XPRewardBase on death.
    if (!AbilitySystemAeyerji->GetSet<UAeyerjiRewardAttributeSet>())
    {
        UAeyerjiRewardAttributeSet* RewardSet = NewObject<UAeyerjiRewardAttributeSet>(this);
        AbilitySystemAeyerji->AddAttributeSetSubobject(RewardSet);
    }

    // Hook death delegate (server only)
    BindDeathEvent();
	
	if (IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(this))
	{
		TeamAgent->SetGenericTeamId(TeamId);   // 0 = players
		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Fix this spam?!"));
	}

	if (HasAuthority())
	{
		AJ_LOG(this, TEXT("HandleASCReady - Adding startup abilities (server)"));
		AddStartupAbilities();
	}
	
	// OPTIONAL: Set tag relationship tables, etc.
}

void AEnemyParentNative::GiveStartupAbilitiesAndEffects()
{
	if (bStartupGiven || !AbilitySystemAeyerji || !HasAuthority())
	{
		return;        // Only once, server side
	}

	/* ---------- Abilities ---------- */
	for (const TSubclassOf<UGameplayAbility>& AbilityClass : StartupAbilities)
	{
		if (AbilityClass)
		{
			AbilitySystemAeyerji->GiveAbility(FGameplayAbilitySpec(AbilityClass, /*Level=*/1, INDEX_NONE, this));
		}
	}

	/* ---------- Passive Effects ---------- */
	for (const TSubclassOf<UGameplayEffect>& GEClass : StartupEffects)
	{
		if (GEClass)
		{
			const FGameplayEffectContextHandle Ctx = AbilitySystemAeyerji->MakeEffectContext();
			FGameplayEffectSpecHandle          Spec = AbilitySystemAeyerji->MakeOutgoingSpec(GEClass, /*Level=*/1.f, Ctx);

			if (Spec.IsValid())
			{
				AbilitySystemAeyerji->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	bStartupGiven = true;
}

void AEnemyParentNative::SetEnemyHighlighted(bool bInHighlighted)
{
	if (!OutlineHighlight)
	{
		return;
	}

	if (bEnemyHighlighted == bInHighlighted)
	{
		return;
	}

	bEnemyHighlighted = bInHighlighted;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::RefreshEnemyHighlightTargets()
{
	if (!OutlineHighlight)
	{
		return;
	}

	const bool bWasHighlighted = bEnemyHighlighted;

	OutlineHighlight->SetHighlighted(false);
	OutlineHighlight->ExplicitTargets.Reset();

	ConfigureEnemyOutlineComponent();

	const int32 ChannelIndex = FMath::Clamp(HighlightChannel, 0, 255);

	// When using palette-defined channels (0-7) keep the default mapping that ships with the component.
	// For any higher channel we ensure a direct identity mapping so the custom LUT row is used.
	if (ChannelIndex >= 8)
	{
		OutlineHighlight->RarityIndexToStencil.FindOrAdd(ChannelIndex) = ChannelIndex;
	}
	else
	{
		// Remove overrides that might have been injected previously so defaults (0->1, 1->2, ...) remain intact.
		if (int32* Existing = OutlineHighlight->RarityIndexToStencil.Find(ChannelIndex))
		{
			const int32 DefaultValue = ChannelIndex + 1;
			if (*Existing == ChannelIndex)
			{
				OutlineHighlight->RarityIndexToStencil.Remove(ChannelIndex);
			}
			else if (*Existing != DefaultValue)
			{
				// Leave user customization in place if they intentionally mapped to a different stencil.
			}
		}
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		OutlineHighlight->ExplicitTargets.Add(MeshComp);
	}

	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (IsValid(Primitive))
		{
			OutlineHighlight->ExplicitTargets.Add(Primitive);
		}
	}

	OutlineHighlight->InitializeFromRarityIndex(ChannelIndex);
	bEnemyHighlighted = bWasHighlighted;
	UpdateEnemyHighlightState();
}

bool AEnemyParentNative::IsHoverTargetComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return true;
	}

	if (Component == GetMesh())
	{
		return true;
	}

	for (UPrimitiveComponent* Primitive : AdditionalHighlightPrimitives)
	{
		if (Primitive && Component == Primitive)
		{
			return true;
		}
	}

	return Component->GetOwner() == this;
}

void AEnemyParentNative::UpdateEnemyHighlightState()
{
	if (!OutlineHighlight)
	{
		return;
	}

	const bool bShouldHighlight = bEnemyHighlighted || HoverHighlightRefCount > 0 || bHighlightOnSpawn;
	OutlineHighlight->SetHighlighted(bShouldHighlight);
}

void AEnemyParentNative::HandleMeshBeginCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, VeryVerbose, TEXT("Enemy %s hover begin on %s"), *GetName(), *GetNameSafe(TouchedComponent));
	++HoverHighlightRefCount;
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::HandleMeshEndCursorOver(UPrimitiveComponent* TouchedComponent)
{
	UE_LOG(LogTemp, VeryVerbose, TEXT("Enemy %s hover end on %s"), *GetName(), *GetNameSafe(TouchedComponent));
	HoverHighlightRefCount = FMath::Max(0, HoverHighlightRefCount - 1);
	UpdateEnemyHighlightState();
}

void AEnemyParentNative::ConfigureEnemyOutlineComponent()
{
	if (!OutlineHighlight)
	{
		return;
	}
}
