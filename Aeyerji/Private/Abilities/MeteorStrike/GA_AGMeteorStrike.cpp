#include "Abilities/MeteorStrike/GA_AGMeteorStrike.h"

#include "Abilities/MeteorStrike/AeyerjiMeteorStrike.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "UObject/UObjectGlobals.h"

namespace AeyerjiMeteorStrikeTags
{
	const FGameplayTag& AbilityTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.MeteorStrike"));
		return Tag;
	}

	const FGameplayTag& SpawnHeightTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			TEXT("Ability.Param.Meteor.SpawnHeight"),
			/*ErrorIfNotFound=*/false);
		return Tag;
	}

	const FGameplayTag& ActorClassTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			TEXT("Ability.Param.Meteor.ActorClass"),
			/*ErrorIfNotFound=*/false);
		return Tag;
	}

	TSubclassOf<AAeyerjiMeteorStrike> ResolveMeteorClass(const FAeyerjiAbilityResolvedConfig& Config)
	{
		FSoftObjectPath AssetPath;
		if (ActorClassTag().IsValid() && Config.TryGetAssetTunable(ActorClassTag(), AssetPath)
			&& AssetPath.IsValid())
		{
			TSoftClassPtr<AActor> SoftClass(AssetPath);
			if (UClass* Loaded = SoftClass.Get())
			{
				if (Loaded->IsChildOf(AAeyerjiMeteorStrike::StaticClass()))
				{
					return Loaded;
				}
			}
			else if (UClass* SyncLoaded = SoftClass.LoadSynchronous())
			{
				if (SyncLoaded->IsChildOf(AAeyerjiMeteorStrike::StaticClass()))
				{
					return SyncLoaded;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("MeteorStrike: asset tunable %s is not a meteor actor, using native fallback."),
				*AssetPath.ToString());
		}
		return AAeyerjiMeteorStrike::StaticClass();
	}
}

UGA_AGMeteorStrike::UGA_AGMeteorStrike()
{
	AbilityTag = AeyerjiMeteorStrikeTags::AbilityTag();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(AbilityTag);
	SetAssetTags(AssetTags);

	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_AGMeteorStrike::OnTargetedAbilityCastStartedNative(
	const FGameplayAbilityActorInfo& ActorInfo,
	const FAeyerjiAbilityResolvedConfig& Config,
	FVector TargetLocation,
	float ImpactDelaySeconds) const
{
	Super::OnTargetedAbilityCastStartedNative(ActorInfo, Config, TargetLocation, ImpactDelaySeconds);

	AActor* Avatar = ActorInfo.AvatarActor.Get();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World || !Avatar->HasAuthority() || TargetLocation.ContainsNaN())
	{
		return;
	}

	float SpawnHeight = AAeyerjiMeteorStrike::DefaultSpawnHeight;
	if (AeyerjiMeteorStrikeTags::SpawnHeightTag().IsValid())
	{
		Config.TryGetFloatTunable(AeyerjiMeteorStrikeTags::SpawnHeightTag(), SpawnHeight);
	}

	FActorSpawnParameters Spawn;
	Spawn.Owner = Avatar;
	Spawn.Instigator = Cast<APawn>(Avatar);
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAeyerjiMeteorStrike* Meteor = World->SpawnActor<AAeyerjiMeteorStrike>(
		AeyerjiMeteorStrikeTags::ResolveMeteorClass(Config),
		AAeyerjiMeteorStrike::ComputeSpawnLocation(TargetLocation, SpawnHeight),
		FRotator::ZeroRotator,
		Spawn);
	if (!Meteor)
	{
		UE_LOG(LogTemp, Warning, TEXT("MeteorStrike: failed to spawn meteor visual for %s."),
			*Config.AbilityTag.ToString());
		return;
	}
	if (!Meteor->InitializeMeteor(TargetLocation, ImpactDelaySeconds, SpawnHeight))
	{
		Meteor->Destroy();
		return;
	}
	ActiveMeteor = Meteor;
}

void UGA_AGMeteorStrike::OnTargetedAbilityAppliedNative(
	const FGameplayAbilityActorInfo& ActorInfo,
	const FAeyerjiAbilityResolvedConfig& Config,
	const TArray<AActor*>& Targets,
	FVector TargetLocation) const
{
	Super::OnTargetedAbilityAppliedNative(ActorInfo, Config, Targets, TargetLocation);

	// Damage was just applied by the base; land the meteor on the same callstack so the
	// fracture presentation cannot drift from the gameplay impact.
	if (AAeyerjiMeteorStrike* Meteor = ActiveMeteor.Get())
	{
		Meteor->ResolveImpact();
	}
}

void UGA_AGMeteorStrike::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// Ending before impact means the base never committed or damaged: dismiss the visual
	// without a fracture. After impact the meteor is already resolved, so this is a no-op.
	if (AAeyerjiMeteorStrike* Meteor = ActiveMeteor.Get())
	{
		Meteor->CancelMeteor();
	}
	ActiveMeteor.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
