#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Attributes/GE_Regen_Periodic.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"

namespace
{
	UAeyerjiInventoryComponent* MakeTestInventory(UObject* Outer)
	{
		AActor* Owner = NewObject<AActor>(Outer);
		UAeyerjiInventoryComponent* Inventory = NewObject<UAeyerjiInventoryComponent>(Owner);
		Owner->AddInstanceComponent(Inventory);
		Inventory->SetGridDimensions(4, 4);
		return Inventory;
	}

	UAeyerjiInventoryComponent* MakeTestInventoryWithASC(UObject* Outer, UAbilitySystemComponent*& OutASC)
	{
		AActor* Owner = GWorld
			? GWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity)
			: NewObject<AActor>(Outer);
		Owner->SetFlags(RF_Transient);
		OutASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(OutASC);
		if (Owner->GetWorld())
		{
			OutASC->RegisterComponent();
		}

		UAeyerjiAttributeSet* AttributeSet = NewObject<UAeyerjiAttributeSet>(OutASC);
		OutASC->AddAttributeSetSubobject(AttributeSet);
		OutASC->InitAbilityActorInfo(Owner, Owner);

		UAeyerjiInventoryComponent* Inventory = NewObject<UAeyerjiInventoryComponent>(Owner);
		Owner->AddInstanceComponent(Inventory);
		Inventory->SetGridDimensions(4, 4);
		return Inventory;
	}

	UAeyerjiItemInstance* MakeTestItem(UObject* Outer, UItemDefinition* Definition, int32 ItemLevel = 1)
	{
		UAeyerjiItemInstance* Item = NewObject<UAeyerjiItemInstance>(Outer);
		Item->Definition = Definition;
		Item->UniqueId = FGuid::NewGuid();
		Item->ItemLevel = ItemLevel;
		Item->InventorySize = Definition ? Definition->InventorySize : FIntPoint(1, 1);
		return Item;
	}

	UItemDefinition* MakeTestDefinition(UObject* Outer, EItemCategory Category = EItemCategory::Assault)
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(Outer);
		Definition->ItemCategory = Category;
		Definition->DefaultSlot = static_cast<EEquipmentSlot>(Category);
		Definition->RequiredLevel = Category == EItemCategory::Corruption ? 50 : 1;
		Definition->InventorySize = FIntPoint(1, 1);
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryEquipFirstThenBagTest,
	"Aeyerji.Inventory.EquipFirstThenBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryEquipFirstThenBagTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Assault preserves legacy Offense ordinal."), static_cast<int32>(EEquipmentSlot::Assault), 0);
	TestEqual(TEXT("Guard preserves legacy Defense ordinal."), static_cast<int32>(EEquipmentSlot::Guard), 1);
	TestEqual(TEXT("Flow preserves legacy Magic ordinal."), static_cast<int32>(EEquipmentSlot::Flow), 2);
	TestEqual(TEXT("Corruption is appended after legacy lanes."), static_cast<int32>(EEquipmentSlot::Corruption), 3);

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	if (!Inventory)
	{
		return false;
	}

	UItemDefinition* Definition = MakeTestDefinition(Inventory);
	UAeyerjiItemInstance* EquippableItem = MakeTestItem(Inventory, Definition, 1);
	const EAeyerjiAddItemResult EquipResult = UAeyerjiInventoryBPFL::EquipFirstThenBag(Inventory, EquippableItem);
	TestEqual(TEXT("Level-valid item equips first."), EquipResult, EAeyerjiAddItemResult::Equipped);
	TestTrue(TEXT("Equipped item receives a slot index."), EquippableItem->EquippedSlotIndex != INDEX_NONE);

	UAeyerjiItemInstance* HighLevelItem = MakeTestItem(Inventory, Definition, 99);
	const EAeyerjiAddItemResult BagResult = UAeyerjiInventoryBPFL::EquipFirstThenBag(Inventory, HighLevelItem);
	TestEqual(TEXT("Equip-rejected item falls back to bag placement."), BagResult, EAeyerjiAddItemResult::Bagged);
	TestTrue(TEXT("Bagged item remains owned."), Inventory->FindItemById(HighLevelItem->UniqueId) != nullptr);

	FInventoryItemGridData Placement;
	TestTrue(TEXT("Bagged item has a grid placement."), Inventory->GetPlacementForItem(HighLevelItem->UniqueId, Placement));

	UAeyerjiItemInstance* MissingDefinitionItem = MakeTestItem(Inventory, nullptr, 1);
	const EAeyerjiAddItemResult MissingDefinitionResult = UAeyerjiInventoryBPFL::EquipFirstThenBag(Inventory, MissingDefinitionItem);
	TestEqual(TEXT("Missing definition returns a distinct failure."), MissingDefinitionResult, EAeyerjiAddItemResult::Failed_MissingDefinition);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryLegacySnapshotLoadTest,
	"Aeyerji.Inventory.LegacySnapshotLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryLegacySnapshotLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	UItemDefinition* LegacyDefinition = MakeTestDefinition(Inventory, EItemCategory::Guard);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("Legacy definition can be created."), LegacyDefinition);
	if (!Inventory || !LegacyDefinition)
	{
		return false;
	}

	FInventoryItemSnapshot Snapshot;
	Snapshot.ItemId = FGuid::NewGuid();
	Snapshot.Definition = LegacyDefinition;
	Snapshot.DefinitionKey = NAME_None;
	Snapshot.InventorySize = FIntPoint(1, 1);
	Snapshot.EquippedSlot = EEquipmentSlot::Guard;
	Snapshot.SlotIndex = INDEX_NONE;

	FAeyerjiInventorySaveData SaveData;
	SaveData.ItemSnapshots.Add(Snapshot);
	SaveData.GridColumns = 4;
	SaveData.GridRows = 4;

	Inventory->ApplySaveData(SaveData);

	UAeyerjiItemInstance* RestoredItem = Inventory->FindItemById(Snapshot.ItemId);
	TestNotNull(TEXT("Legacy snapshot restores item through Definition fallback."), RestoredItem);
	if (RestoredItem)
	{
		TestEqual(TEXT("Restored item keeps legacy definition."), RestoredItem->Definition.Get(), LegacyDefinition);
	}

	FAeyerjiInventorySaveData RoundTrip = Inventory->BuildSaveData();
	TestEqual(TEXT("Roundtrip has one snapshot."), RoundTrip.ItemSnapshots.Num(), 1);
	if (RoundTrip.ItemSnapshots.Num() == 1)
	{
		TestFalse(TEXT("Roundtrip snapshot now has stable definition key."), RoundTrip.ItemSnapshots[0].DefinitionKey.IsNone());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventorySaveDataAttributeSanitizerTest,
	"Aeyerji.Inventory.SaveDataAttributeSanitizer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventorySaveDataAttributeSanitizerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FItemStatModifier ValidModifier;
	ValidModifier.Attribute = UAeyerjiAttributeSet::GetAttackDamageAttribute();
	ValidModifier.Op = EItemModOp::Additive;
	ValidModifier.Magnitude = 10.f;

	FItemStatModifier InvalidModifier;
	InvalidModifier.Op = EItemModOp::Additive;
	InvalidModifier.Magnitude = 99.f;

	FRolledAffix RolledAffix;
	RolledAffix.AffixId = TEXT("Automation_InvalidAttribute");
	RolledAffix.FinalModifiers.Add(InvalidModifier);
	RolledAffix.FinalModifiers.Add(ValidModifier);

	FInventoryItemSnapshot Snapshot;
	Snapshot.ItemId = FGuid::NewGuid();
	Snapshot.FinalAggregatedModifiers.Add(ValidModifier);
	Snapshot.FinalAggregatedModifiers.Add(InvalidModifier);
	Snapshot.RolledAffixes.Add(RolledAffix);

	FAeyerjiInventorySaveData SaveData;
	SaveData.ItemSnapshots.Add(Snapshot);

	const int32 RemovedCount = UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(SaveData);
	TestEqual(TEXT("Invalid aggregate and rolled-affix attributes are removed."), RemovedCount, 2);
	TestEqual(TEXT("Aggregate modifiers keep the valid attribute."), SaveData.ItemSnapshots[0].FinalAggregatedModifiers.Num(), 1);
	TestEqual(TEXT("Rolled affix modifiers keep the valid attribute."), SaveData.ItemSnapshots[0].RolledAffixes[0].FinalModifiers.Num(), 1);
	TestTrue(TEXT("Valid aggregate attribute is preserved."),
		SaveData.ItemSnapshots[0].FinalAggregatedModifiers[0].Attribute == UAeyerjiAttributeSet::GetAttackDamageAttribute());
	TestTrue(TEXT("Valid rolled affix attribute is preserved."),
		SaveData.ItemSnapshots[0].RolledAffixes[0].FinalModifiers[0].Attribute == UAeyerjiAttributeSet::GetAttackDamageAttribute());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventorySlotUnlockThresholdTest,
	"Aeyerji.Inventory.SlotUnlockThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventorySlotUnlockThresholdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	if (!Inventory)
	{
		return false;
	}

	constexpr int32 MaxNormalSlots = 5;
	TestEqual(TEXT("Level 1 unlocks one normal slot."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(1, MaxNormalSlots), 1);
	TestEqual(TEXT("Level 9 still unlocks one normal slot."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(9, MaxNormalSlots), 1);
	TestEqual(TEXT("Level 10 unlocks two normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(10, MaxNormalSlots), 2);
	TestEqual(TEXT("Level 20 unlocks three normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(20, MaxNormalSlots), 3);
	TestEqual(TEXT("Level 30 unlocks four normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(30, MaxNormalSlots), 4);
	TestEqual(TEXT("Level 40 unlocks five normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(40, MaxNormalSlots), 5);
	TestEqual(TEXT("Level 50 keeps five normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(50, MaxNormalSlots), 5);
	TestEqual(TEXT("Legacy four-slot configs are promoted to five normal slots."), UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(50, 4), 5);

	Inventory->SetAutomationOwnerLevelForInventoryRules(1);
	TestEqual(TEXT("Assault uses level-gated normal slots at level 1."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Assault), 1);
	TestEqual(TEXT("Guard uses level-gated normal slots at level 1."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Guard), 1);
	TestEqual(TEXT("Flow uses level-gated normal slots at level 1."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Flow), 1);
	TestEqual(TEXT("Corruption slots are hidden while locked."), Inventory->GetVisibleEquipmentSlotCount(EEquipmentSlot::Corruption), 0);
	TestEqual(TEXT("Corruption slots are locked at level 1."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption), 0);

	Inventory->SetAutomationOwnerLevelForInventoryRules(40);
	TestEqual(TEXT("Assault unlocks five slots at level 40."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Assault), 5);
	Inventory->SlotsPerEquipmentCategory = 4;
	TestEqual(TEXT("Legacy four-slot component defaults still show five normal slots."), Inventory->GetVisibleEquipmentSlotCount(EEquipmentSlot::Assault), 5);
	TestEqual(TEXT("Legacy four-slot component defaults still unlock five normal slots at level 40."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Assault), 5);

	Inventory->SetAutomationOwnerLevelForInventoryRules(50);
	TestEqual(TEXT("Corruption slots become visible at level 50."), Inventory->GetVisibleEquipmentSlotCount(EEquipmentSlot::Corruption), 3);
	TestEqual(TEXT("Corruption unlocks three slots at level 50."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryNormalLaneCompatibilityTest,
	"Aeyerji.Inventory.NormalLaneCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryNormalLaneCompatibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	Inventory->SetAutomationOwnerLevelForInventoryRules(10);
	UItemDefinition* AssaultDefinition = MakeTestDefinition(Inventory, EItemCategory::Assault);
	UItemDefinition* GuardDefinition = MakeTestDefinition(Inventory, EItemCategory::Guard);
	UItemDefinition* FlowDefinition = MakeTestDefinition(Inventory, EItemCategory::Flow);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("Assault definition can be created."), AssaultDefinition);
	TestNotNull(TEXT("Guard definition can be created."), GuardDefinition);
	TestNotNull(TEXT("Flow definition can be created."), FlowDefinition);
	if (!Inventory || !AssaultDefinition || !GuardDefinition || !FlowDefinition)
	{
		return false;
	}

	UAeyerjiItemInstance* AssaultItem = MakeTestItem(Inventory, AssaultDefinition, 1);
	UAeyerjiItemInstance* GuardItem = MakeTestItem(Inventory, GuardDefinition, 1);
	UAeyerjiItemInstance* FlowItem = MakeTestItem(Inventory, FlowDefinition, 1);
	Inventory->AddItemInstance(AssaultItem, true);
	Inventory->AddItemInstance(GuardItem, true);
	Inventory->AddItemInstance(FlowItem, true);

	Inventory->Server_EquipItem(AssaultItem->UniqueId, EEquipmentSlot::Assault, 0);
	Inventory->Server_EquipItem(GuardItem->UniqueId, EEquipmentSlot::Guard, 0);
	Inventory->Server_EquipItem(FlowItem->UniqueId, EEquipmentSlot::Flow, 0);

	TestNotNull(TEXT("Normal item equips in Assault."), Inventory->GetEquipped(EEquipmentSlot::Assault, 0));
	TestNotNull(TEXT("Normal item equips in Guard."), Inventory->GetEquipped(EEquipmentSlot::Guard, 0));
	TestNotNull(TEXT("Normal item equips in Flow."), Inventory->GetEquipped(EEquipmentSlot::Flow, 0));
	TestFalse(TEXT("Assault item cannot equip in Guard."), Inventory->CanEquipItemInSlot(AssaultItem, EEquipmentSlot::Guard, 0));
	TestFalse(TEXT("Guard item cannot equip in Flow."), Inventory->CanEquipItemInSlot(GuardItem, EEquipmentSlot::Flow, 0));
	TestFalse(TEXT("Flow item cannot equip in Assault."), Inventory->CanEquipItemInSlot(FlowItem, EEquipmentSlot::Assault, 0));
	TestFalse(TEXT("Normal item cannot equip in Corruption."), Inventory->CanEquipItemInSlot(AssaultItem, EEquipmentSlot::Corruption, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryCorruptionUnlockTest,
	"Aeyerji.Inventory.CorruptionUnlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryCorruptionUnlockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	UItemDefinition* CorruptionDefinition = MakeTestDefinition(Inventory, EItemCategory::Corruption);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("Corruption definition can be created."), CorruptionDefinition);
	if (!Inventory || !CorruptionDefinition)
	{
		return false;
	}

	UAeyerjiItemInstance* First = MakeTestItem(Inventory, CorruptionDefinition, 50);
	UAeyerjiItemInstance* Second = MakeTestItem(Inventory, CorruptionDefinition, 50);
	UAeyerjiItemInstance* Third = MakeTestItem(Inventory, CorruptionDefinition, 50);
	Inventory->AddItemInstance(First, true);
	Inventory->AddItemInstance(Second, true);
	Inventory->AddItemInstance(Third, true);

	Inventory->SetAutomationOwnerLevelForInventoryRules(49);
	TestEqual(TEXT("Corruption slots are locked below level 50."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption), 0);
	TestFalse(TEXT("Corruption item cannot equip below level 50."), Inventory->CanEquipItemInSlot(First, EEquipmentSlot::Corruption, 0));
	Inventory->Server_EquipItem(First->UniqueId, EEquipmentSlot::Corruption, 0);
	TestNull(TEXT("Corruption item remains unequipped below level 50."), Inventory->GetEquipped(EEquipmentSlot::Corruption, 0));

	Inventory->SetAutomationOwnerLevelForInventoryRules(50);
	TestEqual(TEXT("Exactly three Corruption slots unlock at level 50."), Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption), 3);
	TestTrue(TEXT("Corruption item can equip in slot 0 at level 50."), Inventory->CanEquipItemInSlot(First, EEquipmentSlot::Corruption, 0));
	TestFalse(TEXT("Corruption item cannot equip in normal lane."), Inventory->CanEquipItemInSlot(First, EEquipmentSlot::Assault, 0));

	Inventory->Server_EquipItem(First->UniqueId, EEquipmentSlot::Corruption, 0);
	Inventory->Server_EquipItem(Second->UniqueId, EEquipmentSlot::Corruption, 1);
	Inventory->Server_EquipItem(Third->UniqueId, EEquipmentSlot::Corruption, 2);

	TestNotNull(TEXT("Corruption slot 0 is occupied."), Inventory->GetEquipped(EEquipmentSlot::Corruption, 0));
	TestNotNull(TEXT("Corruption slot 1 is occupied."), Inventory->GetEquipped(EEquipmentSlot::Corruption, 1));
	TestNotNull(TEXT("Corruption slot 2 is occupied."), Inventory->GetEquipped(EEquipmentSlot::Corruption, 2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryEquipmentStatContributionTest,
	"Aeyerji.Inventory.EquipmentStatContributions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryEquipmentStatContributionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAbilitySystemComponent* ASC = nullptr;
	UAeyerjiInventoryComponent* Inventory = MakeTestInventoryWithASC(GetTransientPackage(), ASC);
	Inventory->SetAutomationOwnerLevelForInventoryRules(50);
	UItemDefinition* Definition = MakeTestDefinition(Inventory, EItemCategory::Assault);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("ASC can be created."), ASC);
	TestNotNull(TEXT("Definition can be created."), Definition);
	if (!Inventory || !ASC || !Definition)
	{
		return false;
	}

	FItemStatModifier DamageModifier;
	DamageModifier.Attribute = UAeyerjiAttributeSet::GetAttackDamageAttribute();
	DamageModifier.Op = EItemModOp::Additive;
	DamageModifier.Magnitude = 10.f;
	Definition->BaseModifiers.Add(DamageModifier);

	FItemStatModifier CritModifier;
	CritModifier.Attribute = UAeyerjiAttributeSet::GetCritChanceAttribute();
	CritModifier.Op = EItemModOp::Additive;
	CritModifier.Magnitude = 100.f;
	Definition->BaseModifiers.Add(CritModifier);

	FItemStatModifier EnemyOnlyModifier;
	EnemyOnlyModifier.Attribute = UAeyerjiAttributeSet::GetVisionRangeAttribute();
	EnemyOnlyModifier.Op = EItemModOp::Additive;
	EnemyOnlyModifier.Magnitude = 500.f;
	Definition->BaseModifiers.Add(EnemyOnlyModifier);

	UAeyerjiItemInstance* Item = MakeTestItem(Inventory, Definition, 1);
	Item->RebuildAggregation();
	Inventory->AddItemInstance(Item, true);

	const float BaselineDamage = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
	const float BaselineCrit = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetCritChanceAttribute());
	const float BaselineVision = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetVisionRangeAttribute());

	Inventory->Server_EquipItem(Item->UniqueId, EEquipmentSlot::Assault, 0);
	TestTrue(TEXT("AttackDamage contribution applies once."),
		FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute()), BaselineDamage + 10.f));
	TestTrue(TEXT("CritChance raw 100 normalizes to 1.0."),
		FMath::IsNearlyEqual(Inventory->GetCurrentEquipmentStatContribution(UAeyerjiAttributeSet::GetCritChanceAttribute()), 1.f));
	TestTrue(TEXT("Enemy-only VisionRange is ignored by player equipment."),
		FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetVisionRangeAttribute()), BaselineVision));

	Inventory->Server_UnequipSlot(EEquipmentSlot::Assault, 0);
	TestTrue(TEXT("AttackDamage returns to baseline after unequip."),
		FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute()), BaselineDamage));
	TestTrue(TEXT("CritChance returns to baseline after unequip."),
		FMath::IsNearlyEqual(ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetCritChanceAttribute()), BaselineCrit));

	if (AActor* TestOwner = Inventory->GetOwner(); TestOwner && TestOwner->GetWorld())
	{
		TestOwner->Destroy();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryEquipmentEffectLifecycleTest,
	"Aeyerji.Inventory.EquipmentEffectLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryEquipmentEffectLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAbilitySystemComponent* ASC = nullptr;
	UAeyerjiInventoryComponent* Inventory = MakeTestInventoryWithASC(GetTransientPackage(), ASC);
	Inventory->SetAutomationOwnerLevelForInventoryRules(50);
	UItemDefinition* Definition = MakeTestDefinition(Inventory, EItemCategory::Assault);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("ASC can be created."), ASC);
	TestNotNull(TEXT("Definition can be created."), Definition);
	if (!Inventory || !ASC || !Definition)
	{
		return false;
	}

	FItemGrantedEffect PersistentEffect;
	PersistentEffect.EffectClass = UGE_Regen_Periodic::StaticClass();
	PersistentEffect.EffectLevel = 1.f;
	Definition->GrantedEffects.Add(PersistentEffect);

	UAeyerjiItemInstance* FirstItem = MakeTestItem(Inventory, Definition, 1);
	UAeyerjiItemInstance* ReplacementItem = MakeTestItem(Inventory, Definition, 1);
	FirstItem->RebuildAggregation();
	ReplacementItem->RebuildAggregation();
	Inventory->AddItemInstance(FirstItem, true);
	Inventory->AddItemInstance(ReplacementItem, true);

	auto CountActiveEffects = [](const UAbilitySystemComponent* AbilitySystem)
	{
		return AbilitySystem
			? AbilitySystem->GetActiveEffects(FGameplayEffectQuery()).Num()
			: 0;
	};

	Inventory->Server_EquipItem(FirstItem->UniqueId, EEquipmentSlot::Assault, 0);
	TestEqual(TEXT("Equipping applies one persistent granted effect."), CountActiveEffects(ASC), 1);
	Inventory->Server_EquipItem(FirstItem->UniqueId, EEquipmentSlot::Assault, 0);
	TestEqual(TEXT("Re-equipping the same item cannot duplicate its effect."), CountActiveEffects(ASC), 1);

	Inventory->Server_EquipItem(ReplacementItem->UniqueId, EEquipmentSlot::Assault, 0);
	TestEqual(TEXT("Replacement removes the prior effect and applies one new effect."), CountActiveEffects(ASC), 1);
	TestEqual(TEXT("Replacement occupies the requested slot."),
		Inventory->GetEquipped(EEquipmentSlot::Assault, 0), ReplacementItem);

	const FAeyerjiInventorySaveData SavedInventory = Inventory->BuildSaveData();
	UAbilitySystemComponent* ReconnectedASC = nullptr;
	UAeyerjiInventoryComponent* ReconnectedInventory = MakeTestInventoryWithASC(GetTransientPackage(), ReconnectedASC);
	ReconnectedInventory->SetAutomationOwnerLevelForInventoryRules(50);
	ReconnectedInventory->ApplySaveData(SavedInventory);
	TestEqual(TEXT("Reconnect hydration restores one equipped effect."), CountActiveEffects(ReconnectedASC), 1);
	TestEqual(TEXT("Reconnect hydration does not duplicate item ownership."),
		ReconnectedInventory->BuildSaveData().ItemSnapshots.Num(), 2);

	ReconnectedInventory->ApplySaveData(SavedInventory);
	TestEqual(TEXT("Repeated hydration cleans old handles before restoring effects."), CountActiveEffects(ReconnectedASC), 1);
	TestEqual(TEXT("Repeated hydration cannot duplicate items."),
		ReconnectedInventory->BuildSaveData().ItemSnapshots.Num(), 2);

	ReconnectedInventory->Server_UnequipSlot(EEquipmentSlot::Assault, 0);
	TestEqual(TEXT("Unequip removes the persistent granted effect."), CountActiveEffects(ReconnectedASC), 0);

	if (AActor* Owner = Inventory->GetOwner(); Owner && Owner->GetWorld())
	{
		Owner->Destroy();
	}
	if (AActor* Owner = ReconnectedInventory->GetOwner(); Owner && Owner->GetWorld())
	{
		Owner->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiInventoryLockedCorruptionSaveLoadTest,
	"Aeyerji.Inventory.LockedCorruptionSaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiInventoryLockedCorruptionSaveLoadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiInventoryComponent* Inventory = MakeTestInventory(GetTransientPackage());
	Inventory->SetAutomationOwnerLevelForInventoryRules(49);
	UItemDefinition* CorruptionDefinition = MakeTestDefinition(Inventory, EItemCategory::Corruption);
	TestNotNull(TEXT("Inventory component can be created."), Inventory);
	TestNotNull(TEXT("Corruption definition can be created."), CorruptionDefinition);
	if (!Inventory || !CorruptionDefinition)
	{
		return false;
	}

	FInventoryItemSnapshot Snapshot;
	Snapshot.ItemId = FGuid::NewGuid();
	Snapshot.Definition = CorruptionDefinition;
	Snapshot.DefinitionKey = NAME_None;
	Snapshot.ItemLevel = 50;
	Snapshot.InventorySize = FIntPoint(1, 1);
	Snapshot.EquippedSlot = EEquipmentSlot::Corruption;
	Snapshot.SlotIndex = 0;

	FEquippedItemEntry EquippedEntry;
	EquippedEntry.ItemId = Snapshot.ItemId;
	EquippedEntry.Slot = EEquipmentSlot::Corruption;
	EquippedEntry.SlotIndex = 0;

	FAeyerjiInventorySaveData SaveData;
	SaveData.ItemSnapshots.Add(Snapshot);
	SaveData.EquippedItems.Add(EquippedEntry);
	SaveData.GridColumns = 4;
	SaveData.GridRows = 4;

	Inventory->ApplySaveData(SaveData);

	UAeyerjiItemInstance* RestoredItem = Inventory->FindItemById(Snapshot.ItemId);
	TestNotNull(TEXT("Locked corruption save restores item ownership."), RestoredItem);
	TestNull(TEXT("Locked corruption save does not restore equipped state."), Inventory->GetEquipped(EEquipmentSlot::Corruption, 0));

	FInventoryItemGridData Placement;
	TestTrue(TEXT("Locked corruption item is placed in the bag."), Inventory->GetPlacementForItem(Snapshot.ItemId, Placement));

	return true;
}

#endif
