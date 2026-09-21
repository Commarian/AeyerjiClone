#include "GUI/AeyerjiMinimapMapSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GUI/AeyerjiMinimapSettings.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "RenderUtils.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAeyerjiMinimap, Log, All);

namespace AeyerjiMinimapMapPrivate
{
	struct FTriangle
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		FVector C = FVector::ZeroVector;
		float MinZ = 0.f;
		float MaxZ = 0.f;
	};

	struct FEdge
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
	};

	FVector2D WorldToPixel(const FVector& WorldPosition, const FBox2D& Bounds, const int32 Resolution)
	{
		const double Side = FMath::Max(Bounds.GetSize().X, 1.0);
		const double U = (WorldPosition.Y - Bounds.Min.Y) / Side;
		const double V = 1.0 - ((WorldPosition.X - Bounds.Min.X) / Side);
		return FVector2D(U * Resolution, V * Resolution);
	}

	bool IntersectsFloor(const float GeometryMinZ, const float GeometryMaxZ, const FAeyerjiMinimapFloorMap& Floor)
	{
		return GeometryMaxZ >= Floor.MinZ && GeometryMinZ <= Floor.MaxZ;
	}
}

void UAeyerjiMinimapMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAeyerjiStreamingSubsystem>();

	if (UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem())
	{
		Streaming->OnStreamingRequestStarted.RemoveDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleStreamingRequestStarted);
		Streaming->OnStreamingRequestStarted.AddDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleStreamingRequestStarted);
		Streaming->OnZoneReady.RemoveDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleZoneReady);
		Streaming->OnZoneReady.AddDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleZoneReady);

		// Game-instance subsystems can be created lazily by the first widget that asks for them.
		// Catch up when zone readiness was already broadcast before this subsystem existed.
		if (Streaming->IsCurrentZoneReady())
		{
			const FName CurrentZoneId = Streaming->GetCurrentZoneId();
			UE_LOG(LogAeyerjiMinimap, Display,
				TEXT("Minimap subsystem initialized after zone readiness; catching up Zone=%s."),
				*CurrentZoneId.ToString());
			ReadyZoneId = CurrentZoneId;
			BeginGenerationRequest(CurrentZoneId);
		}
	}
}

void UAeyerjiMinimapMapSubsystem::Deinitialize()
{
	if (UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem())
	{
		Streaming->OnStreamingRequestStarted.RemoveDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleStreamingRequestStarted);
		Streaming->OnZoneReady.RemoveDynamic(this, &UAeyerjiMinimapMapSubsystem::HandleZoneReady);
	}

	StopWaitingForNavigation();
	ClearGeneratedMap();
	Super::Deinitialize();
}

UAeyerjiMinimapMapSubsystem* UAeyerjiMinimapMapSubsystem::GetMinimapMapSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAeyerjiMinimapMapSubsystem>() : nullptr;
}

FName UAeyerjiMinimapMapSubsystem::GetActiveFloor() const
{
	return FloorMaps.IsValidIndex(ActiveFloorIndex) ? FloorMaps[ActiveFloorIndex].FloorId : NAME_None;
}

UTextureRenderTarget2D* UAeyerjiMinimapMapSubsystem::GetActiveMapTexture() const
{
	return IsMapReady() ? FloorMaps[ActiveFloorIndex].Texture.Get() : nullptr;
}

bool UAeyerjiMinimapMapSubsystem::GetMapWorldBounds(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bMapReady || !MapWorldBounds.bIsValid)
	{
		OutMin = FVector2D::ZeroVector;
		OutMax = FVector2D::ZeroVector;
		return false;
	}

	OutMin = MapWorldBounds.Min;
	OutMax = MapWorldBounds.Max;
	return true;
}

void UAeyerjiMinimapMapSubsystem::UpdateActiveFloor(const float PlayerWorldZ)
{
	if (!bMapReady || FloorMaps.IsEmpty())
	{
		return;
	}

	TArray<FAeyerjiMinimapFloorDef> Definitions;
	Definitions.Reserve(FloorMaps.Num());
	for (const FAeyerjiMinimapFloorMap& FloorMap : FloorMaps)
	{
		FAeyerjiMinimapFloorDef& Definition = Definitions.AddDefaulted_GetRef();
		Definition.FloorId = FloorMap.FloorId;
		Definition.MinZ = FloorMap.MinZ;
		Definition.MaxZ = FloorMap.MaxZ;
	}

	const UAeyerjiMinimapSettings* Settings = GetDefault<UAeyerjiMinimapSettings>();
	ActiveFloorIndex = SelectFloorIndexForHeight(
		Definitions,
		ActiveFloorIndex,
		PlayerWorldZ,
		Settings ? Settings->FloorSwitchHysteresis : 100.f);
}

