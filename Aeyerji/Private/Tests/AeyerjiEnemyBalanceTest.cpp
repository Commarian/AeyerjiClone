#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AttributeSet.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Dom/JsonObject.h"
#include "Enemy/AeyerjiEnemyArchetypeLibrary.h"
#include "Enemy/EnemyScalingTable.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
	bool IsCanonicalEliteMultiplier(const float Actual, const float Expected)
	{
		return FMath::IsNearlyEqual(Actual, Expected, KINDA_SMALL_NUMBER);
	}

	bool LoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	bool LoadJsonArray(const FString& Path, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, OutArray);
	}

	bool UsesSameScalingLane(const FEnemyScalingRow& Ordinary, const FEnemyScalingRow& Elite)
	{
		if (!FMath::IsNearlyEqual(Ordinary.BaseLevel, Elite.BaseLevel)
			|| !FMath::IsNearlyEqual(Ordinary.MaxLevelAdvantage, Elite.MaxLevelAdvantage)
			|| !FMath::IsNearlyEqual(Ordinary.DifficultyExponent, Elite.DifficultyExponent)
			|| Ordinary.Attributes.Num() != Elite.Attributes.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Ordinary.Attributes.Num(); ++Index)
		{
			const FEnemyAttributeScalingEntry& OrdinaryEntry = Ordinary.Attributes[Index];
			const FEnemyAttributeScalingEntry& EliteEntry = Elite.Attributes[Index];
			if (OrdinaryEntry.AttributeName != EliteEntry.AttributeName
				|| !FMath::IsNearlyEqual(OrdinaryEntry.PerLevelMultiplier, EliteEntry.PerLevelMultiplier)
				|| !FMath::IsNearlyEqual(OrdinaryEntry.PerLevelAdd, EliteEntry.PerLevelAdd)
				|| !FMath::IsNearlyEqual(OrdinaryEntry.DifficultyMinMultiplier, EliteEntry.DifficultyMinMultiplier)
				|| !FMath::IsNearlyEqual(OrdinaryEntry.DifficultyMaxMultiplier, EliteEntry.DifficultyMaxMultiplier)
				|| !FMath::IsNearlyEqual(OrdinaryEntry.MinValue, EliteEntry.MinValue)
				|| !FMath::IsNearlyEqual(OrdinaryEntry.MaxValue, EliteEntry.MaxValue))
			{
				return false;
			}
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyBalanceAssetParityTest,
	"Aeyerji.EnemyBalance.AssetParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyBalanceAssetParityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString DataRoot = FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("Source"),
		TEXT("Aeyerji"),
		TEXT("Data"));
	const FString ContractPath = FPaths::Combine(DataRoot, TEXT("EnemyBalanceTargets.json"));

	TSharedPtr<FJsonObject> Contract;
	TestTrue(TEXT("Enemy balance contract loads as JSON."), LoadJsonObject(ContractPath, Contract));
	if (!Contract.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* EnemyValues = nullptr;
	TestTrue(TEXT("Enemy balance contract contains an Enemies array."),
		Contract->TryGetArrayField(TEXT("Enemies"), EnemyValues) && EnemyValues);
	if (!EnemyValues)
	{
		return false;
	}

	const UAeyerjiEnemyArchetypeLibrary* ArchetypeLibrary = LoadObject<UAeyerjiEnemyArchetypeLibrary>(
		nullptr,
		TEXT("/Game/Enemy/AeyerjiEnemyArchetypeLibrary.AeyerjiEnemyArchetypeLibrary"));
	TestNotNull(TEXT("Live enemy archetype library loads."), ArchetypeLibrary);

	int32 LiveEnemyCount = 0;
	for (const TSharedPtr<FJsonValue>& EnemyValue : *EnemyValues)
	{
		const TSharedPtr<FJsonObject> EnemyObject = EnemyValue.IsValid() ? EnemyValue->AsObject() : nullptr;
		if (!EnemyObject.IsValid())
		{
			AddError(TEXT("Enemy balance contract contains a non-object enemy entry."));
			continue;
		}

		FString RuntimeStatus;
		if (!EnemyObject->TryGetStringField(TEXT("RuntimeStatus"), RuntimeStatus)
			|| RuntimeStatus != TEXT("Live"))
		{
			continue;
		}

		++LiveEnemyCount;
		FString EnemyId;
		FString SourcePath;
		FString DataTablePath;
		if (!EnemyObject->TryGetStringField(TEXT("Id"), EnemyId)
			|| !EnemyObject->TryGetStringField(TEXT("Source"), SourcePath)
			|| !EnemyObject->TryGetStringField(TEXT("DataTableAsset"), DataTablePath))
		{
			AddError(TEXT("A live enemy contract entry is missing Id, Source, or DataTableAsset."));
			continue;
		}

		const FString Context = FString::Printf(TEXT("Enemy balance parity: %s"), *EnemyId);
		TArray<TSharedPtr<FJsonValue>> SourceRows;
		const FString FullSourcePath = FPaths::Combine(DataRoot, SourcePath);
		TestTrue(*FString::Printf(TEXT("%s source JSON loads."), *EnemyId),
			LoadJsonArray(FullSourcePath, SourceRows));
		if (SourceRows.IsEmpty())
		{
			continue;
		}

		const UDataTable* LiveTable = LoadObject<UDataTable>(nullptr, *DataTablePath);
		TestNotNull(*FString::Printf(TEXT("%s live DataTable loads."), *EnemyId), LiveTable);
		if (!LiveTable)
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s live DataTable uses FAttributeMetaData."), *EnemyId),
			LiveTable->GetRowStruct() == FAttributeMetaData::StaticStruct());

		TSet<FName> ExpectedRowNames;
		for (const TSharedPtr<FJsonValue>& SourceRowValue : SourceRows)
		{
			const TSharedPtr<FJsonObject> SourceRow = SourceRowValue.IsValid()
				? SourceRowValue->AsObject()
				: nullptr;
			if (!SourceRow.IsValid())
			{
				AddError(FString::Printf(TEXT("%s contains a non-object source row."), *EnemyId));
				continue;
			}

			FString RowNameString;
			if (!SourceRow->TryGetStringField(TEXT("Name"), RowNameString))
			{
				AddError(FString::Printf(TEXT("%s contains a source row without Name."), *EnemyId));
				continue;
			}

			const FName RowName(*RowNameString);
			ExpectedRowNames.Add(RowName);
			const FAttributeMetaData* LiveRow = LiveTable->FindRow<FAttributeMetaData>(
				RowName,
				Context,
				false);
			TestNotNull(
				*FString::Printf(TEXT("%s live DataTable contains %s."), *EnemyId, *RowNameString),
				LiveRow);
			if (!LiveRow)
			{
				continue;
			}

			double BaseValue = 0.0;
			double MinValue = 0.0;
			double MaxValue = 0.0;
			FString DerivedAttributeInfo;
			bool bCanStack = false;
			const bool bHasAllFields =
				SourceRow->TryGetNumberField(TEXT("BaseValue"), BaseValue)
				&& SourceRow->TryGetNumberField(TEXT("MinValue"), MinValue)
				&& SourceRow->TryGetNumberField(TEXT("MaxValue"), MaxValue)
				&& SourceRow->TryGetStringField(TEXT("DerivedAttributeInfo"), DerivedAttributeInfo)
				&& SourceRow->TryGetBoolField(TEXT("bCanStack"), bCanStack);
			TestTrue(
				*FString::Printf(TEXT("%s source row %s has all metadata fields."), *EnemyId, *RowNameString),
				bHasAllFields);
			if (!bHasAllFields)
			{
				continue;
			}

			TestTrue(
				*FString::Printf(TEXT("%s %s BaseValue matches source."), *EnemyId, *RowNameString),
				FMath::IsNearlyEqual(LiveRow->BaseValue, static_cast<float>(BaseValue)));
			TestTrue(
				*FString::Printf(TEXT("%s %s MinValue matches source."), *EnemyId, *RowNameString),
				FMath::IsNearlyEqual(LiveRow->MinValue, static_cast<float>(MinValue)));
			TestTrue(
				*FString::Printf(TEXT("%s %s MaxValue matches source."), *EnemyId, *RowNameString),
				FMath::IsNearlyEqual(LiveRow->MaxValue, static_cast<float>(MaxValue)));
			TestEqual(
				*FString::Printf(TEXT("%s %s DerivedAttributeInfo matches source."), *EnemyId, *RowNameString),
				LiveRow->DerivedAttributeInfo,
				DerivedAttributeInfo);
			TestEqual(
				*FString::Printf(TEXT("%s %s bCanStack matches source."), *EnemyId, *RowNameString),
				LiveRow->bCanStack,
				bCanStack);
		}

		TestEqual(*FString::Printf(TEXT("%s live row count matches source."), *EnemyId),
			LiveTable->GetRowNames().Num(), ExpectedRowNames.Num());

		FString GameplayTagString;
		if (ArchetypeLibrary
			&& EnemyObject->TryGetStringField(TEXT("GameplayTag"), GameplayTagString)
			&& !GameplayTagString.IsEmpty())
		{
			const FGameplayTag GameplayTag =
				FGameplayTag::RequestGameplayTag(FName(*GameplayTagString), false);
			const FAeyerjiEnemyArchetypeEntry* Entry = ArchetypeLibrary->FindEntryByTag(GameplayTag);
			TestNotNull(
				*FString::Printf(TEXT("%s has a live archetype-library entry."), *EnemyId),
				Entry);
			if (Entry)
			{
				TestEqual(
					*FString::Printf(TEXT("%s archetype-library DataTable matches the contract."), *EnemyId),
					Entry->AttributeDefaultsTable.ToSoftObjectPath().ToString(),
					DataTablePath);
			}
		}
	}

	TestTrue(TEXT("Enemy balance contract contains live enemy mappings."), LiveEnemyCount > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyBalanceFallbackDifficultyTest,
	"Aeyerji.EnemyBalance.DifficultyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyBalanceFallbackDifficultyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiDifficultyTuning* FallbackTuning =
		NewObject<UAeyerjiDifficultyTuning>(GetTransientPackage());
	TestNotNull(TEXT("Fallback difficulty tuning can be constructed."), FallbackTuning);
	if (!FallbackTuning)
	{
		return false;
	}

	TestEqual(TEXT("Fallback Normal world tier matches the canonical curve key."),
		FallbackTuning->NormalWorldTier, 167);
	TestTrue(TEXT("Fallback Normal world tier evaluates to an exact one-times stat budget."),
		FMath::IsNearlyEqual(FallbackTuning->EvaluateStatBudget(FallbackTuning->NormalWorldTier), 1.f));

	const UAeyerjiDifficultyTuning* LiveTuning = UAeyerjiDifficultySettings::Get();
	TestNotNull(TEXT("Configured difficulty tuning asset loads."), LiveTuning);
	if (LiveTuning)
	{
		TestEqual(TEXT("Configured and fallback Normal world tiers stay in parity."),
			LiveTuning->NormalWorldTier, FallbackTuning->NormalWorldTier);
		TestTrue(TEXT("Configured Normal world tier evaluates to an exact one-times stat budget."),
			FMath::IsNearlyEqual(LiveTuning->EvaluateStatBudget(LiveTuning->NormalWorldTier), 1.f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyBalanceSpawnerDefaultsTest,
	"Aeyerji.EnemyBalance.EliteSpawnerDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyBalanceSpawnerDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AAeyerjiSpawnerGroup* NativeDefaults = GetDefault<AAeyerjiSpawnerGroup>();
	TestNotNull(TEXT("Native spawner defaults are available."), NativeDefaults);
	if (!NativeDefaults)
	{
		return false;
	}

	TestTrue(TEXT("Native elite health promotion is canonical."),
		IsCanonicalEliteMultiplier(NativeDefaults->EliteHealthMultiplier, 4.f));
	TestTrue(TEXT("Native elite damage promotion is canonical."),
		IsCanonicalEliteMultiplier(NativeDefaults->EliteDamageMultiplier, 1.35f));
	TestTrue(TEXT("Native elite range promotion remains canonical."),
		IsCanonicalEliteMultiplier(NativeDefaults->EliteRangeMultiplier, 1.5f));

	static const FName CoreDefaultNames[] =
	{
		GET_MEMBER_NAME_CHECKED(AAeyerjiSpawnerGroup, EliteHealthMultiplier),
		GET_MEMBER_NAME_CHECKED(AAeyerjiSpawnerGroup, EliteDamageMultiplier),
		GET_MEMBER_NAME_CHECKED(AAeyerjiSpawnerGroup, EliteRangeMultiplier)
	};
	for (const FName PropertyName : CoreDefaultNames)
	{
		const FProperty* Property = FindFProperty<FProperty>(AAeyerjiSpawnerGroup::StaticClass(), PropertyName);
		TestNotNull(*FString::Printf(TEXT("%s remains reflected."), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s cannot be edited per placed spawner."), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
		}
	}

	UClass* SpawnerBlueprintClass = LoadClass<AAeyerjiSpawnerGroup>(
		nullptr,
		TEXT("/Game/Levels/Director/BP_AeyerjiSpawnerGroup.BP_AeyerjiSpawnerGroup_C"));
	TestNotNull(TEXT("Live spawner Blueprint class loads."), SpawnerBlueprintClass);
	const AAeyerjiSpawnerGroup* LiveDefaults = SpawnerBlueprintClass
		? SpawnerBlueprintClass->GetDefaultObject<AAeyerjiSpawnerGroup>()
		: nullptr;
	TestNotNull(TEXT("Live spawner Blueprint defaults are available."), LiveDefaults);
	if (LiveDefaults)
	{
		TestTrue(TEXT("Live elite health promotion matches the native contract."),
			IsCanonicalEliteMultiplier(LiveDefaults->EliteHealthMultiplier, NativeDefaults->EliteHealthMultiplier));
		TestTrue(TEXT("Live elite damage promotion matches the native contract."),
			IsCanonicalEliteMultiplier(LiveDefaults->EliteDamageMultiplier, NativeDefaults->EliteDamageMultiplier));
		TestTrue(TEXT("Live elite range promotion matches the native contract."),
			IsCanonicalEliteMultiplier(LiveDefaults->EliteRangeMultiplier, NativeDefaults->EliteRangeMultiplier));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyBalanceScalingAssetTest,
	"Aeyerji.EnemyBalance.ScalingAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyBalanceScalingAssetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UDataTable* ScalingTable = LoadObject<UDataTable>(
		nullptr,
		TEXT("/Game/Enemy/EnemyAttrsScalingTable.EnemyAttrsScalingTable"));
	TestNotNull(TEXT("Live enemy scaling table loads."), ScalingTable);
	if (!ScalingTable)
	{
		return false;
	}

	const FEnemyScalingRow* Melee = ScalingTable->FindRow<FEnemyScalingRow>(
		TEXT("Melee"), TEXT("Enemy balance automation"), false);
	const FEnemyScalingRow* EliteMelee = ScalingTable->FindRow<FEnemyScalingRow>(
		TEXT("EliteMelee"), TEXT("Enemy balance automation"), false);
	const FEnemyScalingRow* Ranged = ScalingTable->FindRow<FEnemyScalingRow>(
		TEXT("Ranged"), TEXT("Enemy balance automation"), false);
	const FEnemyScalingRow* EliteRanged = ScalingTable->FindRow<FEnemyScalingRow>(
		TEXT("EliteRanged"), TEXT("Enemy balance automation"), false);
	TestNotNull(TEXT("Ordinary melee scaling lane exists."), Melee);
	TestNotNull(TEXT("Elite melee scaling lane exists."), EliteMelee);
	TestNotNull(TEXT("Ordinary ranged scaling lane exists."), Ranged);
	TestNotNull(TEXT("Elite ranged scaling lane exists."), EliteRanged);

	const FGameplayTag ExpectedMelee =
		FGameplayTag::RequestGameplayTag(TEXT("Enemy.Role.Elite.Melee"), false);
	const FGameplayTag ExpectedRanged =
		FGameplayTag::RequestGameplayTag(TEXT("Enemy.Role.Elite.Ranged"), false);
	TestTrue(TEXT("Elite melee scaling lane uses the elite melee parent tag."),
		EliteMelee && ExpectedMelee.IsValid() && EliteMelee->ArchetypeTag == ExpectedMelee);
	TestTrue(TEXT("Elite ranged scaling lane uses the elite ranged parent tag."),
		EliteRanged && ExpectedRanged.IsValid() && EliteRanged->ArchetypeTag == ExpectedRanged);
	TestTrue(TEXT("Elite melee lane mirrors ordinary melee scaling before elite promotion."),
		Melee && EliteMelee && UsesSameScalingLane(*Melee, *EliteMelee));
	TestTrue(TEXT("Elite ranged lane mirrors ordinary ranged scaling before elite promotion."),
		Ranged && EliteRanged && UsesSameScalingLane(*Ranged, *EliteRanged));

	return true;
}

#endif
