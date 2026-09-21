"""Run with Unreal Editor's Execute Python Script command; creates profile assets.

Rerunning updates only assets marked as owned by this importer. Existing map,
ZoneRun, templates and Blueprint classes are never modified by this script.
"""

import json
from pathlib import Path

import unreal


OWNER_KEY = "AeyerjiPacingProfileSource"
OWNER_VALUE = "RiftPacingProfiles.json:v1"

# Spatial tuning stays owned by the working Director template. Refresh it on
# every import, including existing assets that contain earlier reduced values.
SHARED_DISTANCE_PROPERTIES = (
    "rift_region_staging_distance",
    "rift_pressure_radius",
    "rift_enemy_wake_distance",
    "rift_enemy_sleep_distance",
    "rift_minimum_spawn_distance_from_players",
)


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def resolve_director_settings(profile, director_template):
    overrides = profile["director_settings"]
    conflicts = set(overrides).intersection(SHARED_DISTANCE_PROPERTIES)
    require(not conflicts,
            "Distance settings belong to director_template; remove profile overrides: "
            + ", ".join(sorted(conflicts)))
    settings = {
        prop: director_template.get_editor_property(prop)
        for prop in SHARED_DISTANCE_PROPERTIES
    }
    for prop in overrides:
        director_template.get_editor_property(prop)
    settings.update(overrides)
    return settings


def main():
    source = Path(__file__).resolve().with_name("RiftPacingProfiles.json")
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    require(data["schema_version"] == 1, "Unsupported pacing schema")
    library = unreal.EditorAssetLibrary
    director_template = require(library.load_asset(data["director_template"]), "Missing director template")
    group_template = require(library.load_asset(data["group_template"]), "Missing group template")
    classes = {
        name: require(library.load_blueprint_class(path), "Missing enemy Blueprint: " + path)
        for name, path in data["archetypes"].items()
    }

    # Preflight the complete input and all destinations before creating assets.
    jobs = []
    destinations = set()
    for profile in data["profiles"]:
        name = profile["name"]
        require(name.replace("_", "").isalnum(), "Invalid profile name")
        ordinary = []
        for role, weight in profile["ordinary_weights"].items():
            require(type(weight) is int and 0 < weight <= 32, "Invalid pool weight")
            ordinary.extend([classes[role]] * weight)
        require(ordinary, "Empty ordinary pool")
        elites = [classes[role] for role in profile["elite_pool"]]
        require(0 <= profile["elite_chance"] <= 1, "Invalid elite chance")
        require(elites or profile["elite_chance"] == 0, "Elite chance requires an elite pool")
        settings = resolve_director_settings(profile, director_template)
        for prop in ("enemy_types", "elite_enemy_types", "rift_elite_chance",
                     "rift_progress_points", "rift_elite_progress_points"):
            group_template.get_editor_property(prop)
        director_template.get_editor_property("spawn_groups")
        paths = [data["destination"] + "/SG_" + name,
                 data["destination"] + "/ED_" + name]
        for path, template in zip(paths, (group_template, director_template)):
            require(path not in destinations, "Duplicate destination: " + path)
            destinations.add(path)
            if library.does_asset_exist(path):
                asset = require(library.load_asset(path), "Cannot load " + path)
                require(library.get_metadata_tag(asset, OWNER_KEY) == OWNER_VALUE,
                        "Refusing to overwrite an unowned asset: " + path)
                require(asset.get_class() == template.get_class(), "Asset class mismatch: " + path)
        jobs.append((profile, ordinary, elites, paths, settings))

    def get_or_duplicate(path, template):
        if library.does_asset_exist(path):
            asset = require(library.load_asset(path), "Cannot load " + path)
            require(library.checkout_loaded_asset(asset), "Cannot check out " + path)
        else:
            asset = require(library.duplicate_asset(template, path), "Cannot duplicate to " + path)
        library.set_metadata_tag(asset, OWNER_KEY, OWNER_VALUE)
        return asset

    for profile, ordinary, elites, paths, settings in jobs:
        group = get_or_duplicate(paths[0], data["group_template"])
        group.set_editor_property("enemy_types", ordinary)
        group.set_editor_property("elite_enemy_types", elites)
        group.set_editor_property("rift_elite_chance", profile["elite_chance"])
        group.set_editor_property("rift_progress_points", 1)
        group.set_editor_property("rift_elite_progress_points", 5)
        require(library.save_loaded_asset(group), "Cannot save " + paths[0])

        director = get_or_duplicate(paths[1], data["director_template"])
        director.set_editor_property("spawn_groups", [group])
        for prop, value in settings.items():
            director.set_editor_property(prop, value)
            require(director.get_editor_property(prop) == value or
                    abs(director.get_editor_property(prop) - value) < 0.0001,
                    "Property verification failed: " + prop)
        require(library.save_loaded_asset(director), "Cannot save " + paths[1])
        unreal.log("[RiftPacingImport] Saved " + paths[0] + " and " + paths[1])
        unreal.log("[RiftPacingImport] " + profile["name"]
                   + " inherited distances (cm) from " + data["director_template"]
                   + ": " + ", ".join(prop + "=" + str(settings[prop])
                                       for prop in SHARED_DISTANCE_PROPERTIES))

    unreal.log("[RiftPacingImport] Complete. Assign the chosen ED asset on NeonMapZoneRun; "
               "update region group overrides as described in RiftPacingProfiles.md. "
               "No live map or ZoneRun settings were changed.")


if __name__ == "__main__":
    main()
