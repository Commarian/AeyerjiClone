// AeyerjiOutlinePPManager.cpp

#include "Components/AeyerjiOutlinePPManager.h"

#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AAeyerjiOutlinePPManager::AAeyerjiOutlinePPManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAeyerjiOutlinePPManager::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	APostProcessVolume* PostProcessVolume = TargetPostProcessVolume ? TargetPostProcessVolume.Get() : FindFallbackPostProcessVolume();
	if (!PostProcessVolume)
	{
		UE_LOG(LogTemp, Warning, TEXT("AAeyerjiOutlinePPManager: No PostProcessVolume found. Assign one to TargetPostProcessVolume."));
		return;
	}

	OutlineMIDs.Reset();

	if (OutlinePostProcessMaterial)
	{
		if (UMaterialInstanceDynamic* PrimaryMID = AddMaterialToVolume(PostProcessVolume, OutlinePostProcessMaterial))
		{
			OutlineMIDs.Add(OutlinePostProcessMaterial, PrimaryMID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AAeyerjiOutlinePPManager: Failed to create outline MID from primary material %s."),
				*GetNameSafe(OutlinePostProcessMaterial));
		}
	}

	for (UMaterialInterface* AdditionalMaterial : AdditionalOutlineMaterials)
	{
		if (!AdditionalMaterial)
		{
			continue;
		}

		if (UMaterialInstanceDynamic* AdditionalMID = AddMaterialToVolume(PostProcessVolume, AdditionalMaterial))
		{
			OutlineMIDs.Add(AdditionalMaterial, AdditionalMID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AAeyerjiOutlinePPManager: Failed to create outline MID from additional material %s."),
				*GetNameSafe(AdditionalMaterial));
		}
	}

	if (OutlineMIDs.Num() == 0)
	{
		if (UMaterialInstanceDynamic* ExistingMID = AcquireOrWrapExistingMID(PostProcessVolume))
		{
			OutlineMIDs.Add(nullptr, ExistingMID);
			UE_LOG(LogTemp, Log, TEXT("AAeyerjiOutlinePPManager: Using existing outline MID on volume %s."), *GetNameSafe(PostProcessVolume));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AAeyerjiOutlinePPManager: No outline MID created or found on volume %s."), *GetNameSafe(PostProcessVolume));
		}
	}
}

APostProcessVolume* AAeyerjiOutlinePPManager::FindFallbackPostProcessVolume() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
	{
		if (APostProcessVolume* Volume = *It)
		{
			if (Volume->bUnbound)
			{
				return Volume;
			}
		}
	}

	for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
	{
		if (APostProcessVolume* Volume = *It)
		{
			return Volume;
		}
	}

	return nullptr;
}

UMaterialInstanceDynamic* AAeyerjiOutlinePPManager::AcquireOrWrapExistingMID(APostProcessVolume* PostProcessVolume)
{
	if (!PostProcessVolume)
	{
		return nullptr;
	}

	for (FWeightedBlendable& Blendable : PostProcessVolume->Settings.WeightedBlendables.Array)
	{
		if (!Blendable.Object)
		{
			continue;
		}

		if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(Blendable.Object))
		{
			return ExistingMID;
		}

		if (UMaterialInterface* Material = Cast<UMaterialInterface>(Blendable.Object))
		{
			UMaterialInstanceDynamic* WrappedMID = UMaterialInstanceDynamic::Create(Material, this);
			if (WrappedMID)
			{
				Blendable.Object = WrappedMID;
				return WrappedMID;
			}
		}
	}

	return nullptr;
}

UMaterialInstanceDynamic* AAeyerjiOutlinePPManager::AddMaterialToVolume(APostProcessVolume* PostProcessVolume, UMaterialInterface* Material)
{
	if (!PostProcessVolume || !Material)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(Material, this);
	if (NewMID)
	{
		if (const UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(Material))
		{
			NewMID->CopyParameterOverrides(const_cast<UMaterialInstance*>(SourceInstance));
		}

		PostProcessVolume->Settings.AddBlendable(NewMID, 1.0f);
	}
	return NewMID;
}

UMaterialInstanceDynamic* AAeyerjiOutlinePPManager::GetMIDForMaterial(const UMaterialInterface* SourceMaterial) const
{
	if (!SourceMaterial)
	{
		if (const TObjectPtr<UMaterialInstanceDynamic>* FallbackMID = OutlineMIDs.Find(nullptr))
		{
			return FallbackMID->Get();
		}
		return nullptr;
	}

	if (const TObjectPtr<UMaterialInstanceDynamic>* FoundMID = OutlineMIDs.Find(SourceMaterial))
	{
		return FoundMID->Get();
	}

	return nullptr;
}
