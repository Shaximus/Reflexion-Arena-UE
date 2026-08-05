# ARM-04 item 3 — author one test map headlessly.
#
# Run via:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script="<this file>" \
#     -unattended -nopause -nosplash -nullrhi
#
# Writes /Game/Maps/RxTestMap and a machine-readable result JSON so the caller
# never has to infer success from log prose. Diagnostics are not suppressed.

import json
import os
import traceback

import unreal

MAP_PACKAGE = os.environ.get("RX_MAP_PACKAGE", "/Game/Maps/RxTestMap")
RESULT_PATH = os.environ.get("RX_RESULT_PATH", "/tmp/rx_make_test_map_result.json")

# RX_INCLUDE_FLOOR=0 builds the NEGATIVE CONTROL map: identical in every way except
# that the floor is omitted, so a working collision check MUST fail on it.
INCLUDE_FLOOR = os.environ.get("RX_INCLUDE_FLOOR", "1") != "0"

result = {
    "map_package": MAP_PACKAGE,
    "steps": [],
    "actors_spawned": [],
    "ok": False,
    "error": None,
}


def step(name, ok, detail=""):
    result["steps"].append({"step": name, "ok": bool(ok), "detail": str(detail)})
    unreal.log("RX_STEP %s ok=%s %s" % (name, ok, detail))


def write_result():
    with open(RESULT_PATH, "w") as f:
        json.dump(result, f, indent=2)
    unreal.log("RX_RESULT_WRITTEN %s" % RESULT_PATH)


try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    step("get_subsystems", les is not None and eas is not None,
         "LevelEditorSubsystem=%s EditorActorSubsystem=%s" % (les is not None, eas is not None))

    # A brand-new empty level. new_level saves the asset as part of the call.
    created = les.new_level(MAP_PACKAGE)
    step("new_level", created, MAP_PACKAGE)
    if not created:
        raise RuntimeError("new_level returned False for %s" % MAP_PACKAGE)

    cube = unreal.load_object(None, "/Engine/BasicShapes/Cube.Cube")
    step("load_cube", cube is not None, "/Engine/BasicShapes/Cube.Cube")
    if cube is None:
        raise RuntimeError("could not load /Engine/BasicShapes/Cube.Cube")

    def spawn(cls, loc, rot, label):
        a = eas.spawn_actor_from_class(cls, loc, rot)
        if a is None:
            raise RuntimeError("spawn_actor_from_class returned None for %s" % label)
        a.set_actor_label(label)
        result["actors_spawned"].append({"label": label, "class": a.get_class().get_name()})
        return a

    # Floor: 50m x 50m x 1m collision-bearing slab, top surface at Z=0.
    if INCLUDE_FLOOR:
        floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -50), unreal.Rotator(0, 0, 0), "RxFloor")
        fc = floor.static_mesh_component
        fc.set_static_mesh(cube)
        floor.set_actor_scale3d(unreal.Vector(50.0, 50.0, 1.0))
        fc.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        fc.set_mobility(unreal.ComponentMobility.STATIC)
        step("floor", True, "50x50x1 slab, top at Z=0, QueryAndPhysics")
    else:
        step("floor", True, "OMITTED - negative control map, nothing to stand on")

    # A raised block to prove collision blocks lateral movement, not just gravity.
    wall = spawn(unreal.StaticMeshActor, unreal.Vector(600, 0, 150), unreal.Rotator(0, 0, 0), "RxWall")
    wc = wall.static_mesh_component
    wc.set_static_mesh(cube)
    wall.set_actor_scale3d(unreal.Vector(1.0, 8.0, 3.0))
    wc.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    wc.set_mobility(unreal.ComponentMobility.STATIC)
    step("wall", True, "blocking wall at X=600")

    spawn(unreal.PlayerStart, unreal.Vector(0, 0, 120), unreal.Rotator(0, 0, 0), "RxPlayerStart")
    step("player_start", True, "at (0,0,120)")

    spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(-45, -45, 0), "RxSun")
    spawn(unreal.SkyLight, unreal.Vector(0, 0, 800), unreal.Rotator(0, 0, 0), "RxSkyLight")
    step("lights", True, "directional + sky")

    saved = les.save_current_level()
    step("save_current_level", saved, MAP_PACKAGE)
    if not saved:
        raise RuntimeError("save_current_level returned False")

    result["ok"] = True

except Exception as exc:  # noqa: BLE001 - we want every failure recorded, not swallowed
    result["error"] = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    unreal.log_error("RX_FAILED %s" % result["error"])

write_result()
