// ItemDefinitionJsonImportCommandlet.cpp

#include "Editor/ItemDefinitionJsonImportCommandlet.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include <type_traits>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogItemDefinitionJsonImport, Log, All);

UItemDefinitionJsonImportCommandlet::UItemDefinitionJsonImportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

#if WITH_EDITOR
namespace
{
constexpr TCHAR DefaultItemDefinitionDestination[] = TEXT("/Game/Inventory/Items/Definitions");
constexpr int64 MaxImportJsonBytes = 16ll * 1024ll * 1024ll;
constexpr int32 MaxImportedItems = 4096;
constexpr int32 MaxImportedTags = 256;
constexpr int32 MaxImportedAffixes = 64;
constexpr int32 MaxImportedModifiers = 2048;
constexpr int32 MaxImportedGrants = 256;
constexpr int32 MaxImportedSetByCallerMagnitudes = 64;
constexpr int32 MaxImportedSynergyColors = 64;
constexpr int32 MaxImportedTextLength = 16384;

struct FItemImportReport
{
	int32 Created = 0;
	int32 Updated = 0;
	int32 Failed = 0;
	int32 Warnings = 0;

	void Warn(const FString& Message)
	{
		++Warnings;
		UE_LOG(LogItemDefinitionJsonImport, Warning, TEXT("%s"), *Message);
	}

	void Error(const FString& Message)
	{
		++Failed;
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("%s"), *Message);
	}
};

/** Returns a JSON field when present, including explicit null values. */
const TSharedPtr<FJsonValue>* FindJsonField(const FJsonObject& Object, const TCHAR* FieldName)
{
	return Object.Values.Find(FieldName);
}

/** Reads a string field without treating missing fields as an error. */
bool TryGetString(const FJsonObject& Object, const TCHAR* FieldName, FString& OutValue)
{
	const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, FieldName);
	if (!Value || !Value->IsValid() || (*Value)->Type == EJson::Null)
	{
		return false;
	}

	if ((*Value)->Type != EJson::String)
	{
		return false;
	}

	OutValue = (*Value)->AsString().Left(MaxImportedTextLength);
	return true;
}

/** Reads one finite JSON number and bounds it before any integer conversion. */
bool TryReadFiniteNumber(
	const TSharedPtr<FJsonValue>& Value,
	double& OutValue,
	const double MinValue,
	const double MaxValue)
{
	if (!Value.IsValid() || Value->Type != EJson::Number)
	{
		return false;
	}

	const double Number = Value->AsNumber();
	if (!FMath::IsFinite(Number))
	{
		return false;
	}

	OutValue = FMath::Clamp(Number, MinValue, MaxValue);
	return true;
}

/** Reads an optional finite number field and preserves the current output when absent or malformed. */
bool TryApplyFiniteNumberField(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	float& OutValue,
	const float MinValue,
	const float MaxValue)
{
	const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, FieldName);
	double Number = 0.0;
	if (!Value || !TryReadFiniteNumber(*Value, Number, MinValue, MaxValue))
	{
		return false;
	}

	OutValue = static_cast<float>(Number);
	return true;
}

bool TryApplyBoundedIntField(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	int32& OutValue,
	const int32 MinValue,
	const int32 MaxValue)
{
	const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, FieldName);
	double Number = 0.0;
	if (!Value || !TryReadFiniteNumber(*Value, Number, MinValue, MaxValue))
	{
		return false;
	}

	OutValue = FMath::RoundToInt(Number);
	return true;
}

bool TryReadOptionalObjectNumber(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	const double DefaultValue,
	double& OutValue,
	const double MinValue,
	const double MaxValue)
{
	const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, FieldName);
	if (!Value)
	{
		OutValue = FMath::Clamp(DefaultValue, MinValue, MaxValue);
		return true;
	}
	return TryReadFiniteNumber(*Value, OutValue, MinValue, MaxValue);
}

/** Reads an enum value by C++ name, short name, or display name. */
template <typename TEnum>
bool TryParseEnumValue(const FString& Text, TEnum& OutValue)
{
	const UEnum* Enum = StaticEnum<TEnum>();
	if (!Enum)
	{
		return false;
	}

	const FString Trimmed = Text.TrimStartAndEnd();
	FString Alias = Trimmed;
	if constexpr (std::is_same_v<TEnum, EItemCategory> || std::is_same_v<TEnum, EEquipmentSlot>)
	{
		if (Trimmed.Equals(TEXT("Offense"), ESearchCase::IgnoreCase))
		{
			Alias = TEXT("Assault");
		}
		else if (Trimmed.Equals(TEXT("Defense"), ESearchCase::IgnoreCase))
		{
			Alias = TEXT("Guard");
		}
		else if (Trimmed.Equals(TEXT("Magic"), ESearchCase::IgnoreCase))
		{
			Alias = TEXT("Flow");
		}
	}

	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const FString Name = Enum->GetNameStringByIndex(Index);
		const FString DisplayName = Enum->GetDisplayNameTextByIndex(Index).ToString();
		const FString QualifiedName = Enum->GetNameByIndex(Index).ToString();

		if (Name.Equals(Alias, ESearchCase::IgnoreCase)
			|| DisplayName.Equals(Alias, ESearchCase::IgnoreCase)
			|| QualifiedName.Equals(Alias, ESearchCase::IgnoreCase))
		{
			OutValue = static_cast<TEnum>(Enum->GetValueByIndex(Index));
			return true;
		}
	}

	return false;
}

