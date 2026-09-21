#include "Testing/AeyerjiCombatBalanceTestHarness.h"

#include "Engine/World.h"

namespace
{
	constexpr int32 MaximumBombardmentEvents = 8192;
	constexpr int32 MaximumTrackedBombardments = 64;

	FString BombardmentCSVString(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		return TEXT("\"") + Value + TEXT("\"");
	}
}

void AAeyerjiCombatBalanceTestHarness::RecordBombardmentEvent(AActor* Source, AActor* AimTarget,
	uint32 AttackId, EAeyerjiBombardmentTelemetryEvent Phase, const FVector& Center, float Radius,
	int32 CandidateCount, int32 DamageApplications)
{
	if (!Source || !Source->HasAuthority() || !Source->GetWorld())
	{
		return;
	}
	AAeyerjiCombatBalanceTestHarness* Harness = FindForWorld(Source->GetWorld());
	if (!Harness || !Harness->HasAuthority() || Harness->TestState != EAeyerjiCombatTestState::Running)
	{
		return;
	}
	const uint64 Key = (static_cast<uint64>(Source->GetUniqueID()) << 32) | AttackId;
	FBombardmentEvent Event;
	if (Phase == EAeyerjiBombardmentTelemetryEvent::Started)
	{
		if (Center.ContainsNaN() || !FMath::IsFinite(Radius) || Radius <= 0.f || Harness->ActiveBombardments.Contains(Key))
		{
			return;
		}
		if (Harness->ActiveBombardments.Num() >= MaximumTrackedBombardments)
		{
			++Harness->BombardmentUntrackedStarts;
			return;
		}
		Event.AttackId = AttackId;
		Event.Source = Source->GetName();
		Event.Archetype = Harness->ResolveDamageArchetype(Source);
		Event.AimTarget = GetNameSafe(AimTarget);
		Event.Center = Center;
		Event.Radius = Radius;
		Harness->ActiveBombardments.Add(Key, Event);
		++Harness->BombardmentStarts;
		Harness->BombardmentPeakActiveZones = FMath::Max(Harness->BombardmentPeakActiveZones, Harness->ActiveBombardments.Num());
	}
	else if (Phase == EAeyerjiBombardmentTelemetryEvent::Impacted || Phase == EAeyerjiBombardmentTelemetryEvent::Cancelled)
	{
		if (!Harness->ActiveBombardments.RemoveAndCopyValue(Key, Event))
		{
			return; // Duplicate terminal event, or its warning began before recording.
		}
		if (Phase == EAeyerjiBombardmentTelemetryEvent::Impacted)
		{
			++Harness->BombardmentImpacts;
			Event.CandidateCount = FMath::Max<int32>(INDEX_NONE, CandidateCount);
			Event.DamageApplications = FMath::Max<int32>(INDEX_NONE, DamageApplications);
			Harness->BombardmentDamageApplications += FMath::Max(0, Event.DamageApplications);
		}
		else
		{
			++Harness->BombardmentCancellations;
		}
	}
	else
	{
		return;
	}
	Event.Phase = Phase;
	Event.TimeSeconds = FMath::Max(0.f, static_cast<float>(Source->GetWorld()->GetTimeSeconds() - Harness->CombatStartWorldTime));
	Event.ActiveZones = Harness->ActiveBombardments.Num();
	Event.PeakActiveZones = Harness->BombardmentPeakActiveZones;
	Event.TotalStarts = Harness->BombardmentStarts;
	Event.TotalImpacts = Harness->BombardmentImpacts;
	Event.TotalCancellations = Harness->BombardmentCancellations;
	Event.TotalDamageApplications = Harness->BombardmentDamageApplications;
	if (Harness->BombardmentEvents.Num() < MaximumBombardmentEvents)
	{
		Harness->BombardmentEvents.Add(MoveTemp(Event));
	}
	else
	{
		++Harness->BombardmentDroppedEvents;
	}
}

void AAeyerjiCombatBalanceTestHarness::ResetBombardmentTelemetry()
{
	BombardmentEvents.Reset();
	ActiveBombardments.Reset();
	BombardmentStarts = 0;
	BombardmentImpacts = 0;
	BombardmentCancellations = 0;
	BombardmentDamageApplications = 0;
	BombardmentPeakActiveZones = 0;
	BombardmentDroppedEvents = 0;
	BombardmentUntrackedStarts = 0;
}

FString AAeyerjiCombatBalanceTestHarness::BuildBombardmentCSV() const
{
	FString CSV(TEXT("TimeSeconds,Phase,AttackId,Source,Archetype,AimTarget,CenterX,CenterY,CenterZ,RadiusCm,CandidateCount,DamageApplications,ActiveZones,PeakActiveZones,TotalStarts,TotalImpacts,TotalCancellations,TotalDamageApplications\n"));
	for (const FBombardmentEvent& Event : BombardmentEvents)
	{
		const TCHAR* Phase = Event.Phase == EAeyerjiBombardmentTelemetryEvent::Started ? TEXT("Started")
			: Event.Phase == EAeyerjiBombardmentTelemetryEvent::Impacted ? TEXT("Impacted") : TEXT("Cancelled");
		CSV += FString::Printf(TEXT("%.3f,%s,%u,%s,%s,%s,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d\n"),
			Event.TimeSeconds, Phase, Event.AttackId, *BombardmentCSVString(Event.Source),
			*BombardmentCSVString(Event.Archetype), *BombardmentCSVString(Event.AimTarget),
			Event.Center.X, Event.Center.Y, Event.Center.Z, Event.Radius, Event.CandidateCount,
			Event.DamageApplications, Event.ActiveZones, Event.PeakActiveZones, Event.TotalStarts,
			Event.TotalImpacts, Event.TotalCancellations, Event.TotalDamageApplications);
	}
	return CSV;
}

FString AAeyerjiCombatBalanceTestHarness::BuildBombardmentSummary() const
{
	return FString::Printf(TEXT("BombardmentStarts=%d\nBombardmentImpacts=%d\nBombardmentCancellations=%d\nBombardmentDamageApplications=%d\nBombardmentActiveZonesAtStop=%d\nBombardmentPeakActiveZones=%d\nBombardmentStoredEvents=%d\nBombardmentDroppedEvents=%d\nBombardmentUntrackedStarts=%d\n"),
		BombardmentStarts, BombardmentImpacts, BombardmentCancellations, BombardmentDamageApplications,
		ActiveBombardments.Num(), BombardmentPeakActiveZones, BombardmentEvents.Num(), BombardmentDroppedEvents, BombardmentUntrackedStarts);
}
