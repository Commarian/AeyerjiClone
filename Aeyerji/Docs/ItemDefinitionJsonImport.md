# Item Definition JSON Import

`UItemDefinition` remains the cooked in-game representation. JSON is only the bulk authoring source used by the editor commandlet to create or update item definition assets.

## Command

```powershell
%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe X:\UnrealProjects\Aeyerji\Aeyerji\Aeyerji.uproject -run=ItemDefinitionJsonImport -Json="X:\UnrealProjects\Aeyerji\Aeyerji\Source\Aeyerji\Data\Items.json" -Dest="/Game/Inventory/Items/Definitions"
```

Options:

- `-Json=<path>`: Required source JSON file.
- `-Dest=<path>`: Optional default package path. Defaults to `/Game/Inventory/Items/Definitions` and must be a valid `/Game/...` package path.
- `-NoSave`: Updates packages in memory without saving them.
- `-DryRun`: Parses item entries and reports what would be imported.

## Validation and limits

The commandlet rejects input larger than 16 MiB, invalid JSON, invalid destination paths, incompatible existing objects, and package save failures. One import accepts at most 4,096 items. Per item, it bounds imported collections to 256 gameplay tags, 64 rarity ranges, 64 affixes, 2,048 base modifiers, 256 granted effects, 256 granted abilities, 64 set-by-caller magnitudes per effect, and 64 synergy colors. Imported text fields are limited to 16,384 characters.

Numeric values must be finite. Counts, levels, transforms, colors, probabilities, durations, stencil values, and other authored numbers are normalized to their supported runtime ranges before assets are updated. Oversized arrays are truncated with bounded parsing; review the commandlet log after every bulk import.

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

Run with `-DryRun` first for large authoring changes. The commandlet can create or update native `UItemDefinition` assets, but it does not modify Blueprint graphs or other binary content assets.