void UAeyerjiMinimapMapSubsystem::RequestRebuild()
{
	UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem();
	const FName ZoneId = Streaming ? Streaming->GetCurrentZoneId() : NAME_None;
	if (ZoneId.IsNone() || !ShouldRenderLocally())
	{
		return;
	}

	InvalidateCurrentZone();
	if (ReadyZoneId == ZoneId || (Streaming && Streaming->IsCurrentZoneReady()))
	{
		ReadyZoneId = ZoneId;
		BeginGenerationRequest(ZoneId);
	}
	else
	{
		RequestedZoneId = ZoneId;
	}
}

void UAeyerjiMinimapMapSubsystem::InvalidateCurrentZone()
{
	StopWaitingForNavigation();
	ClearGeneratedMap();
	RequestedZoneId = NAME_None;
	bGenerationRequested = false;
}

FVector2D UAeyerjiMinimapMapSubsystem::WorldToMapUV(
	const FVector2D& WorldPosition,
	const FVector2D& MapMin,
	const float MapSide)
{
	const double SafeSide = FMath::Max(static_cast<double>(MapSide), 1.0);
	return FVector2D(
		(WorldPosition.Y - MapMin.Y) / SafeSide,
		1.0 - ((WorldPosition.X - MapMin.X) / SafeSide));
}

int32 UAeyerjiMinimapMapSubsystem::GetFloorRelationForHeight(const float WorldZ) const
{
	if (!FloorMaps.IsValidIndex(ActiveFloorIndex))
	{
		return 0;
	}

	const FAeyerjiMinimapFloorMap& Floor = FloorMaps[ActiveFloorIndex];
	if (WorldZ < Floor.MinZ)
	{
		return -1;
	}
	if (WorldZ > Floor.MaxZ)
	{
		return 1;
	}
	return 0;
}

int32 UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(
	const TArray<FAeyerjiMinimapFloorDef>& Floors,
	const int32 CurrentFloorIndex,
	const float WorldZ,
	const float Hysteresis)
{
	if (Floors.IsEmpty())
	{
		return INDEX_NONE;
	}

	const float SafeHysteresis = FMath::Max(0.f, Hysteresis);
	if (Floors.IsValidIndex(CurrentFloorIndex))
	{
		const FAeyerjiMinimapFloorDef& Current = Floors[CurrentFloorIndex];
		if (WorldZ >= Current.MinZ - SafeHysteresis && WorldZ <= Current.MaxZ + SafeHysteresis)
		{
			return CurrentFloorIndex;
		}
	}

	int32 BestIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Floors.Num(); ++Index)
	{
		const FAeyerjiMinimapFloorDef& Floor = Floors[Index];
		if (!FMath::IsFinite(Floor.MinZ) || !FMath::IsFinite(Floor.MaxZ) || Floor.MaxZ <= Floor.MinZ)
		{
			continue;
		}

		const float Distance = WorldZ < Floor.MinZ
			? Floor.MinZ - WorldZ
			: (WorldZ > Floor.MaxZ ? WorldZ - Floor.MaxZ : 0.f);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

void UAeyerjiMinimapMapSubsystem::HandleStreamingRequestStarted(
	const FName ZoneId,
	const TArray<FName>& LevelsToLoad,
	const TArray<FName>& LevelsToUnload)
{
	static_cast<void>(LevelsToLoad);
	static_cast<void>(LevelsToUnload);
	StopWaitingForNavigation();
	ClearGeneratedMap();
	RequestedZoneId = ZoneId;
	ReadyZoneId = NAME_None;
	bGenerationRequested = false;
}

void UAeyerjiMinimapMapSubsystem::HandleZoneReady(const FName ZoneId)
{
	ReadyZoneId = ZoneId;
	if ((bMapReady && ActiveZoneId == ZoneId)
		|| (bGenerationRequested && RequestedZoneId == ZoneId))
	{
		return;
	}

	BeginGenerationRequest(ZoneId);
}

void UAeyerjiMinimapMapSubsystem::HandleNavigationGenerationFinished(ANavigationData* NavigationData)
{
	if (bGenerationRequested && Cast<ARecastNavMesh>(NavigationData))
	{
		TryGenerateRequestedMap();
	}
}

void UAeyerjiMinimapMapSubsystem::BeginGenerationRequest(const FName ZoneId)
{
	if (ZoneId.IsNone() || !ShouldRenderLocally())
	{
		return;
	}

	StopWaitingForNavigation();
	ClearGeneratedMap();
	RequestedZoneId = ZoneId;
	bGenerationRequested = true;
	GenerationRequestStartSeconds = FPlatformTime::Seconds();

	UWorld* World = GetWorld();
	BoundNavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (BoundNavigationSystem)
	{
		BoundNavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(
			this, &UAeyerjiMinimapMapSubsystem::HandleNavigationGenerationFinished);
		BoundNavigationSystem->OnNavigationGenerationFinishedDelegate.AddDynamic(
			this, &UAeyerjiMinimapMapSubsystem::HandleNavigationGenerationFinished);
	}

	TryGenerateRequestedMap();
}

void UAeyerjiMinimapMapSubsystem::TryGenerateRequestedMap()
{
	if (!bGenerationRequested || RequestedZoneId.IsNone())
	{
		return;
	}

	const UAeyerjiMinimapSettings* Settings = GetDefault<UAeyerjiMinimapSettings>();
	const double Timeout = Settings ? FMath::Max(0.1f, Settings->NavigationWaitTimeout) : 10.0;
	if ((FPlatformTime::Seconds() - GenerationRequestStartSeconds) > Timeout)
	{
		UE_LOG(LogAeyerjiMinimap, Warning,
			TEXT("Minimap generation timed out waiting for navigation Zone=%s Timeout=%.2fs; keeping procedural fallback."),
			*RequestedZoneId.ToString(), Timeout);
		bGenerationRequested = false;
		StopWaitingForNavigation();
		return;
	}

	UWorld* World = GetWorld();
	if (!World || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World) || !GenerateMapFromNavigation())
	{
		ScheduleGenerationRetry();
	}
}

