// ------------------------------ AeyerjiLog.h ------------------------------
#pragma once

#include "CoreMinimal.h"

/** Unified log channel for every Aeyerji trace. */
DECLARE_LOG_CATEGORY_EXTERN(LogAeyerji, Log, All);

/*--------------------------  INTERNAL HELPERS  ---------------------------*/
namespace Aeyerji::Detail
{
	/** Returns "SERVER" / "CLIENT" / "STANDALONE" for the given object. */
	inline const TCHAR* GetSide(const UObject* Obj)
	{
		// No object or no world -> assume stand-alone editor preview
		if (!Obj || !Obj->GetWorld())
		{
			return TEXT("STANDALONE");
		}

		switch (Obj->GetWorld()->GetNetMode())
		{
		case NM_DedicatedServer: return TEXT("DedSERVER");
		case NM_ListenServer:    return TEXT("LiSERVER");
		case NM_Client:          return TEXT("CLIENT");
		default:                 return TEXT("STANDALONE");
		}
	}

	/** Returns the Unreal class name of the object (or "Static" when null). */
	inline FString GetClass(const UObject* Obj)
	{
		return Obj ? Obj->GetClass()->GetName() : TEXT("Static");
	}
} // namespace Aeyerji::Detail

/*-----------------------------  PUBLIC MACROS  ----------------------------*/

/**
 * AJ_LOG(ObjPtr, TEXT("Fmt %d"), Args...)
 *
 * - `ObjPtr` can be `this`, any `UObject*`, a `UWorld*`, or `nullptr`.
 * - Prints:   [SERVER] MyCharacter: Your message
 *             [CLIENT] BP_ActionBar_C_0: Refreshed slot %d
 */
#define AJ_LOG(ObjPtr, Fmt, ...)                                                        \
do {                                                                                   \
	const UObject* AjLogObj = Cast<const UObject>(ObjPtr);                          \
	UE_LOG(LogAeyerji, Log, TEXT("[%s] %s: " Fmt),                                  \
		Aeyerji::Detail::GetSide(AjLogObj),                                         \
		*Aeyerji::Detail::GetClass(AjLogObj),                                       \
		##__VA_ARGS__);                                                             \
} while (0)

/** Convenience when you have no object context (compiles to STANDALONE). */
#define AJ_LOG_STATIC(Fmt, ...) AJ_LOG(nullptr, Fmt, ##__VA_ARGS__)
