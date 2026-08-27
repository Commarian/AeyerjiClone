#include "Environment/NeonStripSplineActor.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/KismetMathLibrary.h"

static const FName kNeonMeshTag(TEXT("NeonStripGen"));
static const FName kNeonLightTag(TEXT("NeonLightGen"));

namespace
{
	constexpr int32 MaxGeneratedNeonComponents = 4096;
	constexpr float MaxNeonDistance = 10000000.f;
	constexpr float MaxNeonIntensity = 1000000000.f;

	float SafeNeonValue(const float Value, const float DefaultValue, const float MinValue, const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}

	bool IsFiniteNeonVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FLinearColor SafeNeonColor(const FLinearColor& Value)
	{
		return FLinearColor(
			SafeNeonValue(Value.R, 0.f, 0.f, 1000.f),
			SafeNeonValue(Value.G, 0.95f, 0.f, 1000.f),
			SafeNeonValue(Value.B, 1.f, 0.f, 1000.f),
			SafeNeonValue(Value.A, 1.f, 0.f, 1.f));
	}
}

ANeonStripSplineActor::ANeonStripSplineActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    Spline->SetupAttachment(RootComponent);
    Spline->SetMobility(EComponentMobility::Movable);
    Spline->SetClosedLoop(false);

    // Defaults
    StripMesh = nullptr;
    StripMaterial = nullptr;
    bGenerateMesh = true;
    StripWidth = 20.0f;      // cm as scale relative to 100uu
    StripThickness = 2.0f;   // cm as scale relative to 100uu
    RollDegrees = 0.0f;
    MeshZOffset = 0.5f;
    ForwardAxis = ESplineMeshAxis::X;

    bGenerateLights = true;
    LightMode = ENeonLightMode::Rect;
    bLightPerSegment = true;
    LightSpacing = 400.0f;
    LightColor = FLinearColor(0.0f, 0.95f, 1.0f, 1.0f);
    LightIntensity = 5000.0f;
    LightAttenuationRadius = 800.0f;
    LightSourceWidth = 25.0f;
    LightSourceHeight = 2.0f;
    bAlignLightsToTangent = true;
    bCastLightShadows = false;
    LightZOffset = 0.0f;

    // Point light defaults
    bPointPerSegment = false;
    PointLightSpacing = 600.0f;
    PointLightIntensity = 4000.0f;
    PointLightAttenuationRadius = 700.0f;
    bPointUseInverseSquaredFalloff = true;

    // Fallbacks for convenience
    {
        static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("StaticMesh'/Engine/BasicShapes/Plane.Plane'"));
        if (PlaneMesh.Succeeded())
        {
            StripMesh = PlaneMesh.Object;
        }
    }
    {
        static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMat(TEXT("MaterialInstanceConstant'/Game/Materials/MI_Neon_Cyan.MI_Neon_Cyan'"));
        if (DefaultMat.Succeeded())
        {
            StripMaterial = DefaultMat.Object;
        }
    }
}

void ANeonStripSplineActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Rebuild();
}

void ANeonStripSplineActor::Rebuild()
{
    ClearGeneratedComponents();
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

    BuildMeshes();
    if (bGenerateLights)
    {
        BuildLights();
    }
}

void ANeonStripSplineActor::ClearGeneratedComponents()
{
    // Remove previous spline meshes
    TInlineComponentArray<USplineMeshComponent*> Meshes(this);
    for (USplineMeshComponent* C : Meshes)
    {
        if (!IsValid(C)) continue;
        if (C->ComponentTags.Contains(kNeonMeshTag))
        {
            C->DestroyComponent();
        }
    }

    // Remove previous generated lights (rect or point)
    {
        TInlineComponentArray<URectLightComponent*> Rects(this);
        for (URectLightComponent* L : Rects)
        {
            if (!IsValid(L)) continue;
            if (L->ComponentTags.Contains(kNeonLightTag))
            {
                L->DestroyComponent();
            }
        }
        TInlineComponentArray<UPointLightComponent*> Points(this);
        for (UPointLightComponent* L : Points)
        {
            if (!IsValid(L)) continue;
            if (L->ComponentTags.Contains(kNeonLightTag))
            {
                L->DestroyComponent();
            }
        }
    }
}

