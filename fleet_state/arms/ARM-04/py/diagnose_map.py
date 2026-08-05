# ARM-04 — diagnostic: is the geometry actually IN the map, and does the editor world
# in a commandlet even have a physics scene to trace against?
#
# Written because verify_collision.py reported "no hit" on a map that definitely has a
# floor. A detector that fails its positive control proves nothing; this finds out why.

import json
import os
import traceback

import unreal

MAP = os.environ.get("RX_MAP", "/Game/Maps/RxTestMap")
RESULT_PATH = os.environ.get("RX_RESULT_PATH", "/tmp/rx_diag.json")

out = {"map": MAP, "actors": [], "notes": [], "error": None}

try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    loaded = les.load_level(MAP)
    out["notes"].append("load_level=%s" % loaded)

    for a in eas.get_all_level_actors():
        entry = {
            "label": a.get_actor_label(),
            "class": a.get_class().get_name(),
            "location": [a.get_actor_location().x, a.get_actor_location().y,
                         a.get_actor_location().z],
            "scale": [a.get_actor_scale3d().x, a.get_actor_scale3d().y,
                      a.get_actor_scale3d().z],
        }
        prims = a.get_components_by_class(unreal.PrimitiveComponent)
        comps = []
        for p in prims:
            comps.append({
                "class": p.get_class().get_name(),
                "collision_enabled": str(p.get_collision_enabled()),
                "profile": str(p.get_collision_profile_name()),
                "mesh": (p.get_editor_property("static_mesh").get_name()
                         if isinstance(p, unreal.StaticMeshComponent)
                         and p.get_editor_property("static_mesh") else None),
            })
        entry["primitives"] = comps
        out["actors"].append(entry)

    world = unreal.EditorLevelLibrary.get_editor_world()
    out["notes"].append("world=%s" % world)
    # NOTE: World has no Python-exposed 'has_begun_play'. Recorded as a measured
    # limitation rather than retried; see MANIFEST claim C6-4.

except Exception as exc:  # noqa: BLE001
    out["error"] = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    unreal.log_error("RX_FAILED %s" % out["error"])

with open(RESULT_PATH, "w") as f:
    json.dump(out, f, indent=2)
unreal.log_warning("RX_DIAG_WRITTEN %s" % RESULT_PATH)