bool UAeyerjiMinimapMapSubsystem::GenerateMapFromNavigation()
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	ARecastNavMesh* RecastNavMesh = NavigationSystem
		? Cast<ARecastNavMesh>(NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		: nullptr;
	if (!World || !RecastNavMesh)
	{
		return false;
	}

	TArray<AeyerjiMinimapMapPrivate::FTriangle> Triangles;
	TArray<AeyerjiMinimapMapPrivate::FEdge> Edges;
	FBox NavigationBounds(EForceInit::ForceInit);
	TArray<FNavTileRef> TileRefs;
	RecastNavMesh->GetAllNavMeshTiles(TileRefs);
	for (const FNavTileRef TileRef : TileRefs)
	{
		FRecastDebugGeometry Geometry;
		Geometry.bGatherNavMeshEdges = true;
		// UE 5.8 returns false for a valid single-tile request even after filling OutGeometry;
		// the return value means the all-tiles traversal is complete, not that extraction failed.
		RecastNavMesh->GetDebugGeometryForTile(Geometry, TileRef);
		if (Geometry.MeshVerts.IsEmpty())
		{
			continue;
		}

		for (int32 AreaIndex = 0; AreaIndex < RECAST_MAX_AREAS; ++AreaIndex)
		{
			const TArray<int32>& Indices = Geometry.AreaIndices[AreaIndex];
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				if (!Geometry.MeshVerts.IsValidIndex(Indices[Index])
					|| !Geometry.MeshVerts.IsValidIndex(Indices[Index + 1])
					|| !Geometry.MeshVerts.IsValidIndex(Indices[Index + 2]))
				{
					continue;
				}

				AeyerjiMinimapMapPrivate::FTriangle& Triangle = Triangles.AddDefaulted_GetRef();
				Triangle.A = Geometry.MeshVerts[Indices[Index]];
				Triangle.B = Geometry.MeshVerts[Indices[Index + 1]];
				Triangle.C = Geometry.MeshVerts[Indices[Index + 2]];
				Triangle.MinZ = FMath::Min3(Triangle.A.Z, Triangle.B.Z, Triangle.C.Z);
				Triangle.MaxZ = FMath::Max3(Triangle.A.Z, Triangle.B.Z, Triangle.C.Z);
				NavigationBounds += Triangle.A;
				NavigationBounds += Triangle.B;
				NavigationBounds += Triangle.C;
			}
		}

		for (int32 EdgeIndex = 0; EdgeIndex + 1 < Geometry.NavMeshEdges.Num(); EdgeIndex += 2)
		{
			AeyerjiMinimapMapPrivate::FEdge& Edge = Edges.AddDefaulted_GetRef();
			Edge.A = Geometry.NavMeshEdges[EdgeIndex];
			Edge.B = Geometry.NavMeshEdges[EdgeIndex + 1];
		}
	}

	if (Triangles.IsEmpty() || !NavigationBounds.IsValid)
	{
		return false;
	}

	FZoneDef ZoneDefinition;
	TArray<FAeyerjiMinimapFloorDef> ValidFloorDefinitions;
	if (UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem();
		Streaming && Streaming->GetZoneDefinition(RequestedZoneId, ZoneDefinition))
	{
		for (const FAeyerjiMinimapFloorDef& Floor : ZoneDefinition.MinimapFloors)
		{
			if (FMath::IsFinite(Floor.MinZ) && FMath::IsFinite(Floor.MaxZ) && Floor.MaxZ > Floor.MinZ)
			{
				FAeyerjiMinimapFloorDef ValidFloor = Floor;
				if (ValidFloor.FloorId.IsNone())
				{
					ValidFloor.FloorId = FName(*FString::Printf(TEXT("Floor.%d"), ValidFloorDefinitions.Num() + 1));
				}
				ValidFloorDefinitions.Add(ValidFloor);
			}
			else
			{
				UE_LOG(LogAeyerjiMinimap, Warning,
					TEXT("Ignoring invalid minimap floor Zone=%s Floor=%s MinZ=%.2f MaxZ=%.2f"),
					*RequestedZoneId.ToString(), *Floor.FloorId.ToString(), Floor.MinZ, Floor.MaxZ);
			}
		}
	}

	if (ValidFloorDefinitions.IsEmpty())
	{
		FAeyerjiMinimapFloorDef& AutomaticFloor = ValidFloorDefinitions.AddDefaulted_GetRef();
		AutomaticFloor.FloorId = FName(TEXT("Floor.Default"));
		AutomaticFloor.MinZ = NavigationBounds.Min.Z - 1.f;
		AutomaticFloor.MaxZ = NavigationBounds.Max.Z + 1.f;
	}

	const UAeyerjiMinimapSettings* Settings = GetDefault<UAeyerjiMinimapSettings>();
	const int32 Resolution = FMath::RoundUpToPowerOfTwo(FMath::Clamp(Settings ? Settings->RasterResolution : 2048, 256, 4096));
	const double Padding = FMath::Max(0.f, Settings ? Settings->WorldPadding : 500.f);
	const FVector Center = NavigationBounds.GetCenter();
	const double Side = FMath::Max(NavigationBounds.GetSize().X, NavigationBounds.GetSize().Y) + Padding * 2.0;
	const double SafeSide = FMath::Max(Side, 100.0);
	MapWorldBounds = FBox2D(
		FVector2D(Center.X - SafeSide * 0.5, Center.Y - SafeSide * 0.5),
		FVector2D(Center.X + SafeSide * 0.5, Center.Y + SafeSide * 0.5));

	TArray<FAeyerjiMinimapFloorMap> GeneratedFloors;
	GeneratedFloors.Reserve(ValidFloorDefinitions.Num());
	for (const FAeyerjiMinimapFloorDef& Definition : ValidFloorDefinitions)
	{
		FAeyerjiMinimapFloorMap& FloorMap = GeneratedFloors.AddDefaulted_GetRef();
		FloorMap.FloorId = Definition.FloorId;
		FloorMap.MinZ = Definition.MinZ;
		FloorMap.MaxZ = Definition.MaxZ;
		FloorMap.Texture = NewObject<UTextureRenderTarget2D>(this);
		FloorMap.Texture->AddressX = TA_Clamp;
		FloorMap.Texture->AddressY = TA_Clamp;
		FloorMap.Texture->Filter = TF_Bilinear;
		FloorMap.Texture->RenderTargetFormat = RTF_RGBA8;
		FloorMap.Texture->ClearColor = Settings ? Settings->EmptyColor : FLinearColor::Black;
		FloorMap.Texture->bAutoGenerateMips = false;
		FloorMap.Texture->InitAutoFormat(Resolution, Resolution);
		FloorMap.Texture->UpdateResourceImmediate(true);

		FTextureRenderTargetResource* RenderTargetResource = FloorMap.Texture->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return false;
		}

		FCanvas Canvas(RenderTargetResource, nullptr, FGameTime(), World->GetFeatureLevel());
		Canvas.Clear(Settings ? Settings->EmptyColor : FLinearColor::Black);

		TArray<FCanvasUVTri> CanvasTriangles;
		CanvasTriangles.Reserve(Triangles.Num());
		for (const AeyerjiMinimapMapPrivate::FTriangle& Triangle : Triangles)
		{
			if (!AeyerjiMinimapMapPrivate::IntersectsFloor(Triangle.MinZ, Triangle.MaxZ, FloorMap))
			{
				continue;
			}

			FCanvasUVTri& CanvasTriangle = CanvasTriangles.AddDefaulted_GetRef();
			CanvasTriangle.V0_Pos = AeyerjiMinimapMapPrivate::WorldToPixel(Triangle.A, MapWorldBounds, Resolution);
			CanvasTriangle.V1_Pos = AeyerjiMinimapMapPrivate::WorldToPixel(Triangle.B, MapWorldBounds, Resolution);
			CanvasTriangle.V2_Pos = AeyerjiMinimapMapPrivate::WorldToPixel(Triangle.C, MapWorldBounds, Resolution);
			CanvasTriangle.V0_UV = FVector2D::ZeroVector;
			CanvasTriangle.V1_UV = FVector2D::ZeroVector;
			CanvasTriangle.V2_UV = FVector2D::ZeroVector;
			CanvasTriangle.V0_Color = Settings ? Settings->WalkableFillColor : FLinearColor::Gray;
			CanvasTriangle.V1_Color = CanvasTriangle.V0_Color;
			CanvasTriangle.V2_Color = CanvasTriangle.V0_Color;
		}

		if (!CanvasTriangles.IsEmpty())
		{
			FCanvasTriangleItem TriangleItem(CanvasTriangles, GWhiteTexture);
			TriangleItem.BlendMode = SE_BLEND_Opaque;
			Canvas.DrawItem(TriangleItem);
		}

		for (const AeyerjiMinimapMapPrivate::FEdge& Edge : Edges)
		{
			const float EdgeMinZ = FMath::Min(Edge.A.Z, Edge.B.Z);
			const float EdgeMaxZ = FMath::Max(Edge.A.Z, Edge.B.Z);
			if (!AeyerjiMinimapMapPrivate::IntersectsFloor(EdgeMinZ, EdgeMaxZ, FloorMap))
			{
				continue;
			}

			FCanvasLineItem LineItem(
				AeyerjiMinimapMapPrivate::WorldToPixel(Edge.A, MapWorldBounds, Resolution),
				AeyerjiMinimapMapPrivate::WorldToPixel(Edge.B, MapWorldBounds, Resolution));
			LineItem.SetColor(Settings ? Settings->WalkableBoundaryColor : FLinearColor::White);
			LineItem.LineThickness = Settings ? Settings->BoundaryThickness : 1.5f;
			Canvas.DrawItem(LineItem);
		}

		Canvas.Flush_GameThread();
	}

	FloorMaps = MoveTemp(GeneratedFloors);
	ActiveFloorIndex = FloorMaps.IsEmpty() ? INDEX_NONE : 0;
	ActiveZoneId = RequestedZoneId;
	bMapReady = FloorMaps.IsValidIndex(ActiveFloorIndex);
	bGenerationRequested = false;
	StopWaitingForNavigation();

	if (!bMapReady)
	{
		ClearGeneratedMap();
		return false;
	}

	++GenerationCount;
	const double ElapsedMilliseconds = (FPlatformTime::Seconds() - GenerationRequestStartSeconds) * 1000.0;
	UE_LOG(LogAeyerjiMinimap, Display,
		TEXT("Generated navmesh minimap Zone=%s Floors=%d Triangles=%d Edges=%d Resolution=%d DurationMs=%.2f Generation=%d"),
		*ActiveZoneId.ToString(), FloorMaps.Num(), Triangles.Num(), Edges.Num(), Resolution, ElapsedMilliseconds, GenerationCount);
	OnMinimapMapReady.Broadcast(ActiveZoneId);
	return true;
}

void UAeyerjiMinimapMapSubsystem::ScheduleGenerationRetry()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(GenerationRetryTimer))
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		GenerationRetryTimer,
		this,
		&UAeyerjiMinimapMapSubsystem::TryGenerateRequestedMap,
		0.25f,
		false);
}

void UAeyerjiMinimapMapSubsystem::StopWaitingForNavigation()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GenerationRetryTimer);
	}

	if (BoundNavigationSystem)
	{
		BoundNavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(
			this, &UAeyerjiMinimapMapSubsystem::HandleNavigationGenerationFinished);
	}
	BoundNavigationSystem = nullptr;
}

void UAeyerjiMinimapMapSubsystem::ClearGeneratedMap()
{
	FloorMaps.Reset();
	MapWorldBounds = FBox2D(EForceInit::ForceInit);
	ActiveFloorIndex = INDEX_NONE;
	ActiveZoneId = NAME_None;
	bMapReady = false;
}

bool UAeyerjiMinimapMapSubsystem::ShouldRenderLocally() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_DedicatedServer;
}

UAeyerjiStreamingSubsystem* UAeyerjiMinimapMapSubsystem::GetStreamingSubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UAeyerjiStreamingSubsystem>() : nullptr;
}