/** Reads an enum JSON field without changing the output when the field is absent. */
template <typename TEnum>
bool TryApplyEnumField(const FJsonObject& Object, const TCHAR* FieldName, TEnum& OutValue, FItemImportReport& Report, const FString& ItemName)
{
	FString Text;
	if (!TryGetString(Object, FieldName, Text))
	{
		return false;
	}

	TEnum ParsedValue;
	if (!TryParseEnumValue(Text, ParsedValue))
	{
		Report.Warn(FString::Printf(TEXT("%s: invalid enum value '%s' for %s."), *ItemName, *Text, FieldName));
		return false;
	}

	OutValue = ParsedValue;
	return true;
}

/** Normalizes /Game/Foo/Bar into /Game/Foo/Bar.Bar so LoadObject can resolve it. */
FString NormalizeObjectPath(FString Path)
{
	Path.TrimStartAndEndInline();
	if (Path.IsEmpty() || Path.Contains(TEXT(".")))
	{
		return Path;
	}

	if (Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Script/")))
	{
		FString PackagePath;
		FString AssetName;
		Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!AssetName.IsEmpty())
		{
			return Path + TEXT(".") + AssetName;
		}
	}

	return Path;
}

/** Loads an optional object reference from a string field. Empty strings clear the reference. */
template <typename TObject>
void ApplyObjectField(const FJsonObject& Object, const TCHAR* FieldName, TObjectPtr<TObject>& OutObject, FItemImportReport& Report, const FString& ItemName)
{
	const TSharedPtr<FJsonValue>* JsonValue = FindJsonField(Object, FieldName);
	if (!JsonValue || !JsonValue->IsValid())
	{
		return;
	}

	if ((*JsonValue)->Type == EJson::Null)
	{
		OutObject = nullptr;
		return;
	}

	FString Path;
	if (!TryGetString(Object, FieldName, Path))
	{
		Report.Warn(FString::Printf(TEXT("%s: %s must be an asset path string."), *ItemName, FieldName));
		return;
	}

	Path = NormalizeObjectPath(Path);
	if (Path.Len() > 1024)
	{
		Report.Warn(FString::Printf(TEXT("%s: %s asset path is too long."), *ItemName, FieldName));
		return;
	}
	OutObject = Path.IsEmpty() ? nullptr : LoadObject<TObject>(nullptr, *Path);
	if (!Path.IsEmpty() && !OutObject)
	{
		Report.Warn(FString::Printf(TEXT("%s: failed to load asset '%s' for %s."), *ItemName, *Path, FieldName));
	}
}

/** Loads an optional generated or native class reference from a string field. */
template <typename TClass>
void ApplyClassField(const FJsonObject& Object, const TCHAR* FieldName, TSubclassOf<TClass>& OutClass, FItemImportReport& Report, const FString& ItemName)
{
	const TSharedPtr<FJsonValue>* JsonValue = FindJsonField(Object, FieldName);
	if (!JsonValue || !JsonValue->IsValid())
	{
		return;
	}

	if ((*JsonValue)->Type == EJson::Null)
	{
		OutClass = nullptr;
		return;
	}

	FString Path;
	if (!TryGetString(Object, FieldName, Path))
	{
		Report.Warn(FString::Printf(TEXT("%s: %s must be a class path string."), *ItemName, FieldName));
		return;
	}

	Path = NormalizeObjectPath(Path);
	if (Path.Len() > 1024)
	{
		Report.Warn(FString::Printf(TEXT("%s: %s class path is too long."), *ItemName, FieldName));
		return;
	}
	OutClass = Path.IsEmpty() ? nullptr : FSoftClassPath(Path).TryLoadClass<TClass>();
	if (!Path.IsEmpty() && !*OutClass)
	{
		Report.Warn(FString::Printf(TEXT("%s: failed to load class '%s' for %s."), *ItemName, *Path, FieldName));
	}
}

/** Reads gameplay tags from either ["A.B"] or "A.B". */
void ApplyGameplayTags(const FJsonObject& Object, const TCHAR* FieldName, FGameplayTagContainer& OutTags, FItemImportReport& Report, const FString& ItemName)
{
	const TSharedPtr<FJsonValue>* JsonValue = FindJsonField(Object, FieldName);
	if (!JsonValue || !JsonValue->IsValid())
	{
		return;
	}

	OutTags.Reset();
	auto AddTag = [&OutTags, &Report, &ItemName, FieldName](const FString& TagText)
	{
		if (OutTags.Num() >= MaxImportedTags || TagText.Len() > 256)
		{
			return;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagText), false);
		if (Tag.IsValid())
		{
			OutTags.AddTag(Tag);
		}
		else
		{
			Report.Warn(FString::Printf(TEXT("%s: invalid gameplay tag '%s' in %s."), *ItemName, *TagText, FieldName));
		}
	};

	if ((*JsonValue)->Type == EJson::String)
	{
		AddTag((*JsonValue)->AsString());
		return;
	}

	if ((*JsonValue)->Type != EJson::Array)
	{
		Report.Warn(FString::Printf(TEXT("%s: %s must be a string or string array."), *ItemName, FieldName));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& TagEntries = (*JsonValue)->AsArray();
	const int32 TagEntryCount = FMath::Min(TagEntries.Num(), MaxImportedTags);
	for (int32 TagEntryIndex = 0; TagEntryIndex < TagEntryCount; ++TagEntryIndex)
	{
		const TSharedPtr<FJsonValue>& Entry = TagEntries[TagEntryIndex];
		if (Entry.IsValid() && Entry->Type == EJson::String)
		{
			AddTag(Entry->AsString());
		}
	}
}