void ANeonStripSplineActor::BuildMeshes()
{
    if (!Spline || !bGenerateMesh || !StripMesh)
    {
        return;
    }

    const int32 NumPoints = Spline->GetNumberOfSplinePoints();
    if (NumPoints < 2)
    {
        return;
    }

	const float ScaleY = SafeNeonValue(StripWidth / 100.0f, 0.2f, 0.f, 1000.f);
	const float ScaleZ = SafeNeonValue(StripThickness / 100.0f, 0.02f, 0.f, 1000.f);
	const float SafeRollRadians = FMath::DegreesToRadians(SafeNeonValue(RollDegrees, 0.f, -360000.f, 360000.f));
	const float SafeMeshZOffset = SafeNeonValue(MeshZOffset, 0.f, -MaxNeonDistance, MaxNeonDistance);
	const uint8 RawForwardAxis = ForwardAxis.GetValue();
	const ESplineMeshAxis::Type SafeForwardAxis = RawForwardAxis <= ESplineMeshAxis::Z
		? static_cast<ESplineMeshAxis::Type>(RawForwardAxis)
		: ESplineMeshAxis::X;

    const bool bClosed = Spline->IsClosedLoop();
	const int32 EndIndex = FMath::Min(bClosed ? NumPoints : (NumPoints - 1), MaxGeneratedNeonComponents);
    for (int32 i = 0; i < EndIndex; ++i)
    {
        const int32 NextIndex = (i + 1) % NumPoints;
        const FVector StartPos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
        const FVector EndPos   = Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
        const FVector StartTan = Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
        const FVector EndTan   = Spline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
		if (!IsFiniteNeonVector(StartPos) || !IsFiniteNeonVector(EndPos)
			|| !IsFiniteNeonVector(StartTan) || !IsFiniteNeonVector(EndTan))
		{
			continue;
		}

        const FName SplineMeshName = MakeUniqueObjectName(this, USplineMeshComponent::StaticClass(), TEXT("NeonStripSplineMesh"));
		USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(
			this, USplineMeshComponent::StaticClass(), SplineMeshName, RF_Transactional);
		if (!SplineMesh)
		{
			continue;
		}
        SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        SplineMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        SplineMesh->SetMobility(EComponentMobility::Movable);
        SplineMesh->ComponentTags.Add(kNeonMeshTag);

        SplineMesh->SetStaticMesh(StripMesh);
        if (StripMaterial)
        {
            SplineMesh->SetMaterial(0, StripMaterial);
        }
		SplineMesh->SetForwardAxis(SafeForwardAxis);
        SplineMesh->SetStartAndEnd(StartPos, StartTan, EndPos, EndTan, true);
        SplineMesh->SetStartScale(FVector2D(ScaleY, ScaleZ));
        SplineMesh->SetEndScale(FVector2D(ScaleY, ScaleZ));
		SplineMesh->SetStartRoll(SafeRollRadians);
		SplineMesh->SetEndRoll(SafeRollRadians);
        SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SplineMesh->SetCastShadow(false);
		if (!FMath::IsNearlyZero(SafeMeshZOffset))
        {
			SplineMesh->AddLocalOffset(FVector(0.f, 0.f, SafeMeshZOffset));
        }
		SplineMesh->RegisterComponent();
    }
}

