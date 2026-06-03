#include "Systems/AeyerjiWorldStateTypes.h"

FString FAeyerjiWorldStateKey::ToString() const
{
	FString KeyString = StateTag.ToString();
	if (!InstanceId.IsNone())
	{
		KeyString += FString::Printf(TEXT(":%s"), *InstanceId.ToString());
	}

	if (!OwnerId.IsNone())
	{
		KeyString += FString::Printf(TEXT("@%s"), *OwnerId.ToString());
	}

	return KeyString;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromBool(const bool bValue)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::Bool;
	Value.BoolValue = bValue;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromInt(const int32 ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::Int;
	Value.IntValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromFloat(const float ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::Float;
	Value.FloatValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromName(const FName ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::Name;
	Value.NameValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromString(const FString& ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::String;
	Value.StringValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromGameplayTag(const FGameplayTag& ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::GameplayTag;
	Value.TagValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromSoftObjectPath(const FSoftObjectPath& ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::SoftObjectPath;
	Value.SoftObjectPathValue = ValueIn;
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::FromObject(UObject* ValueIn)
{
	FAeyerjiWorldStateValue Value;
	Value.Type = EAeyerjiWorldStateValueType::Object;
	Value.ObjectValue = ValueIn;
	if (ValueIn)
	{
		Value.SoftObjectPathValue = FSoftObjectPath(ValueIn);
	}
	return Value;
}

FAeyerjiWorldStateValue FAeyerjiWorldStateValue::MakeDataOnlyCopy() const
{
	FAeyerjiWorldStateValue Copy = *this;
	Copy.ObjectValue.Reset();
	return Copy;
}

bool FAeyerjiWorldStateValue::Equals(const FAeyerjiWorldStateValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}

	switch (Type)
	{
	case EAeyerjiWorldStateValueType::None:
		return true;
	case EAeyerjiWorldStateValueType::Bool:
		return BoolValue == Other.BoolValue;
	case EAeyerjiWorldStateValueType::Int:
		return IntValue == Other.IntValue;
	case EAeyerjiWorldStateValueType::Float:
		return FMath::IsNearlyEqual(FloatValue, Other.FloatValue);
	case EAeyerjiWorldStateValueType::Name:
		return NameValue == Other.NameValue;
	case EAeyerjiWorldStateValueType::String:
		return StringValue == Other.StringValue;
	case EAeyerjiWorldStateValueType::GameplayTag:
		return TagValue == Other.TagValue;
	case EAeyerjiWorldStateValueType::SoftObjectPath:
		return SoftObjectPathValue == Other.SoftObjectPathValue;
	case EAeyerjiWorldStateValueType::Object:
		return ObjectValue == Other.ObjectValue && SoftObjectPathValue == Other.SoftObjectPathValue;
	default:
		return false;
	}
}

bool FAeyerjiWorldStateValue::TryGetNumericValue(double& OutValue) const
{
	switch (Type)
	{
	case EAeyerjiWorldStateValueType::Bool:
		OutValue = BoolValue ? 1.0 : 0.0;
		return true;
	case EAeyerjiWorldStateValueType::Int:
		OutValue = static_cast<double>(IntValue);
		return true;
	case EAeyerjiWorldStateValueType::Float:
		OutValue = static_cast<double>(FloatValue);
		return true;
	default:
		OutValue = 0.0;
		return false;
	}
}

FString FAeyerjiWorldStateValue::ToString() const
{
	switch (Type)
	{
	case EAeyerjiWorldStateValueType::None:
		return TEXT("None");
	case EAeyerjiWorldStateValueType::Bool:
		return BoolValue ? TEXT("true") : TEXT("false");
	case EAeyerjiWorldStateValueType::Int:
		return FString::FromInt(IntValue);
	case EAeyerjiWorldStateValueType::Float:
		return FString::SanitizeFloat(FloatValue);
	case EAeyerjiWorldStateValueType::Name:
		return NameValue.ToString();
	case EAeyerjiWorldStateValueType::String:
		return StringValue;
	case EAeyerjiWorldStateValueType::GameplayTag:
		return TagValue.ToString();
	case EAeyerjiWorldStateValueType::SoftObjectPath:
		return SoftObjectPathValue.ToString();
	case EAeyerjiWorldStateValueType::Object:
		return ObjectValue.IsValid()
			? GetNameSafe(ObjectValue.Get())
			: SoftObjectPathValue.ToString();
	default:
		return TEXT("Unknown");
	}
}

FAeyerjiWorldStateEntry FAeyerjiWorldStateEntry::MakeDataOnlyCopy() const
{
	FAeyerjiWorldStateEntry Copy = *this;
	Copy.Value = Value.MakeDataOnlyCopy();
	return Copy;
}