/** Reads [X,Y], {"X":1,"Y":1}, or a scalar into an inventory size. */
bool TryReadIntPoint(const TSharedPtr<FJsonValue>& Value, FIntPoint& OutPoint)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() >= 2)
		{
			double X = 0.0;
			double Y = 0.0;
			if (!TryReadFiniteNumber(Values[0], X, 1.0, 64.0)
				|| !TryReadFiniteNumber(Values[1], Y, 1.0, 64.0))
			{
				return false;
			}
			OutPoint.X = FMath::RoundToInt(X);
			OutPoint.Y = FMath::RoundToInt(Y);
			return true;
		}
	}
	else if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		double X = 0.0;
		double Y = 0.0;
		if (!TryReadOptionalObjectNumber(*Object, TEXT("X"), OutPoint.X, X, 1.0, 64.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("Y"), OutPoint.Y, Y, 1.0, 64.0))
		{
			return false;
		}
		OutPoint = FIntPoint(FMath::RoundToInt(X), FMath::RoundToInt(Y));
		return true;
	}
	else if (Value->Type == EJson::Number)
	{
		double Size = 0.0;
		if (TryReadFiniteNumber(Value, Size, 1.0, 64.0))
		{
			OutPoint = FIntPoint(FMath::RoundToInt(Size));
			return true;
		}
	}

	return false;
}

/** Reads [X,Y,Z] or {"X":0,"Y":0,"Z":0} into a vector. */
bool TryReadVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() >= 3)
		{
			double X = 0.0;
			double Y = 0.0;
			double Z = 0.0;
			if (!TryReadFiniteNumber(Values[0], X, -10000000.0, 10000000.0)
				|| !TryReadFiniteNumber(Values[1], Y, -10000000.0, 10000000.0)
				|| !TryReadFiniteNumber(Values[2], Z, -10000000.0, 10000000.0))
			{
				return false;
			}
			OutVector = FVector(X, Y, Z);
			return true;
		}
	}
	else if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!TryReadOptionalObjectNumber(*Object, TEXT("X"), OutVector.X, X, -10000000.0, 10000000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("Y"), OutVector.Y, Y, -10000000.0, 10000000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("Z"), OutVector.Z, Z, -10000000.0, 10000000.0))
		{
			return false;
		}
		OutVector = FVector(X, Y, Z);
		return true;
	}

	return false;
}

/** Reads [Pitch,Yaw,Roll] or {"Pitch":0,"Yaw":0,"Roll":0} into a rotator. */
bool TryReadRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() >= 3)
		{
			double Pitch = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			if (!TryReadFiniteNumber(Values[0], Pitch, -360000.0, 360000.0)
				|| !TryReadFiniteNumber(Values[1], Yaw, -360000.0, 360000.0)
				|| !TryReadFiniteNumber(Values[2], Roll, -360000.0, 360000.0))
			{
				return false;
			}
			OutRotator = FRotator(Pitch, Yaw, Roll).GetNormalized();
			return true;
		}
	}
	else if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		double Pitch = 0.0;
		double Yaw = 0.0;
		double Roll = 0.0;
		if (!TryReadOptionalObjectNumber(*Object, TEXT("Pitch"), OutRotator.Pitch, Pitch, -360000.0, 360000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("Yaw"), OutRotator.Yaw, Yaw, -360000.0, 360000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("Roll"), OutRotator.Roll, Roll, -360000.0, 360000.0))
		{
			return false;
		}
		OutRotator = FRotator(Pitch, Yaw, Roll).GetNormalized();
		return true;
	}

	return false;
}

/** Reads [R,G,B,A] or {"R":1,"G":1,"B":1,"A":1} into a linear color. */
bool TryReadLinearColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Value->AsArray();
		if (Values.Num() >= 3)
		{
			double R = 0.0;
			double G = 0.0;
			double B = 0.0;
			double A = FMath::Clamp(static_cast<double>(OutColor.A), 0.0, 1.0);
			if (!TryReadFiniteNumber(Values[0], R, 0.0, 1000.0)
				|| !TryReadFiniteNumber(Values[1], G, 0.0, 1000.0)
				|| !TryReadFiniteNumber(Values[2], B, 0.0, 1000.0)
				|| (Values.Num() >= 4 && !TryReadFiniteNumber(Values[3], A, 0.0, 1.0)))
			{
				return false;
			}
			OutColor = FLinearColor(R, G, B, A);
			return true;
		}
	}
	else if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		double R = 0.0;
		double G = 0.0;
		double B = 0.0;
		double A = 0.0;
		if (!TryReadOptionalObjectNumber(*Object, TEXT("R"), OutColor.R, R, 0.0, 1000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("G"), OutColor.G, G, 0.0, 1000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("B"), OutColor.B, B, 0.0, 1000.0)
			|| !TryReadOptionalObjectNumber(*Object, TEXT("A"), OutColor.A, A, 0.0, 1.0))
		{
			return false;
		}
		OutColor = FLinearColor(R, G, B, A);
		return true;
	}

	return false;
}

