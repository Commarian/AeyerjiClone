#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "Aeyerji/AeyerjiSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/AeyerjiWorldStateTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiSaveProfileSerializationTest,
	"Aeyerji.Save.ProfileSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiSaveProfileSerializationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
	TestNotNull(TEXT("Save object can be created."), SaveData);
	if (!SaveData)
	{
		return false;
	}

	SaveData->OwnerKey = TEXT("AutomationOwner");
	SaveData->Attributes.XP = 123.0f;
	SaveData->Attributes.Level = 9;
	SaveData->SelectedPassiveId = FName(TEXT("Passive.Automation"));

	FAeyerjiAbilitySlot AbilitySlot;
	AbilitySlot.Description = FName(TEXT("AutomationAbility"));
	AbilitySlot.Level = 3;
	AbilitySlot.Class = UGameplayAbility::StaticClass();
	AbilitySlot.CaptureStableReferences();
	SaveData->ActionBar = { AbilitySlot };

	FInventoryItemSnapshot ItemSnapshot;
	ItemSnapshot.ItemId = FGuid::NewGuid();
	ItemSnapshot.DefinitionKey = FName(TEXT("/Game/Items/DA_AutomationItem.DA_AutomationItem"));
	ItemSnapshot.ItemLevel = 12;
	ItemSnapshot.InventorySize = FIntPoint(2, 3);
	SaveData->Inventory.ItemSnapshots.Add(ItemSnapshot);

	FAeyerjiWorldStateEntry WorldEntry;
	WorldEntry.Key = FAeyerjiWorldStateKey(
		FGameplayTag::RequestGameplayTag(TEXT("World.Quest.Flag"), false),
		FName(TEXT("AutomationQuest")),
		FName(TEXT("AutomationOwner")));
	WorldEntry.Value = FAeyerjiWorldStateValue::FromBool(true);
	WorldEntry.Persistence = EAeyerjiWorldStatePersistence::Persistent;
	WorldEntry.Scope = EAeyerjiWorldStateScope::Character;
	SaveData->WorldStateEntries.Add(WorldEntry);

	TArray<uint8> Bytes;
	TestTrue(TEXT("Profile save serializes to memory."), UGameplayStatics::SaveGameToMemory(SaveData, Bytes));
	TestTrue(TEXT("Serialized profile contains bytes."), Bytes.Num() > 0);

	UAeyerjiSaveGame* LoadedData = Cast<UAeyerjiSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Profile save loads from memory."), LoadedData);
	if (!LoadedData)
	{
		return false;
	}

	TestEqual(TEXT("XP roundtrips."), LoadedData->Attributes.XP, 123.0f);
	TestEqual(TEXT("Level roundtrips."), LoadedData->Attributes.Level, 9);
	TestEqual(TEXT("Selected passive roundtrips."), LoadedData->SelectedPassiveId, FName(TEXT("Passive.Automation")));
	TestEqual(TEXT("Action bar roundtrips."), LoadedData->ActionBar.Num(), 1);
	TestEqual(TEXT("Action slot data roundtrips."), LoadedData->ActionBar[0].Description, FName(TEXT("AutomationAbility")));
	TestFalse(TEXT("Action slot stable class path roundtrips."), LoadedData->ActionBar[0].SavedAbilityClass.IsNull());
	LoadedData->ActionBar[0].ResolveSavedReferences();
	TestEqual(TEXT("Action slot class resolves."), LoadedData->ActionBar[0].Class.Get(), UGameplayAbility::StaticClass());
	TestEqual(TEXT("Action slot level roundtrips."), LoadedData->ActionBar[0].Level, 3);
	TestEqual(TEXT("Inventory snapshot roundtrips."), LoadedData->Inventory.ItemSnapshots.Num(), 1);
	TestEqual(TEXT("Stable item definition key roundtrips."),
		LoadedData->Inventory.ItemSnapshots[0].DefinitionKey,
		FName(TEXT("/Game/Items/DA_AutomationItem.DA_AutomationItem")));
	TestEqual(TEXT("World-state entries roundtrip."), LoadedData->WorldStateEntries.Num(), 1);
	TestEqual(TEXT("Character world-state scope roundtrips."), LoadedData->WorldStateEntries[0].Scope, EAeyerjiWorldStateScope::Character);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiSaveProfileLargePayloadSerializationTest,
	"Aeyerji.Save.ProfileLargePayloadSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiSaveProfileLargePayloadSerializationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
	TestNotNull(TEXT("Large profile save object can be created."), SaveData);
	if (!SaveData)
	{
		return false;
	}

	SaveData->OwnerKey = TEXT("AutomationLargeOwner");
	SaveData->Attributes.Level = 44;
	SaveData->Attributes.XP = 98765.0f;

	const FGameplayTag FlagTag = FGameplayTag::RequestGameplayTag(TEXT("World.Quest.Flag"), false);
	for (int32 Index = 0; Index < 900; ++Index)
	{
		FInventoryItemSnapshot Snapshot;
		Snapshot.ItemId = FGuid::NewGuid();
		Snapshot.DefinitionKey = FName(*FString::Printf(TEXT("/Game/Items/Automation/DA_Item_%04d.DA_Item_%04d"), Index, Index));
		Snapshot.ItemLevel = 1 + (Index % 100);
		Snapshot.InventorySize = FIntPoint(1 + (Index % 2), 1 + (Index % 3));
		SaveData->Inventory.ItemSnapshots.Add(Snapshot);

		FAeyerjiWorldStateEntry WorldEntry;
		WorldEntry.Key = FAeyerjiWorldStateKey(
			FlagTag,
			FName(*FString::Printf(TEXT("LargeFact_%04d"), Index)),
			FName(TEXT("AutomationLargeOwner")));
		WorldEntry.Value = FAeyerjiWorldStateValue::FromString(FString::Printf(TEXT("LargePayloadValue_%04d_%s"), Index, *FGuid::NewGuid().ToString()));
		WorldEntry.Persistence = EAeyerjiWorldStatePersistence::Persistent;
		WorldEntry.Scope = EAeyerjiWorldStateScope::Character;
		SaveData->WorldStateEntries.Add(WorldEntry);
	}

	TArray<uint8> Bytes;
	TestTrue(TEXT("Large profile serializes to memory."), UGameplayStatics::SaveGameToMemory(SaveData, Bytes));
	TestTrue(TEXT("Large profile exceeds the legacy 65 KB single-RPC risk threshold."), Bytes.Num() > 65 * 1024);

	UAeyerjiSaveGame* LoadedData = Cast<UAeyerjiSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Large profile loads from memory."), LoadedData);
	if (!LoadedData)
	{
		return false;
	}

	TestEqual(TEXT("Large profile item count roundtrips."), LoadedData->Inventory.ItemSnapshots.Num(), SaveData->Inventory.ItemSnapshots.Num());
	TestEqual(TEXT("Large profile world-state count roundtrips."), LoadedData->WorldStateEntries.Num(), SaveData->WorldStateEntries.Num());
	TestEqual(TEXT("Large profile level roundtrips."), LoadedData->Attributes.Level, 44);

	return true;
}

#endif
