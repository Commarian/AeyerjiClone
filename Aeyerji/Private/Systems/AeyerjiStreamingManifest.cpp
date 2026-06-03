#include "Systems/AeyerjiStreamingManifest.h"

bool UAeyerjiStreamingManifest::GetZoneDefinition(const FName ZoneId, FZoneDef& OutZoneDefinition) const
{
	if (ZoneId.IsNone())
	{
		return false;
	}

	for (const FZoneDef& Zone : Zones)
	{
		if (Zone.ZoneId == ZoneId)
		{
			OutZoneDefinition = Zone;
			return true;
		}
	}

	return false;
}

bool UAeyerjiStreamingManifest::GetGameplayMapDefinition(const FName MapId, FAeyerjiGameplayMapDef& OutMapDefinition) const
{
	if (MapId.IsNone())
	{
		return false;
	}

	for (const FAeyerjiGameplayMapDef& MapDef : GameplayMaps)
	{
		if (MapDef.MapId == MapId)
		{
			OutMapDefinition = MapDef;
			return true;
		}
	}

	return false;
}
