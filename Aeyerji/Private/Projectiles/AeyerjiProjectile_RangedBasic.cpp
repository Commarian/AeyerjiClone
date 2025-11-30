#include "Projectiles/AeyerjiProjectile_RangedBasic.h"

#include "Abilities/AbilityTeamUtils.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/GA_PrimaryRangedBasic.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ProjectileAimLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogRangedProjectile, Log, All);

AAeyerjiProjectile_RangedBasic::AAeyerjiProjectile_RangedBasic()
	: bImpactProcessed(false)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	CollisionComponent->InitSphereRadius(16.f);
	CollisionComponent->SetCollisionProfileName(TEXT("Projectile"));
	CollisionComponent->SetGenerateOverlapEvents(true);
	CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiProjectile_RangedBasic::OnCollisionBeginOverlap);
	CollisionComponent->OnComponentHit.AddDynamic(this, &AAeyerjiProjectile_RangedBasic::OnCollisionHit);
	RootComponent = CollisionComponent;

	SpinRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpinRoot"));
	SpinRoot->SetupAttachment(RootComponent);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SpinRoot);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->ProjectileGravityScale = 0.f;
	ProjectileMovement->InitialSpeed = 1200.f;
	ProjectileMovement->MaxSpeed = 1200.f;
	ProjectileMovement->bInitialVelocityInLocalSpace = false;
}

void AAeyerjiProjectile_RangedBasic::InitializeProjectile(UGA_PrimaryRangedBasic* InAbility,
                                                          AActor* InIntendedTarget,
                                                          float InitialSpeed,
                                                          float StationaryTolerance)
{
	if (const UWorld* World = GetWorld())
	{
		SpawnTimeSeconds = World->GetTimeSeconds();
	}
	OwningAbility = InAbility;
	IntendedTarget = InIntendedTarget;
	SourceActor = InAbility ? InAbility->GetAvatarActorFromActorInfo() : nullptr;
	// UE_LOG(LogRangedProjectile,
	//        Log,
	//        TEXT("InitProjectile: %s Owner=%s Instigator=%s Target=%s Speed=%.1f StationaryTol=%.1f Authority=%s"),
	//        *GetName(),
	//        *GetNameSafe(GetOwner()),
	//        *GetNameSafe(GetInstigator()),
	//        *GetNameSafe(IntendedTarget.Get()),
	//        InitialSpeed,
	//        StationaryTolerance,
	//        HasAuthority() ? TEXT("Yes") : TEXT("No"));
	CachedSourceTags.Reset();
	if (SourceActor.IsValid())
	{
		for (const FName& Tag : SourceActor->Tags)
		{
			if (Tag != NAME_None)
			{
				CachedSourceTags.AddUnique(Tag);
			}
		}
	}
	SpawnLocation = GetActorLocation();

	if (CollisionComponent)
	{
		ApplyCollisionResponseOverrides();

		if (SourceActor.IsValid())
		{
			// Allow the projectile to spawn inside the shooter without exploding immediately.
			CollisionComponent->IgnoreActorWhenMoving(SourceActor.Get(), /*bShouldIgnore=*/true);
		}
		if (APawn* SourcePawn = Cast<APawn>(SourceActor.Get()))
		{
			SetInstigator(SourcePawn);
		}
	}

	if (MaxLifetime > 0.f)
	{
		SetLifeSpan(MaxLifetime);
	}

	if (ProjectileMovement)
	{
		const float Speed = FMath::Max(InitialSpeed, 1.f);
		ProjectileMovement->InitialSpeed = Speed;
		ProjectileMovement->MaxSpeed = Speed;
		// Start with world overlap then restore blocking after a short grace based on speed.
		SuppressWorldCollisionForGrace();

		const FVector Start = GetActorLocation();
		const FVector Forward = GetActorForwardVector();

		const bool bHasTarget = IntendedTarget.IsValid();
		FVector CalculatedAim;
		const bool bUsedLead = UProjectileAimLibrary::LaunchProjectileTowards(this,
			ProjectileMovement,
			IntendedTarget.Get(),
			Speed,
			StationaryTolerance,
			CalculatedAim);
		// UE_LOG(LogRangedProjectile,
		//        Log,
		//        TEXT("InitProjectile Aim: HasTarget=%s UsedLead=%s Start=%s AimPoint=%s Forward=%s Speed=%.1f Velocity=%s"),
		//        bHasTarget ? TEXT("Yes") : TEXT("No"),
		//        bUsedLead ? TEXT("Yes") : TEXT("No"),
		//        *Start.ToString(),
		//        *CalculatedAim.ToString(),
		//        *Forward.ToString(),
		//        Speed,
		//        *ProjectileMovement->Velocity.ToString());

		if (!bHasTarget && !bUsedLead)
		{
			// No target or prediction available: stay aligned with the muzzle/forward vector.
			ProjectileMovement->Velocity = Forward * Speed;
		}
		else if (ProjectileMovement->Velocity.IsNearlyZero())
		{
			const FVector FallbackDirection = (bHasTarget && IntendedTarget.IsValid())
				? (IntendedTarget.Get()->GetActorLocation() - Start).GetSafeNormal()
				: Forward;
			// Prediction failed to generate velocity (e.g., target obstructed) so fall back to a best guess.
			ProjectileMovement->Velocity = FallbackDirection * Speed;
			// UE_LOG(LogRangedProjectile,
			//        Log,
			//        TEXT("InitProjectile VelocityFallback: Direction=%s Speed=%.1f"),
			//        *FallbackDirection.ToString(),
			//        Speed);
		}

		ProjectileMovement->Activate(true);
	}
}

