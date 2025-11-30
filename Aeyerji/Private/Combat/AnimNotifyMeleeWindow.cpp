// AnimNotifyMeleeWindow.cpp
#include "Combat/AnimNotifyMeleeWindow.h"
#include "AeyerjiGameplayTags.h"

#include "Abilities/GameplayAbilityTargetTypes.h"          // FGameplayAbilityTargetData_SingleTargetHit
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Logging/LogMacros.h"
#include "Kismet/KismetSystemLibrary.h"

UAnimNotifyMeleeWindow::UAnimNotifyMeleeWindow()
{
	if (!TickEventTag.IsValid())
	{
		TickEventTag = AeyerjiTags::Event_Combat_Melee_TraceWindow;
	}
}

void UAnimNotifyMeleeWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	// Create state for this mesh
	FWindowState& State = ActiveWindows.FindOrAdd(MeshComp);

	// Cache ASC early only for game/PIE worlds
	if (AActor* Owner = MeshComp->GetOwner())
	{
		if (UWorld* W = Owner->GetWorld())
		{
			if (W->IsGameWorld() || W->WorldType == EWorldType::PIE)
			{
				State.CachedASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner);
			}
		}
	}
}

void UAnimNotifyMeleeWindow::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner) return;

	// Server-only gate (optional)
	if (bServerOnly && MeshComp->GetWorld() && MeshComp->GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	const FWindowState* WinState = ActiveWindows.Find(MeshComp);
	if (!WinState || !WinState->CachedASC.IsValid())
	{
		// No ASC (e.g., editor preview actor) — skip the work.
		return;
	}

	const FVector Start = MeshComp->GetSocketLocation(StartSocket);
	const FVector End   = MeshComp->GetSocketLocation(EndSocket);

	TArray<TEnumAsByte<EObjectTypeQuery>> Channels = ObjectTypes;
	if (Channels.Num() == 0)
	{
		Channels = {
			EObjectTypeQuery::ObjectTypeQuery3, // Pawn
			EObjectTypeQuery::ObjectTypeQuery2  // WorldDynamic
		};
	}

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(Owner);

	TArray<FHitResult> Hits;
	const bool bHit = UKismetSystemLibrary::SphereTraceMultiForObjects(
		MeshComp,
		Start, End,
		Radius,
		Channels,
		true,  // bTraceComplex
		IgnoreActors,
		bDrawDebug ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None,
		Hits,
		true,  // bIgnoreSelf
		FLinearColor::Red,
		FLinearColor::Green,
		0.05f
	);

	// De-dup per window if requested
	if (bHit && bPreventRepeatedHitsDuringWindow)
	{
		if (FWindowState* MutableWinState = ActiveWindows.Find(MeshComp))
		{
			Hits.RemoveAll([MutableWinState](const FHitResult& HR)
			{
				AActor* HitActor = HR.GetActor();
				if (!HitActor)
				{
					return true; // drop invalid hits
				}

				TWeakObjectPtr<AActor> Weak = HitActor;
				if (MutableWinState->AlreadyHit.Contains(Weak))
				{
					return true; // filtered
				}

				MutableWinState->AlreadyHit.Add(Weak);
				return false;
			});
		}
	}

	// Send event (even if empty, ability may still want to tick logic)
	SendTickEvent(Owner, Hits);
}

void UAnimNotifyMeleeWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp) return;
	ActiveWindows.Remove(MeshComp);
}

void UAnimNotifyMeleeWindow::SendTickEvent(AActor* OwnerActor, const TArray<FHitResult>& Hits) const
{
	if (!OwnerActor || !TickEventTag.IsValid())
	{
		return;
	}

	UWorld* World = OwnerActor->GetWorld();
	if (!World)
	{
		return;
	}

	// Don’t send from editor preview worlds (Persona)
	if (!(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return;
	}

	// Server-only (optional)
	if (bServerOnly && World->GetNetMode() == NM_Client)
	{
		return;
	}

	// Must have a valid ASC
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor);
	if (!ASC)
	{
		return; // avoids log spam
	}

	// Build payload
	FGameplayEventData Payload;
	Payload.Instigator = OwnerActor;
	Payload.EventTag   = TickEventTag;

	FGameplayAbilityTargetDataHandle TDH;
	for (const FHitResult& HR : Hits)
	{
		auto* Data = new FGameplayAbilityTargetData_SingleTargetHit(HR);
		TDH.Add(Data);
	}
	Payload.TargetData = TDH;

	// Send directly via validated ASC
	ASC->HandleGameplayEvent(TickEventTag, &Payload);
}



