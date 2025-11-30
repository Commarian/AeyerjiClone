// AeyerjiEncounterDefinition.cpp
#include "Director/AeyerjiEncounterDefinition.h"

// We include your spawner header to access the runtime structs FWaveDefinition/FEnemySet.
#include "Director/AeyerjiSpawnerGroup.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

static TSubclassOf<APawn> LoadPawnClassSync(const TSoftClassPtr<APawn>& SoftPtr)
{
	if (!SoftPtr.IsValid())
	{
		if (UAssetManager* AM = UAssetManager::GetIfInitialized())
		{
			AM->GetStreamableManager().RequestSyncLoad(SoftPtr.ToSoftObjectPath());
		}
	}
	return SoftPtr.Get();
}

bool UAeyerjiEncounterDefinition::BuildRuntimeWaves(TArray<FWaveDefinition>& OutWaves) const
{
	OutWaves.Reset();

	int32 ValidSets = 0;

	for (const FWaveDefData& WaveData : Waves)
	{
		FWaveDefinition WaveRuntime;
		WaveRuntime.PostSpawnDelay = FMath::Max(0.f, WaveData.PostSpawnDelay);

		for (const FEnemySetDef& SetData : WaveData.EnemySets)
		{
			TSubclassOf<APawn> EnemyCls = LoadPawnClassSync(SetData.EnemyClass);
			if (!*EnemyCls || SetData.Count <= 0)
			{
				continue;
			}

			FEnemySet RuntimeSet;
			RuntimeSet.EnemyClass    = EnemyCls;
			RuntimeSet.Count         = SetData.Count;
			RuntimeSet.SpawnInterval = FMath::Max(0.f, SetData.SpawnInterval);
			RuntimeSet.bIsElite      = SetData.bIsElite;
			RuntimeSet.bIsMiniBoss   = SetData.bIsMiniBoss;
			RuntimeSet.MiniBossGrantedAbilities = SetData.MiniBossGrantedAbilities;
			RuntimeSet.ForcedEliteAffixes = SetData.ForcedEliteAffixes;
			RuntimeSet.EliteAffixPoolOverride = SetData.EliteAffixPoolOverride;
			RuntimeSet.MinEliteAffixes = FMath::Max(0, SetData.MinEliteAffixes);
			RuntimeSet.MaxEliteAffixes = FMath::Max(RuntimeSet.MinEliteAffixes, SetData.MaxEliteAffixes);
			RuntimeSet.EliteHealthMultiplierOverride = FMath::Max(0.f, SetData.EliteHealthMultiplierOverride);
			RuntimeSet.EliteDamageMultiplierOverride = FMath::Max(0.f, SetData.EliteDamageMultiplierOverride);
			RuntimeSet.EliteRangeMultiplierOverride = FMath::Max(0.f, SetData.EliteRangeMultiplierOverride);
			RuntimeSet.EliteScaleMultiplierOverride = FMath::Max(0.f, SetData.EliteScaleMultiplierOverride);
			RuntimeSet.EliteXPMultiplierOverride = FMath::Max(0.f, SetData.EliteXpMultiplierOverride);
			RuntimeSet.MiniBossXPMultiplierOverride = FMath::Max(0.f, SetData.MiniBossXpMultiplierOverride);

			WaveRuntime.EnemySets.Add(RuntimeSet);
			++ValidSets;
		}

		OutWaves.Add(WaveRuntime);
	}

	return ValidSets > 0;
}