void AAeyerjiProjectile_RangedBasic::BeginPlay()
{
	Super::BeginPlay();

	// Ensure locally replicated instances still know their source so self-grace checks work.
	SpawnLocation = GetActorLocation();
	if (SpawnTimeSeconds <= 0.0)
	{
		if (const UWorld* World = GetWorld())
		{
			SpawnTimeSeconds = World->GetTimeSeconds();
		}
	}
	if (!SourceActor.IsValid())
	{
		if (AActor* OwnerActor = GetOwner())
		{
			SourceActor = OwnerActor;
		}
		else if (APawn* InstigatorPawn = GetInstigator())
		{
			SourceActor = InstigatorPawn;
		}

		if (SourceActor.IsValid())
		{
			CachedSourceTags.Reset();
			for (const FName& Tag : SourceActor->Tags)
			{
				if (Tag != NAME_None)
				{
					CachedSourceTags.AddUnique(Tag);
				}
			}

			if (CollisionComponent)
			{
				CollisionComponent->IgnoreActorWhenMoving(SourceActor.Get(), /*bShouldIgnore=*/true);
			}
			// UE_LOG(LogRangedProjectile,
			//        Log,
			//        TEXT("BeginPlay: %s resolved SourceActor=%s Owner=%s Instigator=%s Authority=%s"),
			//        *GetName(),
			//        *GetNameSafe(SourceActor.Get()),
			//        *GetNameSafe(GetOwner()),
			//        *GetNameSafe(GetInstigator()),
			//        HasAuthority() ? TEXT("Yes") : TEXT("No"));
		}
	}
	else
	{
		// UE_LOG(LogRangedProjectile,
		//        Log,
		//        TEXT("BeginPlay: %s already had SourceActor=%s Owner=%s Instigator=%s Authority=%s"),
		//        *GetName(),
		//        *GetNameSafe(SourceActor.Get()),
		//        *GetNameSafe(GetOwner()),
		//        *GetNameSafe(GetInstigator()),
		//        HasAuthority() ? TEXT("Yes") : TEXT("No"));
	}

	ApplyCollisionResponseOverrides();
	// If we missed Initialize (e.g., client side), ensure the world-block restore timer is set.
	SuppressWorldCollisionForGrace();
}

void AAeyerjiProjectile_RangedBasic::Destroyed()
{
	Super::Destroyed();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorldCollisionRestoreHandle);
	}

	if (!bImpactProcessed)
	{
		OnProjectileExpired.Broadcast();
	}
}