/** Applies all supported pickup visual fields from the nested PickupVisuals object. */
void ApplyPickupVisuals(const FJsonObject& Object, FAeyerjiPickupVisualConfig& OutVisuals, FItemImportReport& Report, const FString& ItemName)
{
	ApplyObjectField(Object, TEXT("PickupGrantedSystem"), OutVisuals.PickupGrantedSystem, Report, ItemName);
	ApplyObjectField(Object, TEXT("InventoryGrantedSystem"), OutVisuals.InventoryGrantedSystem, Report, ItemName);

	FString Text;
	if (TryGetString(Object, TEXT("AttachSocket"), Text))
	{
		OutVisuals.AttachSocket = FName(*Text.Left(256));
	}
	if (TryGetString(Object, TEXT("SecondaryAttachSocket"), Text))
	{
		OutVisuals.SecondaryAttachSocket = FName(*Text.Left(256));
	}
	if (TryGetString(Object, TEXT("ColorParameter"), Text))
	{
		OutVisuals.ColorParameter = FName(*Text.Left(256));
	}

	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("SpawnOffset")))
	{
		TryReadVector(*Value, OutVisuals.SpawnOffset);
	}
	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("FXColor")))
	{
		TryReadLinearColor(*Value, OutVisuals.FXColor);
	}

	Object.TryGetBoolField(TEXT("bPulseOutline"), OutVisuals.bPulseOutline);
	TryApplyFiniteNumberField(Object, TEXT("OutlinePulseDuration"), OutVisuals.OutlinePulseDuration, 0.f, 60.f);
	TryApplyFiniteNumberField(Object, TEXT("OutlinePulseFadeTime"), OutVisuals.OutlinePulseFadeTime, 0.f, 60.f);

	TryApplyBoundedIntField(
		Object, TEXT("OutlineStencilOverride"), OutVisuals.OutlineStencilOverride, -1, 255);
}

/** Replaces rarity affix ranges when the JSON field is present. */
void ApplyRarityAffixRanges(const FJsonObject& Object, UItemDefinition& Definition, FItemImportReport& Report, const FString& ItemName)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(TEXT("RarityAffixRanges"), Entries))
	{
		return;
	}

	Definition.RarityAffixRanges.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedAffixes);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FItemRarityAffixRange Entry;
		TryApplyEnumField(*EntryObject, TEXT("Rarity"), Entry.Rarity, Report, ItemName);
		TryApplyBoundedIntField(*EntryObject, TEXT("MinAffixes"), Entry.MinAffixes, 0, MaxImportedAffixes);
		TryApplyBoundedIntField(*EntryObject, TEXT("MaxAffixes"), Entry.MaxAffixes, 0, MaxImportedAffixes);
		if (Entry.MinAffixes > Entry.MaxAffixes)
		{
			Swap(Entry.MinAffixes, Entry.MaxAffixes);
		}
		Definition.RarityAffixRanges.Add(Entry);
	}
}

FGameplayAttribute ResolveJsonAttribute(const FString& AttributeText)
{
	if (AttributeText.Len() > 256)
	{
		return FGameplayAttribute();
	}
	FString NameString = AttributeText;
	int32 DotIndex = INDEX_NONE;
	if (NameString.FindChar(TEXT('.'), DotIndex))
	{
		NameString = NameString.Mid(DotIndex + 1);
	}

	const FName AttributeName(*NameString);
	if (FProperty* Prop = FindFProperty<FProperty>(UAeyerjiAttributeSet::StaticClass(), AttributeName))
	{
		return FGameplayAttribute(Prop);
	}

	return FGameplayAttribute();
}

bool IsPercentPointImportAttribute(const FGameplayAttribute& Attribute)
{
	return Attribute == UAeyerjiAttributeSet::GetCritChanceAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetAttackDamageVarianceAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetDodgeChanceAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetCooldownReductionAttribute();
}

bool IsPlayerDeferredImportAttribute(const FGameplayAttribute& Attribute)
{
	return Attribute == UAeyerjiAttributeSet::GetVisionRangeAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetProjectilePredictionAmountAttribute();
}

float NormalizeImportedModifierMagnitude(const FItemStatModifier& Modifier)
{
	return IsPercentPointImportAttribute(Modifier.Attribute)
		? Modifier.Magnitude / 100.f
		: Modifier.Magnitude;
}

