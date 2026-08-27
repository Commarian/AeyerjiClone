// ===============================
// File: AeyerjiTargetingManager.cpp
// ===============================
#include "Abilities/AeyerjiTargetingManager.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/Blink/GABlink.h"
#include "Abilities/GameplayAbility.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "AeyerjiGameplayTags.h"
#include "CharacterStatsLibrary.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Logging/AeyerjiLog.h"

namespace
{
	bool TargetingVectorIsFinite(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	float TargetingFiniteOrDefault(const float Value, const float DefaultValue)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}
}

void UAeyerjiTargetingManager::Initialize(AAeyerjiPlayerController* InOwner, const FAeyerjiTargetingTunables& InTunables)
{
	// A possession/world transfer must stop the timer owned by the previous controller first.
	if (OwnerPC.Get() != InOwner)
	{
		ClearTargeting();
	}
	OwnerPC = InOwner;
	Tunables = InTunables;
	Tunables.RangePreviewTickRate = FMath::Max(0.f, TargetingFiniteOrDefault(Tunables.RangePreviewTickRate, 0.05f));
	Tunables.RangePreviewDrawLife = FMath::Max(0.f, TargetingFiniteOrDefault(Tunables.RangePreviewDrawLife, 0.06f));
	Tunables.RangePreviewThickness = FMath::Max(0.f, TargetingFiniteOrDefault(Tunables.RangePreviewThickness, 2.5f));
}

void UAeyerjiTargetingManager::BeginDestroy()
{
	// Ensure timers are cleared before teardown to avoid ticking after owner destruction.
	StopRangePreview();
	Super::BeginDestroy();
}

void UAeyerjiTargetingManager::SetHooks(FAeyerjiTargetingHooks InHooks)
{
	// Owner wires in the functions that actually perform traces/RPCs.
	Hooks = MoveTemp(InHooks);
}

void UAeyerjiTargetingManager::BeginTargeting(const FAeyerjiAbilitySlot& Slot)
{
	// Enter a cast flow state based on the ability's target mode, then kick off preview.
	PendingSlot = Slot;
	StopRangePreview();

	switch (Slot.TargetMode)
	{
	case EAeyerjiTargetMode::GroundLocation: CastFlow = EAeyerjiCastFlow::AwaitingGround;  break;
	case EAeyerjiTargetMode::EnemyActor:     CastFlow = EAeyerjiCastFlow::AwaitingEnemy;   break;
	case EAeyerjiTargetMode::FriendlyActor:  CastFlow = EAeyerjiCastFlow::AwaitingFriend;  break;
	default:                                 CastFlow = EAeyerjiCastFlow::Normal;          return;
	}

	StartRangePreview(Slot);
}

bool UAeyerjiTargetingManager::HandleClick(const FAeyerjiTargetingClickContext& Context)
{
	// Consumes a click if we are mid targeting; dispatches the appropriate activation.
	if (CastFlow == EAeyerjiCastFlow::Normal)
	{
		return false;
	}

	if (!OwnerPC.IsValid())
	{
		return false;
	}

	AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick() CastFlow=%d"), static_cast<int32>(CastFlow));

	const bool bHasGroundHit = Context.bHasGroundHit;
	const FHitResult& GroundHit = Context.GroundHit;

	if (bHasGroundHit && TargetingVectorIsFinite(GroundHit.ImpactPoint))
	{
		AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick() ground hit at %s"), *GroundHit.ImpactPoint.ToString());
	}
	else
	{
		AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick() no valid ground hit"));
	}

	switch (CastFlow)
	{
	case EAeyerjiCastFlow::AwaitingGround:
		if (bHasGroundHit && TargetingVectorIsFinite(GroundHit.ImpactPoint))
		{
			if (Hooks.ActivateAtLocation)
			{
				Hooks.ActivateAtLocation(PendingSlot, FVector_NetQuantize(GroundHit.ImpactPoint));
			}
			else
			{
				AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick(): missing ActivateAtLocation hook"));
			}
		}
		break;

	case EAeyerjiCastFlow::AwaitingEnemy:
	case EAeyerjiCastFlow::AwaitingFriend:
	{
		AActor* TargetActor = nullptr;

		if (CastFlow == EAeyerjiCastFlow::AwaitingEnemy && Context.HoveredEnemy.IsValid())
		{
			TargetActor = Context.HoveredEnemy.Get();
		}

		if (!TargetActor && bHasGroundHit)
		{
			TargetActor = GroundHit.GetActor();
		}
		if (!IsEligibleActorTarget(TargetActor))
		{
			TargetActor = nullptr;
		}

		if (!TargetActor)
		{
			FVector WorldOrigin;
			FVector WorldDir;
			if (OwnerPC->DeprojectMousePositionToWorld(WorldOrigin, WorldDir)
				&& TargetingVectorIsFinite(WorldOrigin)
				&& TargetingVectorIsFinite(WorldDir))
			{
				if (UWorld* World = OwnerPC->GetWorld())
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(TargetingEnemyClickTrace), /*bTraceComplex=*/false);
					if (const APawn* MyPawn = OwnerPC->GetPawn())
					{
						Params.AddIgnoredActor(MyPawn);
					}

					auto ShouldIgnoreActor = [&](const AActor* Actor) -> bool
					{
						return Actor && !IsEligibleActorTarget(Actor);
					};

					const FVector TraceStart = WorldOrigin;
					const FVector TraceEnd = TraceStart + WorldDir * 100000.f;

					for (int32 Pass = 0; Pass < 4; ++Pass)
					{
						FHitResult PawnHit;
						if (!World->LineTraceSingleByChannel(PawnHit, TraceStart, TraceEnd, ECC_GameTraceChannel3, Params))
						{
							break;
						}

						if (ShouldIgnoreActor(PawnHit.GetActor()))
						{
							if (AActor* IgnoredActor = PawnHit.GetActor())
							{
								Params.AddIgnoredActor(IgnoredActor);
							}
							continue;
						}

						TargetActor = PawnHit.GetActor();
						break;
					}
				}
			}
		}

		if (IsEligibleActorTarget(TargetActor))
		{
			AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick() activating ability on actor %s"), *GetNameSafe(TargetActor));
			if (Hooks.ActivateOnActor)
			{
				Hooks.ActivateOnActor(PendingSlot, TargetActor);
			}
			else
			{
				AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick(): missing ActivateOnActor hook"));
			}
		}
		else
		{
			AJ_LOG(OwnerPC.Get(), TEXT("HandleTargetingClick() no actor target found for CastFlow=%d"), static_cast<int32>(CastFlow));
		}
		break;
	}

	default:
		break;
	}

	CastFlow = EAeyerjiCastFlow::Normal;
	PendingSlot = {};
	StopRangePreview();
	return true;
}

