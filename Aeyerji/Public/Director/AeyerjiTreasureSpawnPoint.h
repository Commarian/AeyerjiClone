#pragma once

#include "CoreMinimal.h"
#include "Director/AeyerjiTreasureTypes.h"
#include "GameFramework/Actor.h"

#include "AeyerjiTreasureSpawnPoint.generated.h"

class AAeyerjiRewardPresentationActor;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EAeyerjiTreasureSpawnPointValidationState : uint8
{
	Unvalidated UMETA(DisplayName="Unvalidated"),
	Valid UMETA(DisplayName="Valid"),
	Disabled UMETA(DisplayName="Disabled"),
	OtherRift UMETA(DisplayName="Other Rift"),
	MissingChestClass UMETA(DisplayName="Missing Chest Class"),
	MissingLootProfile UMETA(DisplayName="Missing or Invalid Loot Profile Row"),
	OutsideNavigation UMETA(DisplayName="Outside Navigation"),
	NavigationAnchorTooFar UMETA(DisplayName="Navigation Anchor Too Far"),
	StartExcluded UMETA(DisplayName="Start Excluded"),
	Unreachable UMETA(DisplayName="Unreachable")
};

/**
 * Lightweight designer-authored candidate location for a Rift treasure chest.
 * It never spawns or owns loot itself; LevelDirector validates and selects it on the authority.
 */
UCLASS(Blueprintable, Placeable)
class AEYERJI_API AAeyerjiTreasureSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiTreasureSpawnPoint();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Returns whether this authored location may participate in Rift selection. */
	UFUNCTION(BlueprintPure, Category="Rift|Treasure")
	bool IsEnabled() const { return bEnabled; }

	/** Returns the non-negative authored selection weight for this point. */
	UFUNCTION(BlueprintPure, Category="Rift|Treasure")
	float GetSpawnWeight() const { return SpawnWeight; }

	/** Optional spatial grouping used by the selector's zone-repeat weighting. */
	UFUNCTION(BlueprintPure, Category="Rift|Treasure")
	FName GetZoneId() const { return ZoneId; }

	/** Optional map/Rift scope. An empty value makes this point eligible for every loaded Rift. */
	UFUNCTION(BlueprintPure, Category="Rift|Treasure")
	FName GetRiftZoneId() const { return RiftZoneId; }

	/** Returns whether this point is eligible for the supplied active Rift zone. */
	bool IsEligibleForRiftZone(FName ActiveRiftZoneId) const;

	/** Returns whether this point may bypass the standard Rift-start exclusion distance. */
	bool AllowsNearRiftStart() const { return bAllowNearRiftStart; }

	/** Resolves this point's class override or the Rift default chest class. */
	TSubclassOf<AAeyerjiRewardPresentationActor> ResolveChestClass(
		TSubclassOf<AAeyerjiRewardPresentationActor> DefaultChestClass) const;

	/** Resolves this point's DataTable-row override or the Rift default loot-profile row. */
	FDataTableRowHandle ResolveLootProfileRow(const FDataTableRowHandle& DefaultLootProfileRow) const;

	/** Projects a nearby navigable interaction anchor while keeping the visual chest transform exactly at this actor. */
	bool ResolveNavigationAnchor(const FVector& ProjectionExtent, FVector& OutNavigationAnchor) const;

	/** Records the latest validation result for editor inspection. Runtime selection does not rely on this cached state. */
	void RecordValidationState(
		EAeyerjiTreasureSpawnPointValidationState InState,
		const FVector& InNavigationAnchor = FVector::ZeroVector,
		const FString& InMessage = FString());

	/** Tests only the local NavMesh projection. Use LevelDirector's validation action for Rift-start reachability checks. */
	UFUNCTION(CallInEditor, Category="Rift|Treasure|Validation")
	void ValidateNavigationAnchor();

protected:
	/** Master switch allowing a designer to retain a point in the map without making it eligible. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure")
	bool bEnabled = true;

	/** Relative probability used after NavMesh, reachability, distance, and zone validation succeed. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure", meta=(ClampMin="0.0"))
	float SpawnWeight = 1.f;

	/** Optional spatial grouping such as North, SideRoomA, or BossApproach used to reduce repeated-area selection. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure")
	FName ZoneId = NAME_None;

	/** Optional active ZoneRunDefinition zone scope for maps where multiple Rift point sets may be loaded together. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure")
	FName RiftZoneId = NAME_None;

	/** Allows this one point to be selected inside the Rift-start exclusion distance. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure")
	bool bAllowNearRiftStart = false;

	/** Optional typed DataTable row for a special alcove or side room. Leave both table and row empty to use the Rift default; a partial handle is reported as invalid. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure|Overrides", meta=(RowType="/Script/Aeyerji.AeyerjiTreasureLootProfileRow"))
	FDataTableRowHandle LootProfileRowOverride;

	/** Optional chest presentation class for this point. Leave empty to use the Rift default chest class. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure|Overrides")
	TSubclassOf<AAeyerjiRewardPresentationActor> ChestClassOverride;

	/** Editor-only visual marker. A Blueprint child can assign its preferred chest mesh or material here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Preview")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	/** Shows the preview mesh in editor viewports only. It is always HiddenInGame and has no collision or navigation effect. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Rift|Treasure|Preview")
	bool bShowPreview = true;

	/** Latest validation state, refreshed by the local action or LevelDirector's full validation action. */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category="Rift|Treasure|Validation")
	EAeyerjiTreasureSpawnPointValidationState LastValidationState = EAeyerjiTreasureSpawnPointValidationState::Unvalidated;

	/** Latest nearby NavMesh location reported by validation. This is the anchor passed to a selected chest, not a movement of the chest. */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category="Rift|Treasure|Validation")
	FVector LastNavigationAnchor = FVector::ZeroVector;

	/** Short diagnostic describing the latest validation outcome. */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category="Rift|Treasure|Validation")
	FString LastValidationMessage;

private:
	void RefreshPreviewVisibility();
};
