#include "Abilities/GravitonPull/GA_AGGravitonPull.h"

#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyParentNative.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/RootMotionSource.h"

namespace AeyerjiGravitonPullTags
{
	constexpr float PullDurationSeconds = 0.2f;
	constexpr float MinimumPullDistance = 1.f;
	constexpr float SeparationPadding = 20.f;
	constexpr uint16 RootMotionPriority = 1000;
	const FName RootMotionInstanceName(TEXT("Aeyerji.GravitonPull"));

	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"));
		return Tag;
	}

	const FGameplayTag& PullDistanceTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			TEXT("Ability.Param.Pull.Distance"),
			/*ErrorIfNotFound=*/false);
		return Tag;
	}

	const FGameplayTag& HeavyTargetScaleTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			TEXT("Ability.Param.Pull.HeavyTargetScale"),
			/*ErrorIfNotFound=*/false);
		return Tag;
	}

	bool IsHeavyTarget(const AActor& Target)
	{
		const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(&Target);
		if (!Enemy)
		{
			return false;
		}

		const FGameplayTag SourceTag = Enemy->GetScalingSourceTag();
		return SourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_Elite)
			|| SourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_MiniBoss)
			|| SourceTag.MatchesTagExact(AeyerjiTags::Loot_Source_Boss);
	}
}

UGA_AGGravitonPull::UGA_AGGravitonPull()
{
	AbilityTag = AeyerjiGravitonPullTags::AbilityTag();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AbilityTag);
	SetAssetTags(AssetTags);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_AGGravitonPull::OnTargetedAbilityAppliedNative(
	const FGameplayAbilityActorInfo& ActorInfo,
	const FAeyerjiAbilityResolvedConfig& Config,
	const TArray<AActor*>& Targets,
	const FVector TargetLocation) const
{
	Super::OnTargetedAbilityAppliedNative(ActorInfo, Config, Targets, TargetLocation);

	ACharacter* SourceCharacter = Cast<ACharacter>(ActorInfo.AvatarActor.Get());
	if (!SourceCharacter || !SourceCharacter->HasAuthority())
	{
		return;
	}

	float PullDistance = 0.f;
	if (!AeyerjiGravitonPullTags::PullDistanceTag().IsValid()
		|| !Config.TryGetFloatTunable(AeyerjiGravitonPullTags::PullDistanceTag(), PullDistance)
		|| !FMath::IsFinite(PullDistance)
		|| PullDistance <= 0.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("GravitonPull: %s has no valid Pull.Distance tunable."),
			*Config.AbilityTag.ToString());
		return;
	}

	float HeavyTargetScale = 1.f;
	if (AeyerjiGravitonPullTags::HeavyTargetScaleTag().IsValid())
	{
		Config.TryGetFloatTunable(AeyerjiGravitonPullTags::HeavyTargetScaleTag(), HeavyTargetScale);
	}
	HeavyTargetScale = FMath::IsFinite(HeavyTargetScale)
		? FMath::Clamp(HeavyTargetScale, 0.f, 1.f)
		: 1.f;

	const FVector SourceLocation = SourceCharacter->GetActorLocation();
	const float SourceRadius = FMath::Max(0.f, SourceCharacter->GetSimpleCollisionRadius());
	int32 PulledTargetCount = 0;

	for (AActor* Target : Targets)
	{
		ACharacter* TargetCharacter = Cast<ACharacter>(Target);
		if (!TargetCharacter
			|| TargetCharacter == SourceCharacter
			|| !TargetCharacter->HasAuthority()
			|| TargetCharacter->GetWorld() != SourceCharacter->GetWorld())
		{
			continue;
		}

		UCharacterMovementComponent* TargetMovement = TargetCharacter->GetCharacterMovement();
		if (!TargetMovement || !TargetMovement->UpdatedComponent)
		{
			continue;
		}

		const FVector StartLocation = TargetCharacter->GetActorLocation();
		FVector DirectionToSource = SourceLocation - StartLocation;
		DirectionToSource.Z = 0.f;
		const float DistanceToSource = DirectionToSource.Size();
		if (!FMath::IsFinite(DistanceToSource) || DistanceToSource <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float TargetRadius = FMath::Max(0.f, TargetCharacter->GetSimpleCollisionRadius());
		const float MinimumSeparation = SourceRadius + TargetRadius + AeyerjiGravitonPullTags::SeparationPadding;
		const float AvailablePullDistance = FMath::Max(0.f, DistanceToSource - MinimumSeparation);
		const float TargetScale = AeyerjiGravitonPullTags::IsHeavyTarget(*TargetCharacter)
			? HeavyTargetScale
			: 1.f;
		const float AppliedPullDistance = FMath::Min(PullDistance * TargetScale, AvailablePullDistance);
		if (AppliedPullDistance < AeyerjiGravitonPullTags::MinimumPullDistance)
		{
			continue;
		}

		if (AController* TargetController = TargetCharacter->GetController())
		{
			TargetController->StopMovement();
		}
		TargetMovement->StopMovementImmediately();
		TargetMovement->RemoveRootMotionSource(AeyerjiGravitonPullTags::RootMotionInstanceName);

		TSharedPtr<FRootMotionSource_MoveToForce> PullSource = MakeShared<FRootMotionSource_MoveToForce>();
		PullSource->InstanceName = AeyerjiGravitonPullTags::RootMotionInstanceName;
		PullSource->AccumulateMode = ERootMotionAccumulateMode::Override;
		PullSource->Priority = AeyerjiGravitonPullTags::RootMotionPriority;
		PullSource->StartLocation = StartLocation;
		PullSource->TargetLocation = StartLocation + DirectionToSource.GetSafeNormal() * AppliedPullDistance;
		PullSource->Duration = AeyerjiGravitonPullTags::PullDurationSeconds;
		PullSource->bRestrictSpeedToExpected = true;
		PullSource->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
		PullSource->FinishVelocityParams.SetVelocity = FVector::ZeroVector;

		const uint16 RootMotionSourceId = TargetMovement->ApplyRootMotionSource(PullSource);
		if (RootMotionSourceId == static_cast<uint16>(ERootMotionSourceID::Invalid))
		{
			UE_LOG(LogTemp, Warning, TEXT("GravitonPull: failed to apply pull movement to %s."),
				*GetNameSafe(TargetCharacter));
			continue;
		}

		TargetCharacter->ForceNetUpdate();
		++PulledTargetCount;
	}

	UE_LOG(LogTemp, Display, TEXT("GravitonPull: pulled %d/%d targets toward %s (Distance=%.1f HeavyScale=%.2f)."),
		PulledTargetCount,
		Targets.Num(),
		*GetNameSafe(SourceCharacter),
		PullDistance,
		HeavyTargetScale);
}
