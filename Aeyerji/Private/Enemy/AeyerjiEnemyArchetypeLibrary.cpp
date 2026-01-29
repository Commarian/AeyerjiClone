// AeyerjiEnemyArchetypeLibrary.cpp
#include "Enemy/AeyerjiEnemyArchetypeLibrary.h"

namespace
{
// Copies all archetype settings from a data asset into a library entry.
void CopyArchetypeDataToEntry(const UAeyerjiEnemyArchetypeData& Source, FAeyerjiEnemyArchetypeEntry& OutEntry)
{
	OutEntry.ArchetypeTag = Source.ArchetypeTag;
	OutEntry.GrantedTags = Source.GrantedTags;
	OutEntry.bOverrideTeamId = Source.bOverrideTeamId;
	OutEntry.TeamIdOverride = Source.TeamIdOverride;
	OutEntry.bOverrideTeamTag = Source.bOverrideTeamTag;
	OutEntry.TeamTagOverride = Source.TeamTagOverride;
	OutEntry.AttackMontage = Source.AttackMontage;
	OutEntry.AbilityLevel = Source.AbilityLevel;
	OutEntry.EffectLevel = Source.EffectLevel;
	OutEntry.GrantedAbilities = Source.GrantedAbilities;
	OutEntry.InitGameplayEffects = Source.InitGameplayEffects;
	OutEntry.BasicAttackEffect = Source.BasicAttackEffect;
	OutEntry.AttributeDefaultsTable = Source.AttributeDefaultsTable;
	OutEntry.AttributeSetClass = Source.AttributeSetClass;
	OutEntry.TraitComponents = Source.TraitComponents;
	OutEntry.AggroSettings = Source.AggroSettings;
	OutEntry.MeshOverrides = Source.MeshOverrides;
	OutEntry.StatMultipliers = Source.StatMultipliers;
	OutEntry.bWarnIfMissingAttackMontage = Source.bWarnIfMissingAttackMontage;
	OutEntry.bWarnIfMissingInitEffects = Source.bWarnIfMissingInitEffects;
	OutEntry.bWarnIfMissingGrantedTags = Source.bWarnIfMissingGrantedTags;
}
} // namespace

const FAeyerjiEnemyArchetypeEntry* UAeyerjiEnemyArchetypeLibrary::FindEntryByTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}

	for (const FAeyerjiEnemyArchetypeEntry& Entry : Entries)
	{
		if (Entry.ArchetypeTag == Tag)
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool UAeyerjiEnemyArchetypeLibrary::TryGetEntryByTag(const FGameplayTag& Tag, FAeyerjiEnemyArchetypeEntry& OutEntry) const
{
	const FAeyerjiEnemyArchetypeEntry* Entry = FindEntryByTag(Tag);
	if (!Entry)
	{
		return false;
	}

	OutEntry = *Entry;
	return true;
}

#if WITH_EDITOR
void UAeyerjiEnemyArchetypeLibrary::RunMigration()
{
	bool bChanged = false;

	if (bMigrationClearExisting && !Entries.IsEmpty())
	{
		Modify();
		Entries.Reset();
		bChanged = true;
	}

	for (const TSoftObjectPtr<UAeyerjiEnemyArchetypeData>& SourcePtr : MigrationSources)
	{
		const UAeyerjiEnemyArchetypeData* Source = SourcePtr.LoadSynchronous();
		if (!Source)
		{
			continue;
		}

		if (!Source->ArchetypeTag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Archetype migration skipped: %s has no ArchetypeTag"), *GetNameSafe(Source));
			continue;
		}

		const int32 ExistingIndex = Entries.IndexOfByPredicate([Source](const FAeyerjiEnemyArchetypeEntry& Entry)
		{
			return Entry.ArchetypeTag == Source->ArchetypeTag;
		});

		if (ExistingIndex != INDEX_NONE && !bMigrationOverwriteExisting)
		{
			continue;
		}

		FAeyerjiEnemyArchetypeEntry NewEntry;
		CopyArchetypeDataToEntry(*Source, NewEntry);

		if (ExistingIndex != INDEX_NONE)
		{
			Modify();
			Entries[ExistingIndex] = NewEntry;
			bChanged = true;
		}
		else
		{
			Modify();
			Entries.Add(NewEntry);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		MarkPackageDirty();
	}
}
#endif
