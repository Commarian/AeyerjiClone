// AeyerjiEnemyArchetypeComponent.cpp
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Enemy/AeyerjiEnemyArchetypeData.h"
#include "Enemy/AeyerjiEnemyArchetypeLibrary.h"
#include "Enemy/AeyerjiEnemyTraitComponent.h"
#include "Enemy/EnemyParentNative.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
constexpr int32 MaxArchetypeRuntimeEntries = 64;

bool CanMutateArchetypeSource(const AActor* Owner)
{
	const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	return Owner && (!World || !World->IsGameWorld() || Owner->HasAuthority());
}

template <typename TArchetypeData>
void ValidateArchetypeInternal(bool& bValidated, const TArchetypeData& Data, const FString& DebugName)
{
	if (bValidated)
	{
		return;
	}

	bValidated = true;

	if (Data.bWarnIfMissingAttackMontage && Data.AttackMontage.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype %s has no AttackMontage"), *DebugName);
	}

	if (Data.bWarnIfMissingInitEffects && Data.InitGameplayEffects.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype %s has no InitGameplayEffects"), *DebugName);
	}

	if (Data.bWarnIfMissingGrantedTags && Data.GrantedTags.IsEmpty() && !Data.ArchetypeTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype %s has no GrantedTags"), *DebugName);
	}

	if (Data.CapsuleOverrides.bOverrideCapsuleSize
		&& Data.CapsuleOverrides.CapsuleHalfHeight < Data.CapsuleOverrides.CapsuleRadius)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Enemy archetype %s capsule half-height %.2f is smaller than radius %.2f; half-height will be raised to the radius."),
			*DebugName,
			Data.CapsuleOverrides.CapsuleHalfHeight,
			Data.CapsuleOverrides.CapsuleRadius);
	}
}

template <typename TArchetypeData>
void ApplyAttributeDefaultsInternal(const TArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	if (Data.AttributeDefaultsTable.IsNull())
	{
		return;
	}

	UDataTable* DefaultsTable = Data.AttributeDefaultsTable.LoadSynchronous();
	if (!DefaultsTable)
	{
		return;
	}

	UClass* AttributeSetClass = Data.AttributeSetClass ? Data.AttributeSetClass.Get() : UAeyerjiAttributeSet::StaticClass();
	if (!AttributeSetClass)
	{
		return;
	}

	ASC.InitStats(AttributeSetClass, DefaultsTable);
}

template <typename TArchetypeData>
void ApplyGrantedTagsInternal(const TArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	if (Data.ArchetypeTag.IsValid())
	{
		// Archetype identity drives client presentation such as elite/boss health chunks.
		ASC.AddLooseGameplayTag(
			Data.ArchetypeTag,
			1,
			EGameplayTagReplicationState::TagOnly);
	}

	if (!Data.GrantedTags.IsEmpty())
	{
		ASC.AddLooseGameplayTags(
			Data.GrantedTags,
			1,
			EGameplayTagReplicationState::TagOnly);
	}
}

template <typename TArchetypeData>
void ApplyTeamOverridesInternal(const TArchetypeData& Data, AEnemyParentNative* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	if (Data.bOverrideTeamId)
	{
		Enemy->SetGenericTeamId(FGenericTeamId(Data.TeamIdOverride));
	}

	if (Data.bOverrideTeamTag && Data.TeamTagOverride.IsValid())
	{
		Enemy->SetActiveTeamTag(Data.TeamTagOverride);
	}
}

template <typename TArchetypeData>
void GrantAbilitiesInternal(const TArchetypeData& Data, UAbilitySystemComponent& ASC, AActor* Owner)
{
	if (Data.GrantedAbilities.IsEmpty())
	{
		return;
	}

	const int32 ClampedLevel = FMath::Clamp(Data.AbilityLevel, 1, 1000);
	const int32 AbilityCount = FMath::Min(Data.GrantedAbilities.Num(), MaxArchetypeRuntimeEntries);
	for (int32 Index = 0; Index < AbilityCount; ++Index)
	{
		const TSubclassOf<UGameplayAbility>& AbilityClass = Data.GrantedAbilities[Index];
		if (!*AbilityClass)
		{
			continue;
		}

		if (ASC.FindAbilitySpecFromClass(AbilityClass))
		{
			continue;
		}

		ASC.GiveAbility(FGameplayAbilitySpec(AbilityClass, ClampedLevel, INDEX_NONE, Owner));
	}
}