void ApplyBaseModifiers(const FJsonObject& Object, UItemDefinition& Definition, FItemImportReport& Report, const FString& ItemName)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(TEXT("BaseModifiers"), Entries))
	{
		return;
	}

	Definition.BaseModifiers.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedModifiers);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FString AttributeText;
		if (!TryGetString(*EntryObject, TEXT("Attribute"), AttributeText))
		{
			Report.Warn(FString::Printf(TEXT("%s: BaseModifiers entry missing Attribute."), *ItemName));
			continue;
		}

		FItemStatModifier Modifier;
		Modifier.Attribute = ResolveJsonAttribute(AttributeText);
		if (!Modifier.Attribute.IsValid())
		{
			Report.Warn(FString::Printf(TEXT("%s: BaseModifiers has unknown Attribute '%s'."), *ItemName, *AttributeText));
			continue;
		}

		TryApplyEnumField(*EntryObject, TEXT("Op"), Modifier.Op, Report, ItemName);
		TryApplyFiniteNumberField(*EntryObject, TEXT("Magnitude"), Modifier.Magnitude, -1000000000.f, 1000000000.f);

		if (Modifier.Op != EItemModOp::Additive)
		{
			Report.Warn(FString::Printf(
				TEXT("%s: BaseModifiers '%s' uses non-additive Op=%d; player equipment runtime currently ignores non-additive stat modifiers."),
				*ItemName,
				*AttributeText,
				static_cast<int32>(Modifier.Op)));
		}

		if (IsPlayerDeferredImportAttribute(Modifier.Attribute))
		{
			Report.Warn(FString::Printf(
				TEXT("%s: BaseModifiers '%s' is enemy-only/player-deferred and will be ignored by player equipment."),
				*ItemName,
				*AttributeText));
		}

		if (IsPercentPointImportAttribute(Modifier.Attribute))
		{
			const float RawMagnitude = Modifier.Magnitude;
			Report.Warn(FString::Printf(
				TEXT("%s: BaseModifiers '%s' is authored as percent points; runtime normalizes %.3f -> %.3f."),
				*ItemName,
				*AttributeText,
				RawMagnitude,
				NormalizeImportedModifierMagnitude(Modifier)));
		}

		Definition.BaseModifiers.Add(Modifier);
	}
}

/** Replaces an affix asset list when the JSON field is present. */
void ApplyAffixListField(
	const FJsonObject& Object,
	const TCHAR* FieldName,
	TArray<TObjectPtr<UItemAffixDefinition>>& OutAffixes,
	FItemImportReport& Report,
	const FString& ItemName)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(FieldName, Entries))
	{
		return;
	}

	OutAffixes.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedAffixes);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::String)
		{
			continue;
		}

		const FString Path = NormalizeObjectPath(EntryValue->AsString());
		UItemAffixDefinition* Affix = Path.IsEmpty() || Path.Len() > 1024
			? nullptr
			: LoadObject<UItemAffixDefinition>(nullptr, *Path);
		if (Affix)
		{
			OutAffixes.Add(Affix);
		}
		else if (!Path.IsEmpty())
		{
			Report.Warn(FString::Printf(TEXT("%s: failed to load affix '%s' for %s."), *ItemName, *Path, FieldName));
		}
	}
}

/** Replaces granted gameplay effects when the JSON field is present. */
void ApplyGrantedEffects(const FJsonObject& Object, UItemDefinition& Definition, FItemImportReport& Report, const FString& ItemName)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(TEXT("GrantedEffects"), Entries))
	{
		return;
	}

	Definition.GrantedEffects.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedGrants);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FItemGrantedEffect Effect;
		ApplyClassField(*EntryObject, TEXT("EffectClass"), Effect.EffectClass, Report, ItemName);
		TryApplyFiniteNumberField(*EntryObject, TEXT("EffectLevel"), Effect.EffectLevel, 0.01f, 1000.f);
		ApplyGameplayTags(*EntryObject, TEXT("ApplicationTags"), Effect.ApplicationTags, Report, ItemName);

		const TArray<TSharedPtr<FJsonValue>>* MagnitudeEntries = nullptr;
		if (EntryObject->TryGetArrayField(TEXT("SetByCallerMagnitudes"), MagnitudeEntries))
		{
			const int32 MagnitudeCount = FMath::Min(
				MagnitudeEntries->Num(), MaxImportedSetByCallerMagnitudes);
			for (int32 MagnitudeIndex = 0; MagnitudeIndex < MagnitudeCount; ++MagnitudeIndex)
			{
				const TSharedPtr<FJsonValue>& MagnitudeValue = (*MagnitudeEntries)[MagnitudeIndex];
				if (!MagnitudeValue.IsValid() || MagnitudeValue->Type != EJson::Object)
				{
					continue;
				}

				const TSharedPtr<FJsonObject> MagnitudeObject = MagnitudeValue->AsObject();
				FItemSetByCallerMagnitude Magnitude;
				FGameplayTagContainer SingleTag;
				ApplyGameplayTags(*MagnitudeObject, TEXT("DataTag"), SingleTag, Report, ItemName);
				if (SingleTag.Num() > 0)
				{
					Magnitude.DataTag = SingleTag.First();
				}
				TryApplyFiniteNumberField(*MagnitudeObject, TEXT("LevelOneMagnitude"), Magnitude.LevelOneMagnitude, -1000000000.f, 1000000000.f);
				TryApplyFiniteNumberField(*MagnitudeObject, TEXT("PerLevelMultiplier"), Magnitude.PerLevelMultiplier, -1000000.f, 1000000.f);
				TryApplyFiniteNumberField(*MagnitudeObject, TEXT("PerLevelAdd"), Magnitude.PerLevelAdd, -1000000000.f, 1000000000.f);
				Effect.SetByCallerMagnitudes.Add(Magnitude);
			}
		}

		Definition.GrantedEffects.Add(Effect);
	}
}

