#include "Progression/AeyerjiProgressionLibrary.h"

#include "Engine/CurveTable.h"
#include "Systems/AeyerjiDifficultyTuning.h"

namespace
{
	const TCHAR* XPTablePath = TEXT("/Game/Player/XPCurveTable.XPCurveTable");
	const FName XPRowName(TEXT("XP_Needed"));
}

float UAeyerjiProgressionLibrary::GetXPRequiredForLevel(const int32 CharacterLevel)
{
	static TWeakObjectPtr<UCurveTable> CachedXPTable;
	UCurveTable* XPTable = CachedXPTable.Get();
	if (!XPTable)
	{
		XPTable = LoadObject<UCurveTable>(nullptr, XPTablePath);
		CachedXPTable = XPTable;
	}

	if (XPTable)
	{
		FCurveTableRowHandle RowHandle;
		RowHandle.CurveTable = XPTable;
		RowHandle.RowName = XPRowName;
		const float EvaluatedValue = RowHandle.Eval(
			static_cast<float>(UAeyerjiDifficultySettings::ClampGameplayLevel(CharacterLevel)),
			TEXT("UAeyerjiProgressionLibrary::GetXPRequiredForLevel"));
		if (EvaluatedValue > 0.f)
		{
			return FMath::Max(EvaluatedValue, 1.f);
		}
	}

	return 100.f;
}
