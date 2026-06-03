#include "World/AeyerjiEndRunPortal.h"

#include "Aeyerji/AeyerjiGameState.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

AAeyerjiEndRunPortal::AAeyerjiEndRunPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(SceneRoot);
	InteractionSphere->SetSphereRadius(120.f);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AAeyerjiEndRunPortal::BeginPlay()
{
	Super::BeginPlay();

	if (InteractionSphere)
	{
		InteractionSphere->OnComponentBeginOverlap.RemoveDynamic(this, &AAeyerjiEndRunPortal::HandlePortalOverlap);
		InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiEndRunPortal::HandlePortalOverlap);
		InteractionSphere->OnComponentEndOverlap.RemoveDynamic(this, &AAeyerjiEndRunPortal::HandlePortalEndOverlap);
		InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AAeyerjiEndRunPortal::HandlePortalEndOverlap);
	}
}

void AAeyerjiEndRunPortal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetExtractionCountdown(nullptr, /*bNotifyBlueprintReset=*/true);

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiEndRunPortal::HandlePortalOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	static_cast<void>(OverlappedComponent);
	static_cast<void>(OtherComp);
	static_cast<void>(OtherBodyIndex);
	static_cast<void>(bFromSweep);
	static_cast<void>(SweepResult);

	if (!HasAuthority() || bConsumed || !OtherActor)
	{
		return;
	}

	APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	if (!OverlappingPawn || !OverlappingPawn->IsPlayerControlled())
	{
		return;
	}

	BeginExtractionCountdown(OverlappingPawn);
}

void AAeyerjiEndRunPortal::HandlePortalEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	static_cast<void>(OverlappedComponent);
	static_cast<void>(OtherComp);
	static_cast<void>(OtherBodyIndex);

	if (!HasAuthority() || bConsumed || !OtherActor)
	{
		return;
	}

	APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	if (!OverlappingPawn || !OverlappingPawn->IsPlayerControlled() || PendingExtractingPawn != OverlappingPawn)
	{
		return;
	}

	ResetExtractionCountdown(OverlappingPawn, /*bNotifyBlueprintReset=*/true);
	TryBeginCountdownFromCurrentOverlaps();
}

void AAeyerjiEndRunPortal::BeginExtractionCountdown(APawn* OverlappingPawn)
{
	if (!HasAuthority() || bConsumed || !OverlappingPawn || !OverlappingPawn->IsPlayerControlled())
	{
		return;
	}

	if (PendingExtractingPawn == OverlappingPawn)
	{
		return;
	}

	if (PendingExtractingPawn)
	{
		return;
	}

	const float CountdownDurationSeconds = FMath::Max(ExtractionDurationSeconds, 0.1f);
	PendingExtractingPawn = OverlappingPawn;

	if (AAeyerjiPlayerController* PlayerController = Cast<AAeyerjiPlayerController>(OverlappingPawn->GetController()))
	{
		PlayerController->Client_BeginExtractionCountdown(CountdownDurationSeconds);
	}

	GetWorldTimerManager().ClearTimer(ExtractionCountdownTimerHandle);
	GetWorldTimerManager().SetTimer(
		ExtractionCountdownTimerHandle,
		this,
		&AAeyerjiEndRunPortal::CompleteExtractionCountdown,
		CountdownDurationSeconds,
		false);

	BP_OnExtractionCountdownStarted(OverlappingPawn, CountdownDurationSeconds);
}

void AAeyerjiEndRunPortal::ResetExtractionCountdown(APawn* ExpectedPawn, const bool bNotifyBlueprintReset)
{
	APawn* PreviousPawn = PendingExtractingPawn.Get();
	if (!PreviousPawn)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ExtractionCountdownTimerHandle);
		}
		return;
	}

	if (ExpectedPawn && PreviousPawn != ExpectedPawn)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExtractionCountdownTimerHandle);
	}

	if (IsValid(PreviousPawn))
	{
		if (AAeyerjiPlayerController* PlayerController = Cast<AAeyerjiPlayerController>(PreviousPawn->GetController()))
		{
			PlayerController->Client_ResetExtractionCountdown();
		}
	}

	PendingExtractingPawn = nullptr;

	if (bNotifyBlueprintReset && IsValid(PreviousPawn))
	{
		BP_OnExtractionCountdownReset(PreviousPawn);
	}
}

void AAeyerjiEndRunPortal::TryBeginCountdownFromCurrentOverlaps()
{
	if (!HasAuthority() || bConsumed || PendingExtractingPawn || !InteractionSphere)
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	InteractionSphere->GetOverlappingActors(OverlappingActors, APawn::StaticClass());

	for (AActor* OverlappingActor : OverlappingActors)
	{
		APawn* OverlappingPawn = Cast<APawn>(OverlappingActor);
		if (OverlappingPawn && OverlappingPawn->IsPlayerControlled())
		{
			BeginExtractionCountdown(OverlappingPawn);
			return;
		}
	}
}

void AAeyerjiEndRunPortal::CompleteExtractionCountdown()
{
	if (!HasAuthority() || bConsumed)
	{
		return;
	}

	APawn* ExtractingPawn = PendingExtractingPawn.Get();
	if (!IsValid(ExtractingPawn) || !InteractionSphere || !InteractionSphere->IsOverlappingActor(ExtractingPawn))
	{
		ResetExtractionCountdown(ExtractingPawn, /*bNotifyBlueprintReset=*/true);
		TryBeginCountdownFromCurrentOverlaps();
		return;
	}

	bConsumed = true;
	BP_OnExtractionCountdownCompleted(ExtractingPawn);
	ResetExtractionCountdown(ExtractingPawn, /*bNotifyBlueprintReset=*/false);

	if (InteractionSphere)
	{
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			GameState->Server_CompleteExtraction();
		}
	}

	Destroy();
}