void AAeyerjiProjectile_RangedBasic::HandleImpact(AActor* OtherActor, const FHitResult& Hit)
{
	const bool bIsAuthority = HasAuthority();

	if (bImpactProcessed && bIsAuthority)
	{
		return;
	}

	if (!OtherActor)
	{
		if (bIsAuthority)
		{
			bImpactProcessed = true;
			OnProjectileExpired.Broadcast();
			Destroy();
		}
		return;
	}

	if (OtherActor == GetInstigator() || OtherActor == GetOwner())
	{
		// Friend-or-self hit: keep ignoring so the projectile can pass through.
		//UE_LOG(LogRangedProjectile, Display, TEXT("Projectile ignoring instigator/owner %s"), *GetNameSafe(OtherActor));
		// UE_LOG(LogRangedProjectile,
		//        Log,
		//        TEXT("HandleImpact: ignoring instigator/owner %s Projectile=%s"),
		//        *GetNameSafe(OtherActor),
		//        *GetNameSafe(this));
		IgnoreActorAndResumeFlight(OtherActor, Hit);
		return;
	}

	const AActor* SourceForTeamCheck = GetSourceActorForTeamCheck();

	// Extra grace in case we spawn inside/near the shooter (scaled-up elites).
	if (SelfCollisionGraceDistance > 0.f)
	{
		const float DistSqFromSpawn = FVector::DistSquared(SpawnLocation, GetActorLocation());
		if (DistSqFromSpawn <= FMath::Square(SelfCollisionGraceDistance))
		{
			if (OtherActor == SourceActor.Get() || OtherActor == GetInstigator() || OtherActor == GetOwner() || SharesTagWithSource(OtherActor))
			{
				// UE_LOG(LogRangedProjectile,
				//        Log,
				//        TEXT("HandleImpact: grace-distance ignore Actor=%s Projectile=%s"),
				//        *GetNameSafe(OtherActor),
				//        *GetNameSafe(this));
				IgnoreActorAndResumeFlight(OtherActor, Hit);
				return;
			}
			if (SourceForTeamCheck && AbilityTeamUtils::AreOnSameTeam(SourceForTeamCheck, OtherActor))
			{
				// UE_LOG(LogRangedProjectile,
				//        Log,
				//        TEXT("HandleImpact: grace-distance same-team ignore Actor=%s Projectile=%s"),
				//        *GetNameSafe(OtherActor),
				//        *GetNameSafe(this));
				IgnoreActorAndResumeFlight(OtherActor, Hit);
				return;
			}
		}
	}

	// Grace for world geometry when spawned very close to it (e.g., muzzle inside floor/wall).
	if (SelfCollisionGraceDistance > 0.f)
	{
		const float DistSqFromSpawn = FVector::DistSquared(SpawnLocation, GetActorLocation());
		if (DistSqFromSpawn <= FMath::Square(SelfCollisionGraceDistance))
		{
			ECollisionChannel HitChannel = ECC_OverlapAll_Deprecated;
			if (Hit.Component.IsValid())
			{
				HitChannel = Hit.Component->GetCollisionObjectType();
			}

			if (HitChannel == ECC_WorldStatic || HitChannel == ECC_WorldDynamic)
			{
				// UE_LOG(LogRangedProjectile,
				//        Log,
				//        TEXT("HandleImpact: grace-distance ignore world geometry Channel=%d Actor=%s Projectile=%s"),
				//        static_cast<int32>(HitChannel),
				//        *GetNameSafe(OtherActor),
				//        *GetNameSafe(this));
				SuppressWorldCollisionForGrace();
				IgnoreActorAndResumeFlight(OtherActor, Hit);
				return;
			}
		}
	}

	if (!bIsAuthority)
	{
		// Keep simulated proxies moving visually; authority handles real impacts.
		if (Hit.bBlockingHit)
		{
			IgnoreActorAndResumeFlight(OtherActor, Hit);
		}
		return;
	}

	if (SharesTagWithSource(OtherActor))
	{
		// UE_LOG(LogRangedProjectile,
		//        Log,
		//        TEXT("HandleImpact: shared-tag ignore Actor=%s Projectile=%s"),
		//        *GetNameSafe(OtherActor),
		//        *GetNameSafe(this));
		IgnoreActorAndResumeFlight(OtherActor, Hit);
		return;
	}

	if (SourceForTeamCheck && AbilityTeamUtils::AreOnSameTeam(SourceForTeamCheck, OtherActor))
	{
		// Same-team actors should not block the projectile path either.
		const FGenericTeamId SourceTeam = AbilityTeamUtils::ResolveTeamId(SourceForTeamCheck);
		const FGenericTeamId OtherTeam = AbilityTeamUtils::ResolveTeamId(OtherActor);
		// UE_LOG(LogRangedProjectile,
		//        Display,
		//        TEXT("Projectile ignoring same-team actor %s (Source=%s SourceTeam=%d OtherTeam=%d)"),
		//        *GetNameSafe(OtherActor),
		//        *GetNameSafe(SourceForTeamCheck),
		//        SourceTeam.GetId(),
		//        OtherTeam.GetId());
		IgnoreActorAndResumeFlight(OtherActor, Hit);
		return;
	}

	if (OtherActor)
	{
		const FGenericTeamId SourceTeam = AbilityTeamUtils::ResolveTeamId(SourceForTeamCheck);
		const FGenericTeamId OtherTeam = AbilityTeamUtils::ResolveTeamId(OtherActor);
		// UE_LOG(LogRangedProjectile,
		//        Display,
		//        TEXT("Projectile impacting actor %s (Source=%s SourceTeam=%d OtherTeam=%d)"),
		//        *GetNameSafe(OtherActor),
		//        *GetNameSafe(SourceForTeamCheck),
		//        SourceTeam.GetId(),
		//        OtherTeam.GetId());
	}

	bImpactProcessed = true;

	OnProjectileImpact.Broadcast(OtherActor, Hit);
	OnProjectileExpired.Broadcast();

	// UE_LOG(LogRangedProjectile,
	//        Log,
	//        TEXT("HandleImpact: impact processed Actor=%s Projectile=%s Blocking=%s Location=%s Normal=%s"),
	//        *GetNameSafe(OtherActor),
	//        *GetNameSafe(this),
	//        Hit.bBlockingHit ? TEXT("Yes") : TEXT("No"),
	//        *Hit.ImpactPoint.ToString(),
	//        *Hit.ImpactNormal.ToString());

	Destroy();
}

