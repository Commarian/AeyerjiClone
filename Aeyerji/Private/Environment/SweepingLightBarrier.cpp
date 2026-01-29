#include "Environment/SweepingLightBarrier.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/StaticMesh.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameplayEffect.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ASweepingLightBarrier::ASweepingLightBarrier()
{
    PrimaryActorTick.bCanEverTick = true;

    Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
    RootComponent = Path;
    Path->SetClosedLoop(false);
    // Two default points for convenience
    Path->ClearSplinePoints(false);
    Path->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local);
    Path->AddSplinePoint(FVector(2000.f, 0.f, 0.f), ESplineCoordinateSpace::Local);
    Path->UpdateSpline();

    BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierMesh"));
    BarrierMesh->SetupAttachment(RootComponent);
    BarrierMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BarrierMesh->SetCastShadow(false);

    DamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageBox"));
    DamageBox->SetupAttachment(BarrierMesh);
    DamageBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DamageBox->SetCollisionObjectType(ECC_WorldDynamic);
    DamageBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    DamageBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    DamageBox->OnComponentBeginOverlap.AddDynamic(this, &ASweepingLightBarrier::OnDamageOverlapBegin);
    DamageBox->OnComponentEndOverlap.AddDynamic(this, &ASweepingLightBarrier::OnDamageOverlapEnd);

    RectLight = CreateDefaultSubobject<URectLightComponent>(TEXT("RectLight"));
    RectLight->SetupAttachment(BarrierMesh);
    RectLight->SetCastShadows(false);

    // Defaults
    BarrierWidth = 800.f;   // cm across walkway
    BarrierHeight = 300.f;  // cm
    YawOffsetDegrees = 90.f;

    SweepSpeed = 800.f;
    PauseAtEnds = 1.0f;
    bPingPong = true;

    bUseRectLight = true;
    LightColor = FLinearColor(0.f, 0.95f, 1.f, 1.f);
    LightIntensity = 5000.f;
    LightSourceWidth = 800.f;
    LightSourceHeight = 10.f;

    bDamageContinuous = true;
    DamagePerSecond = 15.f;
    DamageTypeClass = UDamageType::StaticClass();
    DamageEffectClass = UGE_DamagePhysical::StaticClass();
    if (!DamageSetByCallerTag.IsValid())
    {
        DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.Instant"), /*ErrorIfNotFound=*/false);
    }
    DamageTypeTag = AeyerjiTags::DamageType_Physical;

    Alpha = 0.f;
    Direction = +1;
    PauseTimer = 0.f;

    // Simple default mesh/material for visibility
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("StaticMesh'/Engine/BasicShapes/Plane.Plane'"));
    if (PlaneMesh.Succeeded())
    {
        BarrierMesh->SetStaticMesh(PlaneMesh.Object);
    }
}

void ASweepingLightBarrier::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SplineLen = Path->GetSplineLength();

    // Apply material
    if (BarrierMaterial)
    {
        BarrierMesh->SetMaterial(0, BarrierMaterial);
    }

    // Scale the plane: plane is 100x100 by default; scale X used as thickness along tangent, Y across width
    const float ScaleX = 0.1f; // thin thickness
    const float ScaleY = BarrierWidth / 100.f;
    const float ScaleZ = BarrierHeight / 100.f; // for plane Z is ignored visually, but keep consistent
    BarrierMesh->SetRelativeScale3D(FVector(ScaleX, ScaleY, 1.f));

    // Damage box roughly matches visible barrier with a little thickness
    DamageBox->SetBoxExtent(FVector(50.f, BarrierWidth * 0.5f, FMath::Max(20.f, BarrierHeight * 0.5f)));
    DamageBox->SetRelativeLocation(FVector(0.f, 0.f, BarrierHeight * 0.5f));

    // Light
    RectLight->SetVisibility(bUseRectLight);
    RectLight->SetLightColor(LightColor, true);
    RectLight->SetIntensity(LightIntensity);
    RectLight->SetSourceWidth(LightSourceWidth);
    RectLight->SetSourceHeight(LightSourceHeight);

    UpdateBarrierTransform();
}

