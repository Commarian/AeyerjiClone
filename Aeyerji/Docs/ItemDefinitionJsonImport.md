# Item Definition JSON Import

`UItemDefinition` remains the cooked in-game representation. JSON is only the bulk authoring source used by the editor commandlet to create or update item definition assets.

## Command

```powershell
%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe X:\UnrealProjects\Aeyerji\Aeyerji\Aeyerji.uproject -run=ItemDefinitionJsonImport -Json="X:\UnrealProjects\Aeyerji\Aeyerji\Source\Aeyerji\Data\Items.json" -Dest="/Game/Inventory/Items/Definitions"
```

Options:
- `-Json=<path>`: Required source JSON file.
- `-Dest=<path>`: Optional default package path. Defaults to `/Game/Inventory/Items/Definitions`.
- `-NoSave`: Updates packages in memory without saving them.
- `-DryRun`: Parses item entries and reports what would be imported.

## JSON Shape

The root can be a single item object, an array of item objects, or an object with `DestinationPath` and `Items`.

```json
{
  "DestinationPath": "/Game/Inventory/Items/Definitions",
  "Items": [
    {
      "AssetName": "DI_FlameShard",
      "DisplayName": "Flame Shard",
      "Description": "A damage item that adds fire pressure.",
      "ItemCategory": "Assault",
      "DefaultSlot": "Assault",
      "RequiredLevel": 1,
      "ItemTags": ["Item.Assault", "Damage.Type.Fire"],
      "CorruptionPowerText": "",
      "CorruptionDrawbackText": "",
      "InventorySize": [1, 1],
      "Icon": "/Game/UI/Icons/T_FlameShard.T_FlameShard",
      "WorldMesh": "/Game/Inventory/Meshes/SM_FlameShard.SM_FlameShard",
      "WorldMeshOffset": [0, 0, 12],
      "WorldMeshRotation": [0, 0, 0],
      "WorldMeshScale": [1, 1, 1],
      "RarityAffixRanges": [
        { "Rarity": "Common", "MinAffixes": 0, "MaxAffixes": 0 },
        { "Rarity": "Rare", "MinAffixes": 2, "MaxAffixes": 3 }
      ],
      "BaseModifiers": [
        { "Attribute": "AeyerjiAttributeSet.AttackDamage", "Op": "Additive", "Magnitude": 5 }
      ],
      "GrantedEffects": [
        {
          "EffectClass": "/Game/GAS/Effects/GE_FlameItem.GE_FlameItem_C",
          "EffectLevel": 1,
          "ApplicationTags": ["Damage.Type.Fire"]
        }
      ],
      "GrantedAbilities": [],
      "AffixPool": [],
      "PickupVisuals": {
        "PickupGrantedSystem": "/Game/FX/NS_ItemPickup.NS_ItemPickup",
        "ColorParameter": "PickupColor",
        "FXColor": [1, 0.25, 0.05, 1]
      },
      "bEnableEquipSynergy": false,
      "EquipSynergyColors": []
    }
  ]
}
```

Arrays present in JSON replace the asset arrays. Missing fields leave the existing asset value unchanged, so empty arrays are how you intentionally clear list fields.
