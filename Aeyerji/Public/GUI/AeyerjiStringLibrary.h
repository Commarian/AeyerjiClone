// Copyright (c) 2025 Aeyerji.

#pragma once

#include "CoreMinimal.h"

/**
 * String table resolution helpers for the GlobalStringTable used across Aeyerji for player-facing text.
 *
 * Resolution path: /Game/Localization/GlobalStringTable.GlobalStringTable
 *
 * ## Usage
 *   FText Label = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("MyKey"));
 *
 * ## Adding strings
 * - Add a row to Source/Aeyerji/Data/Strings/GlobalStringTable.csv with a stable Key and SourceString.
 * - After editing the CSV, REIMPORT the String Table asset inside the Unreal Editor:
 *     Content/Localization/GlobalStringTable (or /Game/Localization/GlobalStringTable)
 *   This is required for the asset to pick up new keys at runtime and in editor.
 *
 * All user-facing keys (HUD labels, messages, upgrade names, error strings, combat feedback, etc.)
 * should be routed through this instead of raw FText::FromString or LOCTEXT/NSLOCTEXT literals.
 */
namespace AeyerjiStringLibrary
{
	/** Resolve text using a raw key literal. Returns empty text if key not found in table. */
	inline FText GetGlobalStringTableText(const TCHAR* Key)
	{
		static const FName GlobalStringTableId(TEXT("/Game/Localization/GlobalStringTable.GlobalStringTable"));
		if (!Key || Key[0] == TEXT('\0'))
		{
			return FText::GetEmpty();
		}
		return FText::FromStringTable(GlobalStringTableId, FTextKey(Key));
	}

	/** Resolve text using FName key. Returns empty for NAME_None. */
	inline FText GetGlobalStringTableText(const FName Key)
	{
		return Key.IsNone()
			? FText::GetEmpty()
			: GetGlobalStringTableText(*Key.ToString());
	}
}