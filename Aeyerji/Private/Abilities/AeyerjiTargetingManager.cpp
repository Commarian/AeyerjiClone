// ===============================
// File: AeyerjiTargetingManager.cpp
// ===============================
#include "Abilities/AeyerjiTargetingManager.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/Blink/DA_Blink.h"
#include "Abilities/Blink/GABlink.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GravitonPull/DA_AGGravitonPull.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AttributeSet_Ranges.h"
#include "CharacterStatsLibrary.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Logging/AeyerjiLog.h"
#include "UObject/UnrealType.h"

void UAeyerjiTargetingManager::Initialize(AAeyerjiPlayerController* InOwner, const FAeyerjiTargetingTunables& InTunables)
{
	// Cache owning controller and tunables (owner re-calls on possession swaps).
	OwnerPC = InOwner;
	Tunables = InTunables;
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
	// Enter a cast flow state based on the ability’s target mode, then kick off preview.
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

	if (!OwnerPC)
	{
		return false;
	}

	AJ_LOG(OwnerPC, TEXT("HandleTargetingClick() CastFlow=%d"), static_cast<int32>(CastFlow));

	const bool bHasGroundHit = Context.bHasGroundHit;
	const FHitResult& GroundHit = Context.GroundHit;

	if (bHasGroundHit)
	{
		AJ_LOG(OwnerPC, TEXT("HandleTargetingClick() ground hit at %s"), *GroundHit.ImpactPoint.ToString());
	}
	else
	{
		AJ_LOG(OwnerPC, TEXT("HandleTargetingClick() no ground hit"));
	}

	switch (CastFlow)
	{
	case EAeyerjiCastFlow::AwaitingGround:
		if (bHasGroundHit)
		{
			if (Hooks.ActivateAtLocation)
			{
				Hooks.ActivateAtLocation(PendingSlot, FVector_NetQuantize(GroundHit.ImpactPoint));
			}
			else
			{
				AJ_LOG(OwnerPC, TEXT("HandleTargetingClick(): missing ActivateAtLocation hook"));
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

		if (!TargetActor)
		{
			FVector WorldOrigin;
			FVector WorldDir;
			if (OwnerPC->DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
			{
				if (UWorld* World = OwnerPC->GetWorld())
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(TargetingPawnTrace), /*bTraceComplex=*/false);
					if (const APawn* MyPawn = OwnerPC->GetPawn())
					{
						Params.AddIgnoredActor(MyPawn);
					}

					auto ShouldIgnoreActor = [&](const AActor* Actor) -> bool
					{
						if (!Actor)
						{
							return false;
						}

						if (const APawn* Pawn = Cast<APawn>(Actor))
						{
							return Pawn->IsPlayerControlled();
						}

						const AActor* Owner = Actor->GetOwner();
						while (Owner)
						{
							if (const APawn* OwnerPawn = Cast<APawn>(Owner))
							{
								if (OwnerPawn->IsPlayerControlled())
								{
									return true;
								}
							}
							Owner = Owner->GetOwner();
						}

						return false;
					};

					const FVector TraceStart = WorldOrigin;
					const FVector TraceEnd = TraceStart + WorldDir * 100000.f;

					for (int32 Pass = 0; Pass < 4; ++Pass)
					{
						FHitResult PawnHit;
						if (!World->LineTraceSingleByChannel(PawnHit, TraceStart, TraceEnd, ECC_Pawn, Params))
						{
							break;
						}

						if (ShouldIgnoreActor(PawnHit.GetActor()))
						{
							Params.AddIgnoredActor(PawnHit.GetActor());
							continue;
						}

						TargetActor = PawnHit.GetActor();
						break;
					}
				}
			}
		}

		if (TargetActor)
		{
			AJ_LOG(OwnerPC, TEXT("HandleTargetingClick() activating ability on actor %s"), *GetNameSafe(TargetActor));
			if (Hooks.ActivateOnActor)
			{
				Hooks.ActivateOnActor(PendingSlot, TargetActor);
			}
			else
			{
				AJ_LOG(OwnerPC, TEXT("HandleTargetingClick(): missing ActivateOnActor hook"));
			}
		}
		else
		{
			AJ_LOG(OwnerPC, TEXT("HandleTargetingClick() no actor target found for CastFlow=%d"), static_cast<int32>(CastFlow));
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
	if (!OwnerPC || !OwnerPC->IsLocalController())
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

	if (OwnerPC)
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
	// Queries CDO/data assets to find a usable preview range for the ability slot.
	float Range = 0.f;

	if (Slot.Class)
	{
		if (const UGameplayAbility* AbilityCDO = Slot.Class->GetDefaultObject<UGameplayAbility>())
		{
			if (const UAbilitySystemComponent* ASC = GetControlledAbilitySystem())
			{
				const FObjectProperty* BlinkDAProp = FindFProperty<FObjectProperty>(Slot.Class, TEXT("BlinkConfig"));
				if (!BlinkDAProp)
				{
					for (TFieldIterator<FObjectProperty> It(Slot.Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
					{
						if (It->PropertyClass && It->PropertyClass->IsChildOf(UDA_Blink::StaticClass()))
						{
							BlinkDAProp = *It;
							break;
						}
					}
				}

				if (BlinkDAProp && BlinkDAProp->PropertyClass->IsChildOf(UDA_Blink::StaticClass()))
				{
					if (const UDA_Blink* BlinkDA = Cast<UDA_Blink>(BlinkDAProp->GetObjectPropertyValue_InContainer(AbilityCDO)))
					{
						float DAValue = BlinkDA->Tunables.MaxRange;

						const FRichCurve* Curve = BlinkDA->Tunables.RangeByLevel.GetRichCurveConst();
						if (Curve && Curve->GetNumKeys() > 0)
						{
							const float Level = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute());
							DAValue = Curve->Eval(Level, BlinkDA->Tunables.MaxRange);
						}

						DAValue *= FMath::Max(0.0f, BlinkDA->Tunables.RangeScalar);
						Range = FMath::Max(Range, DAValue);
					}
				}
			}

			if (const FObjectProperty* GravProp = FindFProperty<FObjectProperty>(Slot.Class, TEXT("GravitonConfig")))
			{
				if (GravProp->PropertyClass && GravProp->PropertyClass->IsChildOf(UDA_AGGravitonPull::StaticClass()))
				{
					if (const UDA_AGGravitonPull* GravDA = Cast<UDA_AGGravitonPull>(GravProp->GetObjectPropertyValue_InContainer(AbilityCDO)))
					{
						Range = FMath::Max(Range, GravDA->Tunables.MaxRange);
					}
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

			auto TryReadFloatProperty = [&](const TCHAR* PropName)
			{
				if (const FProperty* Prop = Slot.Class->FindPropertyByName(PropName))
				{
					if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
					{
						Range = FMath::Max(Range, FloatProp->GetFloatingPointPropertyValue(FloatProp->ContainerPtrToValuePtr<void>(AbilityCDO)));
					}
				}
			};

			TryReadFloatProperty(TEXT("MaxBlinkDistance"));
			TryReadFloatProperty(TEXT("MaxRange"));
			TryReadFloatProperty(TEXT("Range"));
			TryReadFloatProperty(TEXT("DefaultBlinkRange"));
			TryReadFloatProperty(TEXT("BlinkRange"));
			TryReadFloatProperty(TEXT("BlinkDistance"));
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

		if (Range <= 0.f && OwnerPC)
		{
			if (APawn* LocalPawn = OwnerPC->GetPawn())
			{
				Range = UCharacterStatsLibrary::GetAttackRangeFromActorASC(LocalPawn, /*FallbackRange=*/600.f);
			}
		}
	}

	return FMath::Max(0.f, Range);
}

void UAeyerjiTargetingManager::DrawAbilityRangePreview(float Range, EAeyerjiTargetMode Mode)
{
	// Renders a colored circle at pawn location (snapped to ground if available).
	if (!OwnerPC || !OwnerPC->IsLocalController() || Range <= KINDA_SMALL_NUMBER)
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
	if (Hooks.GroundTrace)
	{
		FHitResult GroundHit;
		if (Hooks.GroundTrace(GroundHit))
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
	if (!OwnerPC)
	{
		return nullptr;
	}

	const APawn* ControlledPawn = OwnerPC->GetPawn();
	if (!ControlledPawn)
	{
		return nullptr;
	}

	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(ControlledPawn);
	return ASI ? ASI->GetAbilitySystemComponent() : nullptr;
}