void AAeyerjiProjectile_RangedBasic::OnCollisionBeginOverlap(UPrimitiveComponent* OverlappedComponent,
                                                             AActor* OtherActor,
                                                             UPrimitiveComponent* OtherComp,
                                                             int32 /*OtherBodyIndex*/,
                                                             bool bFromSweep,
                                                             const FHitResult& SweepResult)
{
	// UE_LOG(LogRangedProjectile,
	//        Log,
	//        TEXT("OnOverlap: Projectile=%s OtherActor=%s Comp=%s bFromSweep=%s Blocking=%s Location=%s Normal=%s ActorLoc=%s Velocity=%s Age=%.3f"),
	//        *GetNameSafe(this),
	//        *GetNameSafe(OtherActor),
	//        *GetNameSafe(OtherComp),
	//        bFromSweep ? TEXT("Yes") : TEXT("No"),
	//        SweepResult.bBlockingHit ? TEXT("Yes") : TEXT("No"),
	//        *SweepResult.ImpactPoint.ToString(),
	//        *SweepResult.ImpactNormal.ToString(),
	//        *GetActorLocation().ToString(),
	//        ProjectileMovement ? *ProjectileMovement->Velocity.ToString() : TEXT("None"),
	//        (GetWorld() && SpawnTimeSeconds > 0.0) ? (GetWorld()->GetTimeSeconds() - SpawnTimeSeconds) : -1.0);

	if (!bFromSweep)
	{
		// Convert a boxed overlap into a pseudo-hit so downstream logic runs consistently.
		FHitResult Temp = SweepResult;
		Temp.Component = OtherComp;
		HandleImpact(OtherActor, Temp);
		return;
	}

	HandleImpact(OtherActor, SweepResult);
}

void AAeyerjiProjectile_RangedBasic::OnCollisionHit(UPrimitiveComponent* /*HitComponent*/,
                                                    AActor* OtherActor,
                                                    UPrimitiveComponent* OtherComp,
                                                    FVector /*NormalImpulse*/,
                                                    const FHitResult& Hit)
{
	// UE_LOG(LogRangedProjectile,
	//        Log,
	//        TEXT("OnHit: Projectile=%s OtherActor=%s Comp=%s Blocking=%s Location=%s Normal=%s ActorLoc=%s Velocity=%s Age=%.3f"),
	//        *GetNameSafe(this),
	//        *GetNameSafe(OtherActor),
	//        *GetNameSafe(OtherComp),
	//        Hit.bBlockingHit ? TEXT("Yes") : TEXT("No"),
	//        *Hit.ImpactPoint.ToString(),
	//        *Hit.ImpactNormal.ToString(),
	//        *GetActorLocation().ToString(),
	//        ProjectileMovement ? *ProjectileMovement->Velocity.ToString() : TEXT("None"),
	//        (GetWorld() && SpawnTimeSeconds > 0.0) ? (GetWorld()->GetTimeSeconds() - SpawnTimeSeconds) : -1.0);

	HandleImpact(OtherActor, Hit);
}