/** Replaces granted gameplay abilities when the JSON field is present. */
void ApplyGrantedAbilities(const FJsonObject& Object, UItemDefinition& Definition, FItemImportReport& Report, const FString& ItemName)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(TEXT("GrantedAbilities"), Entries))
	{
		return;
	}

	Definition.GrantedAbilities.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedGrants);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FItemGrantedAbility Ability;
		ApplyClassField(*EntryObject, TEXT("AbilityClass"), Ability.AbilityClass, Report, ItemName);
		TryApplyBoundedIntField(*EntryObject, TEXT("AbilityLevel"), Ability.AbilityLevel, 1, 1000);
		TryApplyBoundedIntField(*EntryObject, TEXT("InputID"), Ability.InputID, -1, 255);
		ApplyGameplayTags(*EntryObject, TEXT("OwnedTags"), Ability.OwnedTags, Report, ItemName);
		Definition.GrantedAbilities.Add(Ability);
	}
}

/** Replaces equip synergy colors when the JSON field is present. */
void ApplyEquipSynergyColors(const FJsonObject& Object, UItemDefinition& Definition)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Object.TryGetArrayField(TEXT("EquipSynergyColors"), Entries))
	{
		return;
	}

	Definition.EquipSynergyColors.Reset();
	const int32 EntryCount = FMath::Min(Entries->Num(), MaxImportedSynergyColors);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const TSharedPtr<FJsonValue>& EntryValue = (*Entries)[EntryIndex];
		if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
		{
			continue;
		}

		const TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
		FItemEquipSynergyColor Entry;
		TryApplyBoundedIntField(*EntryObject, TEXT("StackCount"), Entry.StackCount, 1, 1000000);
		if (const TSharedPtr<FJsonValue>* ColorValue = FindJsonField(*EntryObject, TEXT("Color")))
		{
			TryReadLinearColor(*ColorValue, Entry.Color);
		}
		Definition.EquipSynergyColors.Add(Entry);
	}
}

/** Applies one JSON item object onto a UItemDefinition asset. */
void ApplyItemObjectToDefinition(const FJsonObject& Object, UItemDefinition& Definition, FItemImportReport& Report, const FString& ItemName)
{
	FString Text;
	if (TryGetString(Object, TEXT("DisplayName"), Text))
	{
		Definition.DisplayName = FText::FromString(Text);
	}
	if (TryGetString(Object, TEXT("Description"), Text))
	{
		Definition.Description = FText::FromString(Text);
	}

	const bool bChangedCategory = TryApplyEnumField(Object, TEXT("ItemCategory"), Definition.ItemCategory, Report, ItemName);
	const bool bChangedSlot = TryApplyEnumField(Object, TEXT("DefaultSlot"), Definition.DefaultSlot, Report, ItemName);
	if (bChangedCategory && !bChangedSlot)
	{
		Definition.DefaultSlot = static_cast<EEquipmentSlot>(Definition.ItemCategory);
	}

	double RequiredLevel = 0.0;
	if (Object.TryGetNumberField(TEXT("RequiredLevel"), RequiredLevel))
	{
		Definition.RequiredLevel = UAeyerjiDifficultySettings::FloatToGameplayLevel(
			FMath::IsFinite(RequiredLevel) ? static_cast<float>(RequiredLevel) : 1.f);
	}
	if (TryGetString(Object, TEXT("CorruptionPowerText"), Text))
	{
		Definition.CorruptionPowerText = FText::FromString(Text);
	}
	if (TryGetString(Object, TEXT("CorruptionDrawbackText"), Text))
	{
		Definition.CorruptionDrawbackText = FText::FromString(Text);
	}
	if (Definition.IsCorruptionItem())
	{
		Definition.RequiredLevel = UAeyerjiDifficultySettings::GetMaxGameplayLevel();
		Definition.DefaultSlot = EEquipmentSlot::Corruption;
	}

	ApplyGameplayTags(Object, TEXT("ItemTags"), Definition.ItemTags, Report, ItemName);
	ApplyRarityAffixRanges(Object, Definition, Report, ItemName);
	ApplyBaseModifiers(Object, Definition, Report, ItemName);
	ApplyAffixListField(Object, TEXT("GuaranteedAffixes"), Definition.GuaranteedAffixes, Report, ItemName);
	ApplyAffixListField(Object, TEXT("OptionalAffixPool"), Definition.OptionalAffixPool, Report, ItemName);
	ApplyAffixListField(Object, TEXT("AffixPool"), Definition.OptionalAffixPool, Report, ItemName);
	ApplyGrantedEffects(Object, Definition, Report, ItemName);
	ApplyGrantedAbilities(Object, Definition, Report, ItemName);

	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("InventorySize")))
	{
		TryReadIntPoint(*Value, Definition.InventorySize);
	}

	ApplyObjectField(Object, TEXT("Icon"), Definition.Icon, Report, ItemName);
	ApplyObjectField(Object, TEXT("WorldMesh"), Definition.WorldMesh, Report, ItemName);
	ApplyObjectField(Object, TEXT("WorldSkeletalMesh"), Definition.WorldSkeletalMesh, Report, ItemName);

	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("WorldMeshOffset")))
	{
		TryReadVector(*Value, Definition.WorldMeshOffset);
	}
	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("WorldMeshRotation")))
	{
		TryReadRotator(*Value, Definition.WorldMeshRotation);
	}
	if (const TSharedPtr<FJsonValue>* Value = FindJsonField(Object, TEXT("WorldMeshScale")))
	{
		TryReadVector(*Value, Definition.WorldMeshScale);
	}

	const TSharedPtr<FJsonObject>* PickupVisualsObject = nullptr;
	if (Object.TryGetObjectField(TEXT("PickupVisuals"), PickupVisualsObject))
	{
		ApplyPickupVisuals(**PickupVisualsObject, Definition.PickupVisuals, Report, ItemName);
	}

	Object.TryGetBoolField(TEXT("bEnableEquipSynergy"), Definition.bEnableEquipSynergy);
	if (TryGetString(Object, TEXT("EquipSynergyColorParameter"), Text))
	{
		Definition.EquipSynergyColorParameter = FName(*Text.Left(256));
	}
	ApplyEquipSynergyColors(Object, Definition);
}

