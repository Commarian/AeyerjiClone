#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AeyerjiWorldStateTypes.generated.h"

/**
 * Controls whether a world-state entry is written to the shared world save.
 */
UENUM(BlueprintType)
enum class EAeyerjiWorldStatePersistence : uint8
{
	RuntimeOnly UMETA(DisplayName="Runtime Only"),
	Persistent UMETA(DisplayName="Persistent")
};

/**
 * Controls whether a world-state entry remains server-only or is mirrored to clients.
 */
UENUM(BlueprintType)
enum class EAeyerjiWorldStateReplication : uint8
{
	ServerOnly UMETA(DisplayName="Server Only"),
	PublicReplicated UMETA(DisplayName="Public Replicated")
};

/**
 * Defines the lifetime/ownership lane for a world-state entry.
 */
UENUM(BlueprintType)
enum class EAeyerjiWorldStateScope : uint8
{
	/** Shared world/server facts saved in the shared world-state artifact when persistent. */
	Global UMETA(DisplayName="Global"),

	/** Facts that live only for the active run and are cleared by BeginRun/EndRun. */
	Run UMETA(DisplayName="Run"),

	/** Facts owned by a specific character/profile save, using FAeyerjiWorldStateKey::OwnerId. */
	Character UMETA(DisplayName="Character"),

	/** Facts that live for the current process/session but are not tied to one run. */
	Session UMETA(DisplayName="Session")
};

/**
 * Storage type used by a world-state value.
 */
UENUM(BlueprintType)
enum class EAeyerjiWorldStateValueType : uint8
{
	None UMETA(DisplayName="None"),
	Bool UMETA(DisplayName="Bool"),
	Int UMETA(DisplayName="Integer"),
	Float UMETA(DisplayName="Float"),
	Name UMETA(DisplayName="Name"),
	String UMETA(DisplayName="String"),
	GameplayTag UMETA(DisplayName="Gameplay Tag"),
	SoftObjectPath UMETA(DisplayName="Soft Object Path"),
	Object UMETA(DisplayName="Live Object")
};

/**
 * Comparison operation used by world-state StateTree conditions.
 */
UENUM(BlueprintType)
enum class EAeyerjiWorldStateCompareOp : uint8
{
	Exists UMETA(DisplayName="Exists"),
	DoesNotExist UMETA(DisplayName="Does Not Exist"),
	Equals UMETA(DisplayName="Equals"),
	NotEquals UMETA(DisplayName="Not Equals"),
	Greater UMETA(DisplayName="Greater"),
	GreaterOrEqual UMETA(DisplayName="Greater Or Equal"),
	Less UMETA(DisplayName="Less"),
	LessOrEqual UMETA(DisplayName="Less Or Equal")
};

/**
 * Designer-facing key for facts and registered world objects.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiWorldStateKey
{
	GENERATED_BODY()

	/** Hierarchical gameplay tag that names the fact or object class. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FGameplayTag StateTag;

	/** Optional unique instance id for repeatable placed objects or legacy names. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FName InstanceId = NAME_None;

	/** Optional owner id for character/profile-scoped state. Leave empty for global and run facts. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FName OwnerId = NAME_None;

	FAeyerjiWorldStateKey() = default;
	FAeyerjiWorldStateKey(const FGameplayTag& InStateTag, const FName InInstanceId = NAME_None, const FName InOwnerId = NAME_None)
		: StateTag(InStateTag)
		, InstanceId(InInstanceId)
		, OwnerId(InOwnerId)
	{
	}

	/** Returns true when the key can address a world-state entry. */
	bool IsValid() const { return StateTag.IsValid(); }

	/** Returns a stable human-readable representation for logs and debug UI. */
	FString ToString() const;

	bool operator==(const FAeyerjiWorldStateKey& Other) const
	{
		return StateTag == Other.StateTag && InstanceId == Other.InstanceId && OwnerId == Other.OwnerId;
	}

	bool operator!=(const FAeyerjiWorldStateKey& Other) const
	{
		return !(*this == Other);
	}
};

FORCEINLINE uint32 GetTypeHash(const FAeyerjiWorldStateKey& Key)
{
	const uint32 TagAndInstanceHash = HashCombine(GetTypeHash(Key.StateTag.GetTagName()), GetTypeHash(Key.InstanceId));
	return HashCombine(TagAndInstanceHash, GetTypeHash(Key.OwnerId));
}

/**
 * Typed value stored in the world-state registry.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiWorldStateValue
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStateValueType Type = EAeyerjiWorldStateValueType::None;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	bool BoolValue = false;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	int32 IntValue = 0;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	float FloatValue = 0.f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FName NameValue = NAME_None;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FString StringValue;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FGameplayTag TagValue;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FSoftObjectPath SoftObjectPathValue;

	/** Transient live object pointer. Persistent and replicated copies only keep SoftObjectPathValue. */
	UPROPERTY(Transient, NotReplicated)
	TWeakObjectPtr<UObject> ObjectValue;

	/** Creates a typed boolean value. */
	static FAeyerjiWorldStateValue FromBool(bool bValue);

	/** Creates a typed integer value. */
	static FAeyerjiWorldStateValue FromInt(int32 Value);

	/** Creates a typed float value. */
	static FAeyerjiWorldStateValue FromFloat(float Value);

	/** Creates a typed name value. */
	static FAeyerjiWorldStateValue FromName(FName Value);

	/** Creates a typed string value. */
	static FAeyerjiWorldStateValue FromString(const FString& Value);

	/** Creates a typed gameplay tag value. */
	static FAeyerjiWorldStateValue FromGameplayTag(const FGameplayTag& Value);

	/** Creates a typed soft object path value. */
	static FAeyerjiWorldStateValue FromSoftObjectPath(const FSoftObjectPath& Value);

	/** Creates a typed live-object value and records its soft path when possible. */
	static FAeyerjiWorldStateValue FromObject(UObject* Value);

	/** Returns a copy suitable for save/replication by dropping transient pointers. */
	FAeyerjiWorldStateValue MakeDataOnlyCopy() const;

	/** Compares two values using their active type. */
	bool Equals(const FAeyerjiWorldStateValue& Other) const;

	/** Returns true when both values can be compared numerically. */
	bool TryGetNumericValue(double& OutValue) const;

	/** Returns a stable debug string. */
	FString ToString() const;
};

/**
 * Complete row in the world-state registry.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiWorldStateEntry
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FAeyerjiWorldStateKey Key;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FAeyerjiWorldStateValue Value;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::RuntimeOnly;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	int32 Version = 0;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	FDateTime LastUpdatedUtc = FDateTime::MinValue();

	/** Returns a save/replication-safe copy without transient object pointers. */
	FAeyerjiWorldStateEntry MakeDataOnlyCopy() const;
};