template <typename TArchetypeData>
void ApplyInitEffectsInternal(const TArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	if (Data.InitGameplayEffects.IsEmpty())
	{
		return;
	}

	const float ClampedLevel = FMath::IsFinite(Data.EffectLevel)
		? FMath::Clamp(Data.EffectLevel, 0.01f, 1000.f)
		: 1.f;
	const int32 EffectCount = FMath::Min(Data.InitGameplayEffects.Num(), MaxArchetypeRuntimeEntries);
	for (int32 Index = 0; Index < EffectCount; ++Index)
	{
		const TSubclassOf<UGameplayEffect>& EffectClass = Data.InitGameplayEffects[Index];
		if (!EffectClass)
		{
			continue;
		}

		const FGameplayEffectContextHandle Context = ASC.MakeEffectContext();
		FGameplayEffectSpecHandle Spec = ASC.MakeOutgoingSpec(EffectClass, ClampedLevel, Context);
		if (Spec.IsValid())
		{
			ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
}

void SyncResourceValuesToCurrentMax(UAbilitySystemComponent& ASC)
{
	const FGameplayAttribute HPAttr = UAeyerjiAttributeSet::GetHPAttribute();
	const FGameplayAttribute HPMaxAttr = UAeyerjiAttributeSet::GetHPMaxAttribute();
	const FGameplayAttribute ManaAttr = UAeyerjiAttributeSet::GetManaAttribute();
	const FGameplayAttribute ManaMaxAttr = UAeyerjiAttributeSet::GetManaMaxAttribute();

	if (HPAttr.IsValid() && HPMaxAttr.IsValid())
	{
		const float RawMaxHP = ASC.GetNumericAttribute(HPMaxAttr);
		const float MaxHP = FMath::IsFinite(RawMaxHP) ? FMath::Max(0.f, RawMaxHP) : 0.f;
		ASC.SetNumericAttributeBase(HPAttr, MaxHP);
	}

	if (ManaAttr.IsValid() && ManaMaxAttr.IsValid())
	{
		const float RawMaxMana = ASC.GetNumericAttribute(ManaMaxAttr);
		const float MaxMana = FMath::IsFinite(RawMaxMana) ? FMath::Max(0.f, RawMaxMana) : 0.f;
		ASC.SetNumericAttributeBase(ManaAttr, MaxMana);
	}
}

template <typename TArchetypeData>
void AddTraitComponentsInternal(const TArchetypeData& Data, AActor* Owner)
{
	if (Data.TraitComponents.IsEmpty())
	{
		return;
	}

	if (!Owner)
	{
		return;
	}

	const int32 TraitCount = FMath::Min(Data.TraitComponents.Num(), MaxArchetypeRuntimeEntries);
	for (int32 Index = 0; Index < TraitCount; ++Index)
	{
		const TSubclassOf<UAeyerjiEnemyTraitComponent>& TraitClass = Data.TraitComponents[Index];
		if (!*TraitClass)
		{
			continue;
		}

		if (Owner->GetComponentByClass(TraitClass))
		{
			continue;
		}

		UAeyerjiEnemyTraitComponent* NewTrait = NewObject<UAeyerjiEnemyTraitComponent>(Owner, TraitClass);
		if (!NewTrait)
		{
			continue;
		}

		Owner->AddInstanceComponent(NewTrait);
		NewTrait->OnComponentCreated();
		NewTrait->RegisterComponent();
	}
}

template <typename TArchetypeData>
void ApplyMeshOverridesInternal(const TArchetypeData& Data, AEnemyParentNative* Enemy)
{
	const FAeyerjiEnemyMeshOverrides& MeshOverrides = Data.MeshOverrides;
	if (MeshOverrides.SkeletalMesh.IsNull() && MeshOverrides.AnimClass == nullptr
		&& MeshOverrides.MaterialOverrides.IsEmpty() && !MeshOverrides.bOverrideRelativeTransform)
	{
		return;
	}

	if (!Enemy)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = Enemy->GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (!MeshOverrides.SkeletalMesh.IsNull())
	{
		if (USkeletalMesh* MeshAsset = MeshOverrides.SkeletalMesh.LoadSynchronous())
		{
			MeshComp->SetSkinnedAssetAndUpdate(MeshAsset);
		}
	}

	if (MeshOverrides.AnimClass)
	{
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComp->SetAnimInstanceClass(MeshOverrides.AnimClass);
	}

	if (MeshOverrides.bOverrideRelativeTransform && MeshOverrides.RelativeTransform.IsValid())
	{
		MeshComp->SetRelativeTransform(MeshOverrides.RelativeTransform);
	}

	if (!MeshOverrides.MaterialOverrides.IsEmpty())
	{
		const int32 MaxIndex = FMath::Min3(
			MeshOverrides.MaterialOverrides.Num(),
			MeshComp->GetNumMaterials(),
			MaxArchetypeRuntimeEntries);
		for (int32 Index = 0; Index < MaxIndex; ++Index)
		{
			if (MeshOverrides.MaterialOverrides[Index].IsNull())
			{
				continue;
			}

			if (UMaterialInterface* Material = MeshOverrides.MaterialOverrides[Index].LoadSynchronous())
			{
				MeshComp->SetMaterial(Index, Material);
			}
		}
	}
}

template <typename TArchetypeData>
void ApplyCapsuleOverridesInternal(const TArchetypeData& Data, AEnemyParentNative* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	const FAeyerjiEnemyCapsuleOverrides& CapsuleOverrides = Data.CapsuleOverrides;
	if (!CapsuleOverrides.bOverrideCapsuleSize)
	{
		Enemy->ClearArchetypeCollisionCapsuleSize();
		return;
	}

	Enemy->SetArchetypeCollisionCapsuleSize(
		CapsuleOverrides.CapsuleRadius,
		CapsuleOverrides.CapsuleHalfHeight);
}
} // namespace

UAeyerjiEnemyArchetypeComponent::UAeyerjiEnemyArchetypeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UAeyerjiEnemyArchetypeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoApplyOnBeginPlay)
	{
		ApplyArchetype();
	}
}

void UAeyerjiEnemyArchetypeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAeyerjiEnemyArchetypeComponent, ArchetypeData);
	DOREPLIFETIME(UAeyerjiEnemyArchetypeComponent, ArchetypeLibrary);
	DOREPLIFETIME(UAeyerjiEnemyArchetypeComponent, ArchetypeTag);
}

void UAeyerjiEnemyArchetypeComponent::OnRep_ArchetypeSource()
{
	bApplied = false;
	bValidated = false;
	bVisualsApplied = false;
	bLoggedInvalidLibraryTag = false;
	bLoggedMissingLibraryEntry = false;
	bLoggedBothSources = false;

	ApplyArchetypeVisuals(/*bAllowInEditor=*/false, /*bForce=*/true);
}

void UAeyerjiEnemyArchetypeComponent::ApplyArchetype()
{
	if (bApplied)
	{
		return;
	}

	if (!bLoggedBothSources && !ArchetypeData.IsNull() && !ArchetypeLibrary.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype: both ArchetypeData and ArchetypeLibrary are set on %s; using library if tag resolves."), *GetNameSafe(GetOwner()));
		bLoggedBothSources = true;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const UWorld* World = Owner->GetWorld();
	if (!World || !(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return;
	}

	if (!Owner->HasAuthority())
	{
		return;
	}

	const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(true);
	const UAeyerjiEnemyArchetypeData* DataAsset = LibraryEntry ? nullptr : ResolveArchetypeData(true);
	if (!LibraryEntry && !DataAsset)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner, /*LookForComponent*/ true);
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype skipped: no ASC on %s"), *GetNameSafe(Owner));
		return;
	}

	if (LibraryEntry)
	{
		ValidateArchetype(*LibraryEntry);
	}
	else
	{
		ValidateArchetype(*DataAsset);
	}

	bApplied = true;

	if (LibraryEntry)
	{
		ApplyAttributeDefaults(*LibraryEntry, *ASC);
		ApplyGrantedTags(*LibraryEntry, *ASC);
		ApplyTeamOverrides(*LibraryEntry, *ASC);
		GrantAbilities(*LibraryEntry, *ASC);
		ApplyInitEffects(*LibraryEntry, *ASC);
		AddTraitComponents(*LibraryEntry);
	}
	else
	{
		ApplyAttributeDefaults(*DataAsset, *ASC);
		ApplyGrantedTags(*DataAsset, *ASC);
		ApplyTeamOverrides(*DataAsset, *ASC);
		GrantAbilities(*DataAsset, *ASC);
		ApplyInitEffects(*DataAsset, *ASC);
		AddTraitComponents(*DataAsset);
	}

	// Enemy archetype defaults should leave spawned enemies at full current vitals.
	SyncResourceValuesToCurrentMax(*ASC);
	ASC->ForceReplication();
	Owner->ForceNetUpdate();
}

