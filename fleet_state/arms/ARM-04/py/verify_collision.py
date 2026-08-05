# ARM-04 item 6 (runtime half) — prove collision GEOMETRY actually blocks, by querying
# the physics scene directly rather than inferring it from component properties.
#
# Run against both maps:
#   RX_MAP=/Game/Maps/RxTestMap          -> floor trace MUST hit, wall trace MUST hit
#   RX_MAP=/Game/Maps/RxTestMap_NoFloor  -> floor trace MUST MISS   (negative control)
#
# A capsule sweep is used for the floor, sized to the real character capsule
# (r=35, half-height=90, both measured off BP_ThirdPersonCharacter), so this tests
# the thing the pawn will actually stand on, not an idealised ray.

import json
import os
import traceback

import unreal

MAP = os.environ.get("RX_MAP", "/Game/Maps/RxTestMap")
RESULT_PATH = os.environ.get("RX_RESULT_PATH", "/tmp/rx_collision_result.json")
CAPSULE_RADIUS = 35.0
CAPSULE_HALF_HEIGHT = 90.0

result = {"map": MAP, "traces": [], "ok": False, "error": None}


def record(name, hit, detail):
    result["traces"].append({"trace": name, "hit": bool(hit), "detail": str(detail)})
    unreal.log_warning("RX_TRACE %s hit=%s | %s" % (name, hit, detail))


try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    loaded = les.load_level(MAP)
    record("map_loaded", loaded, MAP)
    if not loaded:
        raise RuntimeError("could not load %s" % MAP)

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        raise RuntimeError("no editor world")

    # --- 1. straight down onto the floor -------------------------------------
    start = unreal.Vector(0, 0, 500)
    end = unreal.Vector(0, 0, -500)
    hit = unreal.SystemLibrary.line_trace_single(
        world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        False, [], unreal.DrawDebugTrace.NONE, True)
    if hit:
        loc = hit.to_tuple()[4] if hasattr(hit, "to_tuple") else None
        actor = hit.get_editor_property("hit_actor")
        imp = hit.get_editor_property("impact_point")
        record("floor_line_trace", True,
               "hit actor=%s impact_z=%.2f" % (actor.get_actor_label() if actor else "?", imp.z))
        result["floor_impact_z"] = imp.z
        result["floor_actor"] = actor.get_actor_label() if actor else None
    else:
        record("floor_line_trace", False, "no hit between z=500 and z=-500")

    # --- 2. capsule sweep, sized to the real character ------------------------
    swept = unreal.SystemLibrary.capsule_trace_single(
        world, unreal.Vector(0, 0, 500), unreal.Vector(0, 0, -500),
        CAPSULE_RADIUS, CAPSULE_HALF_HEIGHT,
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
        unreal.DrawDebugTrace.NONE, True)
    if swept:
        imp = swept.get_editor_property("impact_point")
        actor = swept.get_editor_property("hit_actor")
        record("floor_capsule_sweep", True,
               "r=%s hh=%s hit actor=%s impact_z=%.2f"
               % (CAPSULE_RADIUS, CAPSULE_HALF_HEIGHT,
                  actor.get_actor_label() if actor else "?", imp.z))
        result["capsule_rest_z"] = imp.z + CAPSULE_HALF_HEIGHT
    else:
        record("floor_capsule_sweep", False, "capsule swept through: nothing to stand on")

    # --- 3. lateral trace into the wall (collision is not just gravity) -------
    wall_hit = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(0, 0, 150), unreal.Vector(1000, 0, 150),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
        unreal.DrawDebugTrace.NONE, True)
    if wall_hit:
        actor = wall_hit.get_editor_property("hit_actor")
        imp = wall_hit.get_editor_property("impact_point")
        record("wall_line_trace", True,
               "hit actor=%s impact_x=%.2f" % (actor.get_actor_label() if actor else "?", imp.x))
        result["wall_impact_x"] = imp.x
    else:
        record("wall_line_trace", False, "no lateral blocker found")

    # Expectations differ per map; the caller asserts. We just report what happened.
    result["ok"] = True

except Exception as exc:  # noqa: BLE001
    result["error"] = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    unreal.log_error("RX_FAILED %s" % result["error"])

with open(RESULT_PATH, "w") as f:
    json.dump(result, f, indent=2)
unreal.log_warning("RX_RESULT_WRITTEN %s" % RESULT_PATH)