void UAeyerjiTargetingManager::ClearTargeting()
{
	// Hard reset of cast flow and preview visuals.
	CastFlow = EAeyerjiCastFlow::Normal;
	PendingSlot = {};
	StopRangePreview();
}

void UAeyerjiTargetingManager::TickPreview()
{
	// Heartbeat for keeping the range circle visible while awaiting a target click.
	if (!Preview.bActive || CastFlow == EAeyerjiCastFlow::Normal)
	{
		StopRangePreview();
		return;
	}

	DrawAbilityRangePreview(Preview.Range, Preview.Mode);
}

void UAeyerjiTargetingManager::StartRangePreview(const FAeyerjiAbilitySlot& Slot)
{
	// Begins drawing the range circle on the local client.
	if (!OwnerPC.IsValid() || !OwnerPC->IsLocalController())
	{
		return;
	}

	const float Range = ResolveAbilityPreviewRange(Slot);
	if (Range <= KINDA_SMALL_NUMBER)
	{
		StopRangePreview();
		return;
	}

	Preview.bActive = true;
	Preview.Range = Range;
	Preview.Mode = Slot.TargetMode;

	DrawAbilityRangePreview(Range, Slot.TargetMode);

	if (Tunables.RangePreviewTickRate > KINDA_SMALL_NUMBER)
	{
		OwnerPC->GetWorldTimerManager().SetTimer(
			PreviewTimer,
			FTimerDelegate::CreateUObject(this, &UAeyerjiTargetingManager::TickPreview),
			Tunables.RangePreviewTickRate,
			true);
	}
}

void UAeyerjiTargetingManager::StopRangePreview()
{
	// Clears preview state and timer.
	Preview = {};

	if (OwnerPC.IsValid())
	{
		// World can be gone during teardown (e.g., PIE end); guard before touching timer manager.
		if (UWorld* World = OwnerPC->GetWorld())
		{
			World->GetTimerManager().ClearTimer(PreviewTimer);
		}
		else
		{
			PreviewTimer.Invalidate();
		}
	}
}