void UAeyerjiEnemyArchetypeComponent::SetArchetypeData(UAeyerjiEnemyArchetypeData* NewData, bool bApplyImmediately)
{
	if (!CanMutateArchetypeSource(GetOwner()))
	{
		return;
	}

	ArchetypeData = NewData;
	ArchetypeLibrary = nullptr;
	ArchetypeTag = FGameplayTag();
	bApplied = false;
	bValidated = false;
	bVisualsApplied = false;
	bLoggedInvalidLibraryTag = false;
	bLoggedMissingLibraryEntry = false;
	bLoggedBothSources = false;

	if (bApplyImmediately)
	{
		ApplyArchetype();
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}

	ApplyArchetypeVisuals(/*bAllowInEditor=*/false, /*bForce=*/false);
}

void UAeyerjiEnemyArchetypeComponent::SetArchetypeFromLibrary(UAeyerjiEnemyArchetypeLibrary* NewLibrary, const FGameplayTag& NewTag, bool bApplyImmediately)
{
	if (!CanMutateArchetypeSource(GetOwner()))
	{
		return;
	}

	ArchetypeLibrary = NewLibrary;
	ArchetypeTag = NewTag;
	ArchetypeData = nullptr;
	bApplied = false;
	bValidated = false;
	bVisualsApplied = false;
	bLoggedInvalidLibraryTag = false;
	bLoggedMissingLibraryEntry = false;
	bLoggedBothSources = false;

	if (bApplyImmediately)
	{
		ApplyArchetype();
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}

	ApplyArchetypeVisuals(/*bAllowInEditor=*/false, /*bForce=*/false);
}