void ASweepingLightBarrier::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (SplineLen <= KINDA_SMALL_NUMBER)
    {
        SplineLen = Path->GetSplineLength();
        if (SplineLen <= KINDA_SMALL_NUMBER)
        {
            return;
        }
    }

    if (PauseTimer > 0.f)
    {
        PauseTimer -= DeltaSeconds;
        return;
    }

    const float UnitsPerSecond = FMath::Max(1.f, SweepSpeed);
    const float dAlpha = (UnitsPerSecond / SplineLen) * DeltaSeconds * (float)Direction;
    Alpha += dAlpha;

    if (Alpha >= 1.f)
    {
        Alpha = 1.f;
        if (bPingPong)
        {
            Direction = -1;
            PauseTimer = PauseAtEnds;
        }
        else
        {
            Alpha = 0.f; // loop
        }
    }
    else if (Alpha <= 0.f)
    {
        Alpha = 0.f;
        if (bPingPong)
        {
            Direction = +1;
            PauseTimer = PauseAtEnds;
        }
        else
        {
            Alpha = 1.f;
        }
    }

    UpdateBarrierTransform();

    // Apply continuous damage
    if (bDamageContinuous && Overlapping.Num() > 0 && DamagePerSecond > 0.f)
    {
        const float Damage = DamagePerSecond * DeltaSeconds;
        for (auto It = Overlapping.CreateIterator(); It; ++It)
        {
            if (AActor* Other = It->Get())
            {
                ApplyBarrierDamage(Other, Damage);
            }
        }
    }
}

void ASweepingLightBarrier::UpdateBarrierTransform()
{
    const float Dist = FMath::Clamp(Alpha, 0.f, 1.f) * SplineLen;
    const FVector Pos = Path->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local);
    const FVector Tan = Path->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::Local).GetSafeNormal();
    const FRotator Rot = FRotationMatrix::MakeFromX(Tan).Rotator() + FRotator(0.f, YawOffsetDegrees, 0.f);

    BarrierMesh->SetRelativeLocation(Pos);
    BarrierMesh->SetRelativeRotation(Rot);

    RectLight->SetRelativeLocation(FVector(0.f, 0.f, BarrierHeight * 0.5f));
    RectLight->SetRelativeRotation(FRotator::ZeroRotator);
}

void ASweepingLightBarrier::ApplyBarrierDamage(AActor* Other, float Damage)
{
    if (!IsValid(Other) || Damage <= 0.f)
    {
        return;
    }

    if (DamageEffectClass)
    {
        if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Other))
        {
            FGameplayEffectContextHandle ContextHandle = TargetASC->MakeEffectContext();
            ContextHandle.AddSourceObject(this);

            FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(DamageEffectClass, 1.f, ContextHandle);
            if (SpecHandle.IsValid() && SpecHandle.Data.IsValid())
            {
                const FGameplayTag DamageTag = DamageSetByCallerTag.IsValid()
                    ? DamageSetByCallerTag
                    : FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.Instant"), /*ErrorIfNotFound=*/false);
                if (DamageTag.IsValid())
                {
                    SpecHandle.Data->SetSetByCallerMagnitude(DamageTag, Damage);
                }
                if (DamageTypeTag.IsValid())
                {
                    SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
                }

                TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
                return;
            }
        }
    }

    UGameplayStatics::ApplyDamage(Other, Damage, GetInstigatorController(), this, DamageTypeClass);
}

void ASweepingLightBarrier::OnDamageOverlapBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
    if (!IsValid(Other) || Other == this)
    {
        return;
    }
    Overlapping.Add(Other);

    if (!bDamageContinuous && DamagePerSecond > 0.f)
    {
        ApplyBarrierDamage(Other, DamagePerSecond);
    }
}

void ASweepingLightBarrier::OnDamageOverlapEnd(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex)
{
    Overlapping.Remove(Other);
}
