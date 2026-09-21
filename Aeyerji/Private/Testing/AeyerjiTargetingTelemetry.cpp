#include "Testing/AeyerjiCombatBalanceTestHarness.h"

#include "Engine/World.h"

namespace
{
	constexpr int32 MaximumTargetingEvents = 4096;

	FString TargetingCSVString(FString Value)
	{
		Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
		Value.ReplaceInline(TEXT("\n"), TEXT(" "));
		Value.ReplaceInline(TEXT("\r"), TEXT(" "));
		return TEXT("\"") + Value + TEXT("\"");
	}

	const TCHAR* TargetingEventName(const EAeyerjiCombatTargetingTelemetryEvent Event)
	{
		switch (Event)
		{
		case EAeyerjiCombatTargetingTelemetryEvent::PlayerTargetHandoff:
			return TEXT("PlayerTargetHandoff");
		case EAeyerjiCombatTargetingTelemetryEvent::PlayerPrimaryActivated:
			return TEXT("PlayerPrimaryActivated");
		case EAeyerjiCombatTargetingTelemetryEvent::EnemyDeadTargetCleared:
			return TEXT("EnemyDeadTargetCleared");
	case EAeyerjiCombatTargetingTelemetryEvent::EnemyLeashTargetCleared:
			return TEXT("EnemyLeashTargetCleared");
		case EAeyerjiCombatTargetingTelemetryEvent::InvalidPrimaryActivationPrevented:
			return TEXT("InvalidPrimaryActivationPrevented");
		default:
			return TEXT("Unknown");
		}
	}
}

void AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
	AActor* Source,
	AActor* PreviousTarget,
	AActor* NewTarget,
	const EAeyerjiCombatTargetingTelemetryEvent Event,
	const FName Reason,
	const uint32 CommandSerial)
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

	switch (Event)
	{
	case EAeyerjiCombatTargetingTelemetryEvent::PlayerTargetHandoff:
		++Harness->PlayerTargetHandoffs;
		if (Reason == TEXT("PreviousTargetInvalid"))
		{
			++Harness->DeadTargetHandoffs;
		}
		break;
	case EAeyerjiCombatTargetingTelemetryEvent::PlayerPrimaryActivated:
		++Harness->PlayerPrimaryActivations;
		break;
	case EAeyerjiCombatTargetingTelemetryEvent::EnemyDeadTargetCleared:
		++Harness->EnemyDeadTargetClears;
		break;
	case EAeyerjiCombatTargetingTelemetryEvent::EnemyLeashTargetCleared:
		++Harness->EnemyLeashTargetClears;
		break;
	case EAeyerjiCombatTargetingTelemetryEvent::InvalidPrimaryActivationPrevented:
		++Harness->InvalidPrimaryActivationsPrevented;
		break;
	default:
		return;
	}

	if (Harness->TargetingEvents.Num() >= MaximumTargetingEvents)
	{
		++Harness->TargetingDroppedEvents;
		return;
	}

	FCombatTargetingEvent& Stored = Harness->TargetingEvents.AddDefaulted_GetRef();
	Stored.TimeSeconds = FMath::Max(
		0.f,
		static_cast<float>(Source->GetWorld()->GetTimeSeconds() - Harness->CombatStartWorldTime));
	Stored.Event = TargetingEventName(Event);
	Stored.Source = Source->GetName();
	Stored.PreviousTarget = GetNameSafe(PreviousTarget);
	Stored.NewTarget = GetNameSafe(NewTarget);
	Stored.Reason = Reason.IsNone() ? TEXT("None") : Reason.ToString();
	Stored.CommandSerial = CommandSerial;
}

void AAeyerjiCombatBalanceTestHarness::ResetTargetingTelemetry()
{
	TargetingEvents.Reset();
	PlayerTargetHandoffs = 0;
	DeadTargetHandoffs = 0;
	PlayerPrimaryActivations = 0;
	EnemyDeadTargetClears = 0;
	EnemyLeashTargetClears = 0;
	InvalidPrimaryActivationsPrevented = 0;
	TargetingDroppedEvents = 0;
}

FString AAeyerjiCombatBalanceTestHarness::BuildTargetingCSV() const
{
	FString CSV(TEXT("TimeSeconds,Event,Source,PreviousTarget,NewTarget,Reason,CommandSerial\n"));
	for (const FCombatTargetingEvent& Event : TargetingEvents)
	{
		CSV += FString::Printf(
			TEXT("%.3f,%s,%s,%s,%s,%s,%u\n"),
			Event.TimeSeconds,
			*TargetingCSVString(Event.Event),
			*TargetingCSVString(Event.Source),
			*TargetingCSVString(Event.PreviousTarget),
			*TargetingCSVString(Event.NewTarget),
			*TargetingCSVString(Event.Reason),
			Event.CommandSerial);
	}
	return CSV;
}

FString AAeyerjiCombatBalanceTestHarness::BuildTargetingSummary() const
{
	return FString::Printf(
		TEXT("PlayerTargetHandoffs=%d\nDeadTargetHandoffs=%d\nPlayerPrimaryActivations=%d\nEnemyDeadTargetClears=%d\nEnemyLeashTargetClears=%d\nPreventedTargetlessAttacks=%d\nTargetingStoredEvents=%d\nTargetingDroppedEvents=%d\n"),
		PlayerTargetHandoffs,
		DeadTargetHandoffs,
		PlayerPrimaryActivations,
		EnemyDeadTargetClears,
		EnemyLeashTargetClears,
		InvalidPrimaryActivationsPrevented,
		TargetingEvents.Num(),
		TargetingDroppedEvents);
}