void UAeyerjiEnemyArchetypeComponent::ApplyArchetypeVisuals(bool bAllowInEditor, bool bForce)
{
	if (!bForce && bVisualsApplied)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	if (!bAllowInEditor && !(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return;
	}

	ApplyArchetypeCollisionOverrides(bAllowInEditor);

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(true);
	const UAeyerjiEnemyArchetypeData* DataAsset = LibraryEntry ? nullptr : ResolveArchetypeData(true);
	if (!LibraryEntry && !DataAsset)
	{
		return;
	}

	if (LibraryEntry)
	{
		ApplyMeshOverrides(*LibraryEntry);
	}
	else
	{
		ApplyMeshOverrides(*DataAsset);
	}
	bVisualsApplied = true;
}

void UAeyerjiEnemyArchetypeComponent::ApplyArchetypeCollisionOverrides(const bool bAllowInEditor)
{
	AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(GetOwner());
	const UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;
	if (!Enemy || !World)
	{
		return;
	}

	if (!bAllowInEditor && !(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return;
	}

	const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(true);
	const UAeyerjiEnemyArchetypeData* DataAsset = LibraryEntry ? nullptr : ResolveArchetypeData(true);
	if (LibraryEntry)
	{
		ApplyCapsuleOverrides(*LibraryEntry);
	}
	else if (DataAsset)
	{
		ApplyCapsuleOverrides(*DataAsset);
	}
	else
	{
		Enemy->ClearArchetypeCollisionCapsuleSize();
	}
}

const UAeyerjiEnemyArchetypeData* UAeyerjiEnemyArchetypeComponent::GetArchetypeData() const
{
	return ResolveArchetypeData(false);
}

FGameplayTag UAeyerjiEnemyArchetypeComponent::GetArchetypeTag() const
{
	if (ArchetypeTag.IsValid() && !ArchetypeLibrary.IsNull())
	{
		return ArchetypeTag;
	}

	if (const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(false))
	{
		return Data->ArchetypeTag;
	}

	return FGameplayTag();
}

UAnimMontage* UAeyerjiEnemyArchetypeComponent::GetAttackMontage() const
{
	if (const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(true))
	{
		return LibraryEntry->AttackMontage.LoadSynchronous();
	}

	const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(true);
	if (!Data)
	{
		return nullptr;
	}

	return Data->AttackMontage.LoadSynchronous();
}

TSubclassOf<UGameplayEffect> UAeyerjiEnemyArchetypeComponent::GetBasicAttackEffect() const
{
	if (const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(false))
	{
		return LibraryEntry->BasicAttackEffect;
	}

	const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(false);
	return Data ? Data->BasicAttackEffect : nullptr;
}

TSubclassOf<AActor> UAeyerjiEnemyArchetypeComponent::GetDeadBodyActorClass() const
{
	if (const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(false))
	{
		return LibraryEntry->DeadBodyActorClass;
	}

	const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(false);
	return Data ? Data->DeadBodyActorClass : nullptr;
}

void UAeyerjiEnemyArchetypeComponent::GetGrantedTags(FGameplayTagContainer& OutTags) const
{
	OutTags.Reset();

	if (const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(false))
	{
		OutTags = LibraryEntry->GrantedTags;
		if (LibraryEntry->ArchetypeTag.IsValid())
		{
			OutTags.AddTag(LibraryEntry->ArchetypeTag);
		}

		return;
	}

	const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(false);
	if (Data)
	{
		OutTags = Data->GrantedTags;
		if (Data->ArchetypeTag.IsValid())
		{
			OutTags.AddTag(Data->ArchetypeTag);
		}
	}
}

const FAeyerjiEnemyStatMultipliers* UAeyerjiEnemyArchetypeComponent::GetStatMultipliers() const
{
	if (const FAeyerjiEnemyArchetypeEntry* LibraryEntry = ResolveArchetypeEntry(true))
	{
		return &LibraryEntry->StatMultipliers;
	}

	const UAeyerjiEnemyArchetypeData* Data = ResolveArchetypeData(true);
	return Data ? &Data->StatMultipliers : nullptr;
}

bool UAeyerjiEnemyArchetypeComponent::HasArchetypeData() const
{
	if (ArchetypeTag.IsValid() && !ArchetypeLibrary.IsNull())
	{
		return true;
	}

	return !ArchetypeData.IsNull();
}

const FAeyerjiEnemyArchetypeEntry* UAeyerjiEnemyArchetypeComponent::ResolveArchetypeEntry(bool bLoadIfNeeded) const
{
	if (ArchetypeLibrary.IsNull())
	{
		return nullptr;
	}

	if (!ArchetypeTag.IsValid())
	{
		if (bLoadIfNeeded && !bLoggedInvalidLibraryTag)
		{
			UE_LOG(LogTemp, Warning, TEXT("Enemy archetype: ArchetypeLibrary set but ArchetypeTag is invalid on %s."), *GetNameSafe(GetOwner()));
			bLoggedInvalidLibraryTag = true;
		}
		return nullptr;
	}

	const UAeyerjiEnemyArchetypeLibrary* Library = ArchetypeLibrary.IsValid()
		? ArchetypeLibrary.Get()
		: (bLoadIfNeeded ? ArchetypeLibrary.LoadSynchronous() : nullptr);
	if (!Library)
	{
		if (bLoadIfNeeded && !bLoggedMissingLibraryEntry)
		{
			UE_LOG(LogTemp, Warning, TEXT("Enemy archetype: failed to load ArchetypeLibrary %s on %s."), *ArchetypeLibrary.ToString(), *GetNameSafe(GetOwner()));
			bLoggedMissingLibraryEntry = true;
		}
		return nullptr;
	}

	const FAeyerjiEnemyArchetypeEntry* Entry = Library->FindEntryByTag(ArchetypeTag);
	if (!Entry && bLoadIfNeeded && !bLoggedMissingLibraryEntry)
	{
		UE_LOG(LogTemp, Warning, TEXT("Enemy archetype: ArchetypeLibrary %s has no entry for tag %s on %s."),
			*GetNameSafe(Library), *ArchetypeTag.ToString(), *GetNameSafe(GetOwner()));
		bLoggedMissingLibraryEntry = true;
	}

	return Entry;
}

const UAeyerjiEnemyArchetypeData* UAeyerjiEnemyArchetypeComponent::ResolveArchetypeData(bool bLoadIfNeeded) const
{
	if (ArchetypeData.IsNull())
	{
		return nullptr;
	}

	if (ArchetypeData.IsValid())
	{
		return ArchetypeData.Get();
	}

	return bLoadIfNeeded ? ArchetypeData.LoadSynchronous() : ArchetypeData.Get();
}

void UAeyerjiEnemyArchetypeComponent::ValidateArchetype(const UAeyerjiEnemyArchetypeData& Data)
{
	ValidateArchetypeInternal(bValidated, Data, GetNameSafe(&Data));
}

void UAeyerjiEnemyArchetypeComponent::ValidateArchetype(const FAeyerjiEnemyArchetypeEntry& Data)
{
	const FString DebugName = Data.ArchetypeTag.IsValid() ? Data.ArchetypeTag.ToString() : TEXT("Unknown");
	ValidateArchetypeInternal(bValidated, Data, DebugName);
}

void UAeyerjiEnemyArchetypeComponent::ApplyAttributeDefaults(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	ApplyAttributeDefaultsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::ApplyAttributeDefaults(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC)
{
	ApplyAttributeDefaultsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::ApplyGrantedTags(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	ApplyGrantedTagsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::ApplyGrantedTags(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC)
{
	ApplyGrantedTagsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::ApplyTeamOverrides(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	ApplyTeamOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}

void UAeyerjiEnemyArchetypeComponent::ApplyTeamOverrides(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC)
{
	ApplyTeamOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}

void UAeyerjiEnemyArchetypeComponent::GrantAbilities(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	GrantAbilitiesInternal(Data, ASC, GetOwner());
}

void UAeyerjiEnemyArchetypeComponent::GrantAbilities(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC)
{
	GrantAbilitiesInternal(Data, ASC, GetOwner());
}

void UAeyerjiEnemyArchetypeComponent::ApplyInitEffects(const UAeyerjiEnemyArchetypeData& Data, UAbilitySystemComponent& ASC)
{
	ApplyInitEffectsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::ApplyInitEffects(const FAeyerjiEnemyArchetypeEntry& Data, UAbilitySystemComponent& ASC)
{
	ApplyInitEffectsInternal(Data, ASC);
}

void UAeyerjiEnemyArchetypeComponent::AddTraitComponents(const UAeyerjiEnemyArchetypeData& Data)
{
	AddTraitComponentsInternal(Data, GetOwner());
}

void UAeyerjiEnemyArchetypeComponent::AddTraitComponents(const FAeyerjiEnemyArchetypeEntry& Data)
{
	AddTraitComponentsInternal(Data, GetOwner());
}

void UAeyerjiEnemyArchetypeComponent::ApplyMeshOverrides(const UAeyerjiEnemyArchetypeData& Data)
{
	ApplyMeshOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}

void UAeyerjiEnemyArchetypeComponent::ApplyMeshOverrides(const FAeyerjiEnemyArchetypeEntry& Data)
{
	ApplyMeshOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}

void UAeyerjiEnemyArchetypeComponent::ApplyCapsuleOverrides(const UAeyerjiEnemyArchetypeData& Data)
{
	ApplyCapsuleOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}

void UAeyerjiEnemyArchetypeComponent::ApplyCapsuleOverrides(const FAeyerjiEnemyArchetypeEntry& Data)
{
	ApplyCapsuleOverridesInternal(Data, Cast<AEnemyParentNative>(GetOwner()));
}
