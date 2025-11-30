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

    const float ScaleY = FMath::Max(0.0f, StripWidth / 100.0f);
    const float ScaleZ = FMath::Max(0.0f, StripThickness / 100.0f);

    const bool bClosed = Spline->IsClosedLoop();
    const int32 EndIndex = bClosed ? NumPoints : (NumPoints - 1);
    for (int32 i = 0; i < EndIndex; ++i)
    {
        const int32 NextIndex = (i + 1) % NumPoints;
        const FVector StartPos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
        const FVector EndPos   = Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
        const FVector StartTan = Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
        const FVector EndTan   = Spline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);

        USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(this);
        SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        SplineMesh->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        SplineMesh->RegisterComponent();
        SplineMesh->SetMobility(EComponentMobility::Movable);
        SplineMesh->ComponentTags.Add(kNeonMeshTag);

        SplineMesh->SetStaticMesh(StripMesh);
        if (StripMaterial)
        {
            SplineMesh->SetMaterial(0, StripMaterial);
        }
        SplineMesh->SetForwardAxis(ForwardAxis);
        SplineMesh->SetStartAndEnd(StartPos, StartTan, EndPos, EndTan, true);
        SplineMesh->SetStartScale(FVector2D(ScaleY, ScaleZ));
        SplineMesh->SetEndScale(FVector2D(ScaleY, ScaleZ));
        SplineMesh->SetStartRoll(FMath::DegreesToRadians(RollDegrees));
        SplineMesh->SetEndRoll(FMath::DegreesToRadians(RollDegrees));
        SplineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SplineMesh->SetCastShadow(false);
        if (!FMath::IsNearlyZero(MeshZOffset))
        {
            SplineMesh->AddLocalOffset(FVector(0.f, 0.f, MeshZOffset));
        }
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
        URectLightComponent* Rect = NewObject<URectLightComponent>(this);
        Rect->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Rect->AttachToComponent(this->RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Rect->RegisterComponent();
        Rect->SetMobility(EComponentMobility::Movable);
        Rect->ComponentTags.Add(kNeonLightTag);

        Rect->SetRelativeLocation(LocalPos);
        if (this->bAlignLightsToTangent)
        {
            const FVector Dir = LocalTangent.GetSafeNormal();
            const FRotator Rot = FRotationMatrix::MakeFromX(Dir).Rotator();
            Rect->SetRelativeRotation(Rot);
        }

        Rect->SetLightColor(this->LightColor, true);
        Rect->SetIntensity(this->LightIntensity);
        Rect->SetAttenuationRadius(this->LightAttenuationRadius);
        Rect->SetSourceWidth(this->LightSourceWidth);
        Rect->SetSourceHeight(this->LightSourceHeight);
        Rect->SetCastShadows(this->bCastLightShadows);
    };

    auto CreatePointLightAt = [this](const FVector& LocalPos)
    {
        UPointLightComponent* Pt = NewObject<UPointLightComponent>(this);
        Pt->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Pt->AttachToComponent(this->RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
        Pt->RegisterComponent();
        Pt->SetMobility(EComponentMobility::Movable);
        Pt->ComponentTags.Add(kNeonLightTag);

        Pt->SetRelativeLocation(LocalPos);
        Pt->SetLightColor(this->LightColor, true);
        Pt->SetIntensity(this->PointLightIntensity);
        Pt->SetAttenuationRadius(this->PointLightAttenuationRadius);
        Pt->bUseInverseSquaredFalloff = this->bPointUseInverseSquaredFalloff;
        Pt->SetCastShadows(false); // cheap by default; can add control if needed
    };

    if (this->LightMode == ENeonLightMode::Rect)
    {
        if (this->bLightPerSegment)
        {
            const bool bClosed = this->Spline->IsClosedLoop();
            const int32 EndIndex = bClosed ? NumPoints : (NumPoints - 1);
            for (int32 i = 0; i < EndIndex; ++i)
            {
                const int32 NextIndex = (i + 1) % NumPoints;
                const FVector StartPos = this->Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndPos   = this->Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector Mid      = (StartPos + EndPos) * 0.5f;
                const FVector StartTan = this->Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndTan   = this->Spline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector MidTan   = (StartTan + EndTan) * 0.5f;
                CreateRectLightAt(Mid + FVector(0.f, 0.f, LightZOffset), MidTan);
            }
        }
        else
        {
            const float Length = this->Spline->GetSplineLength();
            const int32 Steps = FMath::Max(1, FMath::FloorToInt(Length / FMath::Max(10.0f, this->LightSpacing)));
            for (int32 s = 0; s <= Steps; ++s)
            {
                const float Dist = FMath::Clamp((s / (float)Steps) * Length, 0.0f, Length);
                const FVector Pos = this->Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local) + FVector(0.f, 0.f, LightZOffset);
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
            const int32 EndIndex = bClosed ? NumPoints : (NumPoints - 1);
            for (int32 i = 0; i < EndIndex; ++i)
            {
                const int32 NextIndex = (i + 1) % NumPoints;
                const FVector StartPos = this->Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                const FVector EndPos   = this->Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
                const FVector Mid      = (StartPos + EndPos) * 0.5f;
                CreatePointLightAt(Mid + FVector(0.f, 0.f, LightZOffset));
            }
        }
        else
        {
            const float Length = this->Spline->GetSplineLength();
            const int32 Steps = FMath::Max(1, FMath::FloorToInt(Length / FMath::Max(10.0f, this->PointLightSpacing)));
            for (int32 s = 0; s <= Steps; ++s)
            {
                const float Dist = FMath::Clamp((s / (float)Steps) * Length, 0.0f, Length);
                const FVector Pos = this->Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local) + FVector(0.f, 0.f, LightZOffset);
                CreatePointLightAt(Pos);
            }
        }
    }
}