/** Makes a conservative UObject name from JSON input without changing valid names. */
FString SanitizeAssetName(FString AssetName)
{
	AssetName.TrimStartAndEndInline();
	AssetName.LeftInline(128);
	for (TCHAR& Character : AssetName)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			Character = TEXT('_');
		}
	}
	return AssetName;
}

/** Creates or loads the target package and definition asset. */
UItemDefinition* LoadOrCreateDefinition(const FString& PackagePath, const FString& AssetName, bool& bOutCreated, FItemImportReport& Report)
{
	bOutCreated = false;
	const FString PackageName = PackagePath + TEXT("/") + AssetName;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		Report.Error(FString::Printf(TEXT("Invalid package name '%s'."), *PackageName));
		return nullptr;
	}

	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	if (UItemDefinition* Existing = LoadObject<UItemDefinition>(nullptr, *ObjectPath))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Report.Error(FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return nullptr;
	}
	if (UObject* ExistingObject = StaticFindObjectFast(UObject::StaticClass(), Package, FName(*AssetName)))
	{
		Report.Error(FString::Printf(
			TEXT("Cannot create item definition '%s': incompatible object '%s' already exists."),
			*PackageName,
			*GetNameSafe(ExistingObject)));
		return nullptr;
	}

	UItemDefinition* Definition = NewObject<UItemDefinition>(
		Package,
		UItemDefinition::StaticClass(),
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional);

	if (!Definition)
	{
		Report.Error(FString::Printf(TEXT("Failed to create item definition '%s'."), *ObjectPath));
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Definition);
	bOutCreated = true;
	return Definition;
}

/** Saves the package that owns a generated item definition. */
bool SaveDefinitionPackage(UItemDefinition& Definition, FItemImportReport& Report)
{
	UPackage* Package = Definition.GetOutermost();
	if (!Package)
	{
		Report.Warn(FString::Printf(TEXT("%s: missing outer package; asset was not saved."), *Definition.GetName()));
		return false;
	}

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	const bool bSaved = UPackage::SavePackage(Package, &Definition, *PackageFilename, SaveArgs);
	if (!bSaved)
	{
		Report.Error(FString::Printf(TEXT("%s: failed to save package '%s'."), *Definition.GetName(), *PackageFilename));
	}
	return bSaved;
}

/** Imports a parsed item object into its target asset package. */
bool ImportOneItem(const FJsonObject& ItemObject, const FString& DefaultPackagePath, bool bSaveAssets, bool bDryRun, FItemImportReport& Report)
{
	FString AssetName;
	if (!TryGetString(ItemObject, TEXT("AssetName"), AssetName)
		&& !TryGetString(ItemObject, TEXT("Name"), AssetName)
		&& !TryGetString(ItemObject, TEXT("Id"), AssetName))
	{
		Report.Error(TEXT("Item entry is missing AssetName."));
		return false;
	}

	AssetName = SanitizeAssetName(AssetName);
	if (AssetName.IsEmpty())
	{
		Report.Error(TEXT("Item entry has an empty AssetName after sanitizing."));
		return false;
	}

	FString PackagePath = DefaultPackagePath;
	TryGetString(ItemObject, TEXT("PackagePath"), PackagePath);
	PackagePath.TrimStartAndEndInline();
	PackagePath.RemoveFromEnd(TEXT("/"));

	if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath))
	{
		Report.Error(FString::Printf(TEXT("%s: invalid PackagePath '%s'."), *AssetName, *PackagePath));
		return false;
	}

	if (bDryRun)
	{
		UE_LOG(LogItemDefinitionJsonImport, Display, TEXT("Dry run: would import %s/%s."), *PackagePath, *AssetName);
		return true;
	}

	bool bCreated = false;
	UItemDefinition* Definition = LoadOrCreateDefinition(PackagePath, AssetName, bCreated, Report);
	if (!Definition)
	{
		return false;
	}

	Definition->Modify();
	ApplyItemObjectToDefinition(ItemObject, *Definition, Report, AssetName);
	Definition->MarkPackageDirty();
	Definition->PostEditChange();

	if (bSaveAssets)
	{
		if (!SaveDefinitionPackage(*Definition, Report))
		{
			return false;
		}
	}

	if (bCreated)
	{
		++Report.Created;
		UE_LOG(LogItemDefinitionJsonImport, Display, TEXT("Created item definition %s/%s."), *PackagePath, *AssetName);
	}
	else
	{
		++Report.Updated;
		UE_LOG(LogItemDefinitionJsonImport, Display, TEXT("Updated item definition %s/%s."), *PackagePath, *AssetName);
	}

	return true;
}