float UAeyerjiTargetingManager::ResolveAbilityPreviewRange(const FAeyerjiAbilitySlot& Slot) const
{
	// Query the global ability table first; legacy reflection is intentionally not used.
	float Range = 0.f;

	FGameplayTag AbilityTag;
	for (const FGameplayTag& Tag : Slot.Tag)
	{
		if (Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Ability.")))
		{
			AbilityTag = Tag;
			break;
		}
	}

	const UAeyerjiAbilityTuningSubsystem* Tuning = nullptr;
	if (OwnerPC.IsValid())
	{
		if (UGameInstance* GameInstance = OwnerPC->GetGameInstance())
		{
			Tuning = GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>();
		}
	}

	FAeyerjiAbilityResolvedConfig Config;
	if (Tuning && Tuning->ResolveAbilityConfig(AbilityTag, FMath::Max(1, Slot.Level), Config))
	{
		Range = FMath::Max(Config.PreviewRange, Config.MaxRange);
	}

	if (Slot.Class)
	{
		if (const UGameplayAbility* AbilityCDO = Slot.Class->GetDefaultObject<UGameplayAbility>())
		{
			if (!AbilityTag.IsValid())
			{
				int32 BestDepth = -1;
				for (const FGameplayTag& Tag : AbilityCDO->GetAssetTags())
				{
					const FString TagString = Tag.ToString();
					if (!TagString.StartsWith(TEXT("Ability.")))
					{
						continue;
					}

					int32 Depth = 1;
					for (const TCHAR Character : TagString)
					{
						if (Character == TEXT('.'))
						{
							++Depth;
						}
					}

					if (Depth > BestDepth)
					{
						BestDepth = Depth;
						AbilityTag = Tag;
					}
				}

				FAeyerjiAbilityResolvedConfig ConfigFromClass;
				if (Tuning && Tuning->ResolveAbilityConfig(AbilityTag, FMath::Max(1, Slot.Level), ConfigFromClass))
				{
					Range = FMath::Max(Range, FMath::Max(ConfigFromClass.PreviewRange, ConfigFromClass.MaxRange));
				}
			}

			if (const UGABlink* BlinkCDO = Cast<UGABlink>(AbilityCDO))
			{
				if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
				{
					Range = FMath::Max(Range, BlinkCDO->GetMaxBlinkRange(ASC));
				}
				else
				{
					Range = FMath::Max(Range, BlinkCDO->GetMaxBlinkRange(nullptr));
				}
			}

			(void)AbilityCDO;
		}
	}

	if (Range <= 0.f)
	{
		if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
		{
			if (const UAttributeSet_Ranges* RangeSet = ASC->GetSet<UAttributeSet_Ranges>())
			{
				Range = FMath::Max(Range, RangeSet->GetBlinkRange());
			}
		}

		if (Range <= 0.f && OwnerPC.IsValid())
		{
			if (APawn* LocalPawn = OwnerPC->GetPawn())
			{
				Range = UCharacterStatsLibrary::GetAttackRangeFromActorASC(LocalPawn, /*FallbackRange=*/600.f);
			}
		}
	}

	return FMath::Max(0.f, TargetingFiniteOrDefault(Range, 0.f));
}

void UAeyerjiTargetingManager::DrawAbilityRangePreview(float Range, EAeyerjiTargetMode Mode)
{
	// Renders a colored circle at pawn location (snapped to ground if available).
	if (!OwnerPC.IsValid() || !OwnerPC->IsLocalController() || !FMath::IsFinite(Range) || Range <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	UWorld* World = OwnerPC->GetWorld();
	APawn* LocalPawn = OwnerPC->GetPawn();
	if (!World || !LocalPawn)
	{
		return;
	}

	FVector Center = LocalPawn->GetActorLocation();
	if (!TargetingVectorIsFinite(Center))
	{
		return;
	}
	if (Hooks.GroundTrace)
	{
		FHitResult GroundHit;
		if (Hooks.GroundTrace(GroundHit) && TargetingVectorIsFinite(GroundHit.ImpactPoint))
		{
			Center.Z = GroundHit.ImpactPoint.Z + 2.f;
		}
	}

	const FColor Color =
		(Mode == EAeyerjiTargetMode::GroundLocation) ? FColor::Purple :
		(Mode == EAeyerjiTargetMode::EnemyActor)     ? FColor::Red :
		(Mode == EAeyerjiTargetMode::FriendlyActor)  ? FColor::Green :
		                                              FColor::Silver;

	constexpr int32 Segments = 64;
	DrawDebugCircle(World, Center, Range, Segments, Color, false, Tunables.RangePreviewDrawLife, 0, Tunables.RangePreviewThickness, FVector(1, 0, 0), FVector(0, 1, 0), false);
}

UAbilitySystemComponent* UAeyerjiTargetingManager::GetControlledAbilitySystem() const
{
	// Helper to fetch the ASC from the currently possessed pawn (if any).
	if (!OwnerPC.IsValid())
	{
		return nullptr;
	}

	APawn* ControlledPawn = OwnerPC->GetPawn();
	if (!ControlledPawn)
	{
		return nullptr;
	}

	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ControlledPawn);
}

bool UAeyerjiTargetingManager::IsEligibleActorTarget(const AActor* TargetActor) const
{
	if (!OwnerPC.IsValid() || !IsValid(TargetActor) || TargetActor->GetWorld() != OwnerPC->GetWorld())
	{
		return false;
	}

	const APawn* ControlledPawn = OwnerPC->GetPawn();
	if (!ControlledPawn || TargetActor == ControlledPawn)
	{
		return false;
	}

	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor))
	{
		if (TargetASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		{
			return false;
		}
	}

	const bool bSameTeam = AbilityTeamUtils::AreOnSameTeam(ControlledPawn, TargetActor);
	return CastFlow == EAeyerjiCastFlow::AwaitingFriend ? bSameTeam : !bSameTeam;
}
