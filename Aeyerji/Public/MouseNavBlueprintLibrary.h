#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MouseNavBlueprintLibrary.generated.h"

UENUM(BlueprintType)
enum class EMouseNavResult : uint8
{
    None         UMETA(DisplayName = "None"),
    NavLocation  UMETA(DisplayName = "Location"),
    ClickedPawn  UMETA(DisplayName = "Pawn")
};

/**
 * Projects the mouse cursor onto the NavMesh or detects a pawn click.
 * - NavLocation is filled only when the result is NavLocation
 * - OutCursorLocation stores the raw cursor impact point for teleport/placement logic
 * - OutPawn is filled only when the result is ClickedPawn
 * - Non-local controllers return None without performing cursor traces
 */
UCLASS()
class AEYERJI_API UMouseNavBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    /**
     * Gets the world-space location under the mouse cursor, projected onto the NavMesh.
     * @param PlayerController   Controller whose mouse we query (auto-resolved if not supplied). Must be local for cursor queries.
     * @param OutNavLocation     Navmesh-projected point when the result is NavLocation
     * @param OutCursorLocation  Raw surface impact point under the cursor for visual placement
     */
    UFUNCTION(BlueprintCallable, Category = "Navigation",
              meta = (WorldContext="WorldContextObject",
                      DisplayName  ="Get Mouse Nav Context",
                      ExpandEnumAsExecs="ReturnValue",
                      AdvancedDisplay="PlayerController"))
    static EMouseNavResult GetMouseNavContext(
            const UObject*      WorldContextObject,
            APlayerController*  PlayerController,
            FVector&            OutNavLocation,
            FVector&            OutCursorLocation,
            APawn*&             OutPawn);

    /**
     * Resolves a nav-projected point to a grounded teleport location by sweeping the pawn's collision downward.
     * @param Pawn                Optional pawn whose capsule/bounds define the clearance we need
     * @param OutTeleportLocation Floor-aligned position safe for teleport
     * @param TraceHeight         How far above the nav point the downward sweep begins
     * @param TraceDepth          How far below the nav point we search for ground
     * @param AdditionalOffset    Extra upward offset applied after landing to avoid clipping
     */
    UFUNCTION(BlueprintCallable, Category = "Navigation",
              meta = (WorldContext="WorldContextObject",
                      AdvancedDisplay="TraceHeight,TraceDepth,AdditionalOffset"))
    static bool ResolveGroundedTeleportLocation(
            const UObject* WorldContextObject,
            const FVector& NavLocation,
            const APawn*   Pawn,
            FVector&       OutTeleportLocation,
            float          TraceHeight = 200.f,
            float          TraceDepth = 300.f,
            float          AdditionalOffset = 2.f);

    /**
     * Clamps a desired teleport target to the nearest navigable point within MaxRange and resolves a safe grounded location.
     * @param OriginLocation      Starting point (e.g., caster location) used to measure range
     * @param DesiredLocation     Raw desired target (e.g., mouse hit result)
     * @param MaxRange            Maximum allowed distance from OriginLocation
     * @param bStraightLine       When true, samples along the straight line toward DesiredLocation; otherwise follows the nav path length
     * @param OutNavLocation      Navmesh-projected point within range (XY clamped)
     * @param OutTeleportLocation Grounded teleport point after height adjustment
     * @param NavProjectionExtent Extent used when projecting onto the NavMesh
     */
    UFUNCTION(BlueprintCallable, Category = "Navigation",
              meta = (WorldContext="WorldContextObject",
                      DisplayName  ="Clamp Teleport To Navigable Range",
                      AdvancedDisplay="PlayerController,NavProjectionExtent,TraceHeight,TraceDepth,AdditionalOffset"))
    static bool GetClosestNavigableLocationInRange(
            const UObject*     WorldContextObject,
            APlayerController* PlayerController,
            const FVector&     OriginLocation,
            const FVector&     DesiredLocation,
            float              MaxRange,
            bool               bStraightLine,
            FVector&           OutNavLocation,
            FVector&           OutTeleportLocation,
            FVector            NavProjectionExtent = FVector(50.f, 50.f, 200.f),
            float              TraceHeight = 200.f,
            float              TraceDepth = 300.f,
            float              AdditionalOffset = 2.f);
};