/** Extracts item objects from either a root array, a root object with Items, or a single item object. */
bool CollectItemObjects(const TSharedPtr<FJsonValue>& RootValue, TArray<TSharedPtr<FJsonObject>>& OutItems, FString& InOutDefaultPackagePath)
{
	if (!RootValue.IsValid())
	{
		return false;
	}

	if (RootValue->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& RootEntries = RootValue->AsArray();
		const int32 RootEntryCount = FMath::Min(RootEntries.Num(), MaxImportedItems);
		for (int32 EntryIndex = 0; EntryIndex < RootEntryCount; ++EntryIndex)
		{
			const TSharedPtr<FJsonValue>& Entry = RootEntries[EntryIndex];
			if (Entry.IsValid() && Entry->Type == EJson::Object)
			{
				OutItems.Add(Entry->AsObject());
			}
		}
		return OutItems.Num() > 0;
	}

	if (RootValue->Type != EJson::Object)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> RootObject = RootValue->AsObject();
	TryGetString(*RootObject, TEXT("DestinationPath"), InOutDefaultPackagePath);

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (RootObject->TryGetArrayField(TEXT("Items"), Items))
	{
		const int32 ItemCount = FMath::Min(Items->Num(), MaxImportedItems);
		for (int32 ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
		{
			const TSharedPtr<FJsonValue>& Entry = (*Items)[ItemIndex];
			if (Entry.IsValid() && Entry->Type == EJson::Object)
			{
				OutItems.Add(Entry->AsObject());
			}
		}
		return OutItems.Num() > 0;
	}

	if (RootObject->HasField(TEXT("AssetName")) || RootObject->HasField(TEXT("Name")) || RootObject->HasField(TEXT("Id")))
	{
		OutItems.Add(RootObject);
		return true;
	}

	return false;
}
}
#endif

int32 UItemDefinitionJsonImportCommandlet::Main(const FString& Params)
{
#if !WITH_EDITOR
	UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("ItemDefinitionJsonImport is editor-only."));
	return 1;
#else
	FString JsonPath;
	if (!FParse::Value(*Params, TEXT("Json="), JsonPath))
	{
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("Missing -Json=<path>."));
		return 1;
	}

	FString DestinationPath = DefaultItemDefinitionDestination;
	FParse::Value(*Params, TEXT("Dest="), DestinationPath);
	DestinationPath.TrimStartAndEndInline();
	DestinationPath.RemoveFromEnd(TEXT("/"));

	const bool bSaveAssets = !FParse::Param(*Params, TEXT("NoSave"));
	const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));
	const int64 JsonFileSize = IFileManager::Get().FileSize(*JsonPath);
	if (JsonFileSize < 0 || JsonFileSize > MaxImportJsonBytes)
	{
		UE_LOG(
			LogItemDefinitionJsonImport,
			Error,
			TEXT("JSON file '%s' is missing or exceeds the %lld-byte import limit."),
			*JsonPath,
			MaxImportJsonBytes);
		return 1;
	}
	if (!DestinationPath.StartsWith(TEXT("/Game/"))
		|| !FPackageName::IsValidLongPackageName(DestinationPath))
	{
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("Invalid project content destination '%s'."), *DestinationPath);
		return 1;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("Failed to read JSON file '%s'."), *JsonPath);
		return 1;
	}

	TSharedPtr<FJsonValue> RootValue;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("Failed to parse JSON file '%s'."), *JsonPath);
		return 1;
	}

	TArray<TSharedPtr<FJsonObject>> Items;
	if (!CollectItemObjects(RootValue, Items, DestinationPath))
	{
		UE_LOG(LogItemDefinitionJsonImport, Error, TEXT("JSON file '%s' does not contain item objects."), *JsonPath);
		return 1;
	}

	FItemImportReport Report;
	for (const TSharedPtr<FJsonObject>& ItemObject : Items)
	{
		if (ItemObject.IsValid())
		{
			ImportOneItem(*ItemObject, DestinationPath, bSaveAssets, bDryRun, Report);
		}
	}

	UE_LOG(
		LogItemDefinitionJsonImport,
		Display,
		TEXT("Item definition import complete. Created=%d Updated=%d Failed=%d Warnings=%d"),
		Report.Created,
		Report.Updated,
		Report.Failed,
		Report.Warnings);

	return Report.Failed > 0 ? 1 : 0;
#endif
}
