#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Systems/AeyerjiWorldStateTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiWorldStateTypesTest,
	"Aeyerji.WorldState.Types",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiWorldStateTypesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FGameplayTag QuestTag = FGameplayTag::RequestGameplayTag(TEXT("World.Quest.Flag"), false);
	const FAeyerjiWorldStateKey Key(QuestTag, FName(TEXT("QuestA")), FName(TEXT("CharacterSlotA")));
	TestTrue(TEXT("A key with a valid state tag is valid."), Key.IsValid());
	TestTrue(TEXT("A key string includes the instance id."), Key.ToString().Contains(TEXT("QuestA")));
	TestTrue(TEXT("A key string includes the owner id."), Key.ToString().Contains(TEXT("CharacterSlotA")));

	const FAeyerjiWorldStateValue BoolValue = FAeyerjiWorldStateValue::FromBool(true);
	TestTrue(TEXT("Bool values compare equal to identical bool values."), BoolValue.Equals(FAeyerjiWorldStateValue::FromBool(true)));
	TestFalse(TEXT("Bool values compare unequal to different bool values."), BoolValue.Equals(FAeyerjiWorldStateValue::FromBool(false)));

	const FAeyerjiWorldStateValue IntValue = FAeyerjiWorldStateValue::FromInt(7);
	double NumericValue = 0.0;
	TestTrue(TEXT("Integer values expose numeric comparison data."), IntValue.TryGetNumericValue(NumericValue));
	TestEqual(TEXT("Integer numeric value is preserved."), NumericValue, 7.0);

	FAeyerjiWorldStateEntry Entry;
	Entry.Key = Key;
	Entry.Value = FAeyerjiWorldStateValue::FromString(TEXT("Started"));
	Entry.Persistence = EAeyerjiWorldStatePersistence::Persistent;
	Entry.Replication = EAeyerjiWorldStateReplication::PublicReplicated;
	Entry.Scope = EAeyerjiWorldStateScope::Character;
	Entry.Version = 3;

	const FAeyerjiWorldStateEntry DataOnly = Entry.MakeDataOnlyCopy();
	TestTrue(TEXT("Data-only copy preserves the key."), DataOnly.Key == Entry.Key);
	TestTrue(TEXT("Data-only copy preserves the value."), DataOnly.Value.Equals(Entry.Value));
	TestEqual(TEXT("Data-only copy preserves the scope."), DataOnly.Scope, Entry.Scope);
	TestEqual(TEXT("Data-only copy preserves the version."), DataOnly.Version, Entry.Version);

	return true;
}

#endif