void AAeyerjiProjectile_RangedBasic::IgnoreActorAndResumeFlight(AActor* ActorToIgnore, const FHitResult& Hit)
{
	if (CollisionComponent && ActorToIgnore)
	{
		CollisionComponent->IgnoreActorWhenMoving(ActorToIgnore, /*bShouldIgnore=*/true);
		CollisionComponent->MoveIgnoreActors.AddUnique(ActorToIgnore);

		if (Hit.Component.IsValid())
		{
			CollisionComponent->IgnoreComponentWhenMoving(Hit.Component.Get(), /*bShouldIgnore=*/true);
		}
		if (Hit.GetComponent())
		{
			CollisionComponent->IgnoreComponentWhenMoving(Hit.GetComponent(), /*bShouldIgnore=*/true);
		}
	}

	if (!ProjectileMovement)
	{
		return;
	}

	if (!Hit.bBlockingHit)
	{
		return;
	}

	// Recover a forward direction either from the existing velocity, trace data, or actor forward vector.
	FVector ResumeDirection = ProjectileMovement->Velocity.GetSafeNormal();
	if (ResumeDirection.IsNearlyZero() && Hit.TraceStart != Hit.TraceEnd)
	{
		ResumeDirection = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();
	}
	if (ResumeDirection.IsNearlyZero())
	{
		ResumeDirection = GetActorForwardVector();
	}

	if (ResumeDirection.IsNearlyZero())
	{
		return;
	}

	const float ResumeSpeed = ProjectileMovement->Velocity.IsNearlyZero()
		? FMath::Max(ProjectileMovement->InitialSpeed, 1.f)
		: ProjectileMovement->Velocity.Size();

	// Nudge the projectile slightly forward so it is no longer penetrating the ignored actor.
	const FVector TeleportOffset = ResumeDirection * FMath::Max(CollisionComponent ? CollisionComponent->GetScaledSphereRadius() : 16.f, 4.f);
	SetActorLocation(GetActorLocation() + TeleportOffset, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	if (ProjectileMovement->UpdatedComponent != CollisionComponent)
	{
		ProjectileMovement->SetUpdatedComponent(CollisionComponent);
	}

	ProjectileMovement->SetComponentTickEnabled(true);
	ProjectileMovement->Velocity = ResumeDirection * ResumeSpeed;
	ProjectileMovement->UpdateComponentVelocity();
	ProjectileMovement->Activate(true);
}

void AAeyerjiProjectile_RangedBasic::SuppressWorldCollisionForGrace(float OverrideDelaySeconds)
{
	if (bWorldCollisionSuppressed || !CollisionComponent)
	{
		return;
	}

	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	bWorldCollisionSuppressed = true;

	if (UWorld* World = GetWorld())
	{
		// Restore after a short window so we don't tunnel through far geometry.
		const float SpeedForCalc = FMath::Max(ProjectileMovement ? ProjectileMovement->InitialSpeed : 800.f, 1.f);
		const float AutoDelay = FMath::Clamp(SelfCollisionGraceDistance / SpeedForCalc, 0.05f, 0.25f);
		const float RestoreDelay = (OverrideDelaySeconds > 0.f) ? OverrideDelaySeconds : AutoDelay;
		World->GetTimerManager().SetTimer(WorldCollisionRestoreHandle, this, &AAeyerjiProjectile_RangedBasic::RestoreWorldCollision, RestoreDelay, /*bLoop=*/false);
	}
}

void AAeyerjiProjectile_RangedBasic::RestoreWorldCollision()
{
	if (!CollisionComponent)
	{
		return;
	}

	// Keep world channels overlapping (matches working setup) and reapply our pawn overlaps.
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Overlap); // PawnCustom
	bWorldCollisionSuppressed = false;
}

const AActor* AAeyerjiProjectile_RangedBasic::GetSourceActorForTeamCheck() const
{
	if (SourceActor.IsValid())
	{
		return SourceActor.Get();
	}

	if (const AActor* InstigatorActor = GetInstigator())
	{
		return InstigatorActor;
	}

	return GetOwner();
}

bool AAeyerjiProjectile_RangedBasic::SharesTagWithSource(const AActor* OtherActor) const
{
	if (!OtherActor)
	{
		return false;
	}

	for (const FName& Tag : CachedSourceTags)
	{
		if (Tag != NAME_None && OtherActor->ActorHasTag(Tag))
		{
			return true;
		}
	}

	return false;
}

void AAeyerjiProjectile_RangedBasic::ApplyCollisionResponseOverrides() const
{
	if (!CollisionComponent)
	{
		return;
	}

	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// Overlap world so spawning inside geometry (or skimming large meshes) does not block flight.
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComponent->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Overlap); // PawnCustom
}