void ANeonStripSplineActor::BuildLights()
{
    if (!Spline || !bGenerateLights || LightMode == ENeonLightMode::None)
    {
        return;
    }

    const int32 NumPoints = Spline->GetNumberOfSplinePoints();
    if (NumPoints < 2)
    {
        return;
    }

    auto CreateRectLightAt = [this](const FVector& LocalPos, const FVector& LocalTangent)
    {
		if (!IsFiniteNeonVector(LocalPos) || !IsFiniteNeonVector(LocalTangent))
		{
			return;
		}

        const FName RectLightName = MakeUniqueObjectName(this, URectLightComponent::StaticClass(), TEXT("NeonRectLight"));
		URectLightComponent* Rect = NewObject<URectLightComponent>(
			this, URectLightComponent::StaticClass(), RectLightName, RF_Transactional);
		if (!Rect)
		{
			return;
		}
        Rect->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Rect->AttachToComponent(this->RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Rect->SetMobility(EComponentMobility::Movable);
        Rect->ComponentTags.Add(kNeonLightTag);

        Rect->SetRelativeLocation(LocalPos);
        if (this->bAlignLightsToTangent)
        {
			const FVector Dir = LocalTangent.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
            const FRotator Rot = FRotationMatrix::MakeFromX(Dir).Rotator();
            Rect->SetRelativeRotation(Rot);
        }

		Rect->SetLightColor(SafeNeonColor(this->LightColor), true);
		Rect->SetIntensity(SafeNeonValue(this->LightIntensity, 5000.f, 0.f, MaxNeonIntensity));
		Rect->SetAttenuationRadius(SafeNeonValue(this->LightAttenuationRadius, 800.f, 0.f, MaxNeonDistance));
		Rect->SetSourceWidth(SafeNeonValue(this->LightSourceWidth, 25.f, 0.1f, MaxNeonDistance));
		Rect->SetSourceHeight(SafeNeonValue(this->LightSourceHeight, 2.f, 0.1f, MaxNeonDistance));
        Rect->SetCastShadows(this->bCastLightShadows);
		Rect->RegisterComponent();
    };

    auto CreatePointLightAt = [this](const FVector& LocalPos)
    {
		if (!IsFiniteNeonVector(LocalPos))
		{
			return;
		}

        const FName PointLightName = MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("NeonPointLight"));
		UPointLightComponent* Pt = NewObject<UPointLightComponent>(
			this, UPointLightComponent::StaticClass(), PointLightName, RF_Transactional);
		if (!Pt)
		{
			return;
		}
        Pt->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Pt->AttachToComponent(this->RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Pt->SetMobility(EComponentMobility::Movable);
        Pt->ComponentTags.Add(kNeonLightTag);

        Pt->SetRelativeLocation(LocalPos);
		Pt->SetLightColor(SafeNeonColor(this->LightColor), true);
		Pt->SetIntensity(SafeNeonValue(this->PointLightIntensity, 4000.f, 0.f, MaxNeonIntensity));
		Pt->SetAttenuationRadius(SafeNeonValue(this->PointLightAttenuationRadius, 700.f, 0.f, MaxNeonDistance));
        Pt->bUseInverseSquaredFalloff = this->bPointUseInverseSquaredFalloff;
        Pt->SetCastShadows(false); // cheap by default; can add control if needed
		Pt->RegisterComponent();
    };

    if (this->LightMode == ENeonLightMode::Rect)
    {
        if (this->bLightPerSegment)
        {
            const bool bClosed = this->Spline->IsClosedLoop();
			const int32 EndIndex = FMath::Min(
				bClosed ? NumPoints : (NumPoints - 1), MaxGeneratedNeonComponents);
            for (int32 i = 0; i < EndIndex; ++i)
            {
                const int32 NextIndex = (i + 1) % NumPoints;
                const FVector StartPos = this->Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndPos   = this->Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector Mid      = (StartPos + EndPos) * 0.5f;
                const FVector StartTan = this->Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndTan   = this->Spline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector MidTan   = (StartTan + EndTan) * 0.5f;
				CreateRectLightAt(Mid + FVector(0.f, 0.f, SafeNeonValue(
					LightZOffset, 0.f, -MaxNeonDistance, MaxNeonDistance)), MidTan);
            }
        }
        else
        {
			const float Length = this->Spline->GetSplineLength();
			if (!FMath::IsFinite(Length) || Length <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const float SafeSpacing = SafeNeonValue(this->LightSpacing, 400.f, 10.f, MaxNeonDistance);
			const int32 Steps = FMath::Clamp(FMath::FloorToInt(FMath::Min(
				Length / SafeSpacing, static_cast<float>(MaxGeneratedNeonComponents - 1))), 1, MaxGeneratedNeonComponents - 1);
            for (int32 s = 0; s <= Steps; ++s)
            {
                const float Dist = FMath::Clamp((s / (float)Steps) * Length, 0.0f, Length);
				const FVector Pos = this->Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local)
					+ FVector(0.f, 0.f, SafeNeonValue(LightZOffset, 0.f, -MaxNeonDistance, MaxNeonDistance));
                const FVector Tan = this->Spline->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local);
                CreateRectLightAt(Pos, Tan);
            }
        }
    }
    else if (this->LightMode == ENeonLightMode::Point)
    {
        if (this->bPointPerSegment)
        {
            const bool bClosed = this->Spline->IsClosedLoop();
			const int32 EndIndex = FMath::Min(
				bClosed ? NumPoints : (NumPoints - 1), MaxGeneratedNeonComponents);
            for (int32 i = 0; i < EndIndex; ++i)
            {
                const int32 NextIndex = (i + 1) % NumPoints;
                const FVector StartPos = this->Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndPos   = this->Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector Mid      = (StartPos + EndPos) * 0.5f;
				CreatePointLightAt(Mid + FVector(0.f, 0.f, SafeNeonValue(
					LightZOffset, 0.f, -MaxNeonDistance, MaxNeonDistance)));
            }
        }
        else
        {
			const float Length = this->Spline->GetSplineLength();
			if (!FMath::IsFinite(Length) || Length <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const float SafeSpacing = SafeNeonValue(this->PointLightSpacing, 600.f, 10.f, MaxNeonDistance);
			const int32 Steps = FMath::Clamp(FMath::FloorToInt(FMath::Min(
				Length / SafeSpacing, static_cast<float>(MaxGeneratedNeonComponents - 1))), 1, MaxGeneratedNeonComponents - 1);
            for (int32 s = 0; s <= Steps; ++s)
            {
                const float Dist = FMath::Clamp((s / (float)Steps) * Length, 0.0f, Length);
				const FVector Pos = this->Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local)
					+ FVector(0.f, 0.f, SafeNeonValue(LightZOffset, 0.f, -MaxNeonDistance, MaxNeonDistance));
                CreatePointLightAt(Pos);
            }
        }
    }
}
