// OutlineHighlightComponent.cpp

#include "Components/OutlineHighlightComponent.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

// Sets up initial stencil mapping so designers immediately get palette-backed colors.
UOutlineHighlightComponent::UOutlineHighlightComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Sensible defaults aligned with the project's loot rarities.
	RarityIndexToStencil.Reserve(8);
	RarityIndexToStencil.Add(0, 1); // Common
	RarityIndexToStencil.Add(1, 2); // Uncommon
	RarityIndexToStencil.Add(2, 3); // Rare
	RarityIndexToStencil.Add(3, 4); // Epic
	RarityIndexToStencil.Add(4, 5); // Pure
	RarityIndexToStencil.Add(5, 6); // Legendary
	RarityIndexToStencil.Add(6, 7); // Perfect Legendary
	RarityIndexToStencil.Add(7, 8); // Celestial
}

// Nothing to do on begin play beyond the default component behavior, but kept for parity.
void UOutlineHighlightComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Resolve the stencil to write by consulting overrides, falling back to the raw rarity index.
int32 UOutlineHighlightComponent::ResolveStencilForRarity(int32 RarityIndex) const
{
	if (const int32* Found = RarityIndexToStencil.Find(RarityIndex))
	{
		return FMath::Clamp(*Found, 0, 255);
	}

	return FMath::Clamp(RarityIndex, 0, 255);
}

// Build a list of primitives that should receive custom depth writes.
void UOutlineHighlightComponent::GatherTargets(TArray<UPrimitiveComponent*>& OutTargets) const
{
	OutTargets.Reset();

	for (UPrimitiveComponent* Comp : ExplicitTargets)
	{
		if (IsValid(Comp))
		{
			OutTargets.Add(Comp);
		}
	}

	if (OutTargets.Num() == 0 && bAffectAllPrimitivesIfNoExplicitTargets)
	{
		if (AActor* Owner = GetOwner())
		{
			TArray<UActorComponent*> OwnerComponents;
			Owner->GetComponents(OwnerComponents);

			for (UActorComponent* Component : OwnerComponents)
			{
				if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Component))
				{
					if (IsValid(Prim))
					{
						OutTargets.Add(Prim);
					}
				}
			}
		}
	}
}

// Utility that applies a lambda to every valid primitive we currently manage.
void UOutlineHighlightComponent::ApplyToTargets(TFunctionRef<void(UPrimitiveComponent*)> Fn) const
{
	TArray<UPrimitiveComponent*> Targets;
	GatherTargets(Targets);

	for (UPrimitiveComponent* Prim : Targets)
	{
		if (IsValid(Prim))
		{
			Fn(Prim);
		}
	}
}

// Clamp and apply the stencil value to every target immediately.
void UOutlineHighlightComponent::ApplyStencilValue(int32 StencilValue)
{
	CachedStencil = FMath::Clamp(StencilValue, 0, 255);

	ApplyToTargets([this](UPrimitiveComponent* Prim)
	{
		Prim->SetCustomDepthStencilWriteMask(WriteMask);
		Prim->SetCustomDepthStencilValue(CachedStencil);
		Prim->MarkRenderStateDirty();
	});
}

// Change the active stencil value based on a logical rarity/index.
void UOutlineHighlightComponent::InitializeFromRarityIndex(int32 RarityIndex)
{
	const int32 Stencil = ResolveStencilForRarity(RarityIndex);
	ApplyStencilValue(Stencil);

	if (bCurrentlyHighlighted)
	{
		SetHighlighted(true);
	}
}

// Toggle the render custom depth flag, ensuring the stencil/write mask are valid when enabling.
void UOutlineHighlightComponent::SetHighlighted(bool bEnable)
{
	bCurrentlyHighlighted = bEnable;

	ApplyToTargets([this, bEnable](UPrimitiveComponent* Prim)
	{
		if (bEnable)
		{
			Prim->SetCustomDepthStencilWriteMask(WriteMask);
			Prim->SetCustomDepthStencilValue(CachedStencil);
		}

		Prim->SetRenderCustomDepth(bEnable);
		Prim->MarkRenderStateDirty();
	});
}

void UOutlineHighlightComponent::PulseHighlight(float Duration, float FadeTime, int32 OverrideStencil)
{
	const float SafeDuration = FMath::Max(Duration, 0.f);
	const float SafeFadeTime = FMath::Max(FadeTime, 0.f);

	if ((SafeDuration + SafeFadeTime) <= 0.f)
	{
		return;
	}

	if (OverrideStencil >= 0 && OverrideStencil <= 255)
	{
		ApplyStencilValue(OverrideStencil);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HighlightPulseHandle);
		bHighlightedBeforePulse = bCurrentlyHighlighted;
		bPulseActive = true;
		SetHighlighted(true);

		const float TotalTime = SafeDuration + SafeFadeTime;
		World->GetTimerManager().SetTimer(
			HighlightPulseHandle,
			this,
			&UOutlineHighlightComponent::HandleHighlightPulseFinished,
			TotalTime,
			false);
	}
}

void UOutlineHighlightComponent::HandleHighlightPulseFinished()
{
	if (!bPulseActive)
	{
		return;
	}

	bPulseActive = false;
	SetHighlighted(bHighlightedBeforePulse);
}
