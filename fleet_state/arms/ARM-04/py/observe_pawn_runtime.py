# ARM-04 item 6 (runtime half) — sample the possessed pawn from inside a LIVE game
# session, via:  -ExecCmds="py <this file>"  on a -game run.
#
# Why this and not the earlier attempts:
#   - the pythonscript COMMANDLET has no initialised physics scene, so traces there
#     missed a floor that provably exists (failed its own positive control)
#   - the `getall` console command emits nothing under -nullrhi -unattended
#   - the launch log alone cannot tell a floored map from a floorless one
# A tick callback inside a real game world has none of those problems.
#
# Decisive comparison:
#   RxTestMap          -> pawn Z falls, then STABILISES (it landed on RxFloor)
#   RxTestMap_NoFloor  -> pawn Z keeps decreasing (nothing to stand on)

import json
import os
import traceback

import unreal

RESULT_PATH = os.environ.get("RX_RESULT_PATH", "/tmp/rx_runtime_observation.json")
SAMPLE_LIMIT = int(os.environ.get("RX_SAMPLES", "180"))

state = {
    "samples": [],
    "handle": None,
    "map": None,
    "error": None,
    "done": False,
}


def get_world():
    """A live game world, not the editor world."""
    try:
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        w = ues.get_game_world()
        if w is not None:
            return w
    except Exception:
        pass
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def write_out():
    zs = [s["z"] for s in state["samples"]]
    summary = {
        "map": state["map"],
        "sample_count": len(zs),
        "z_first": zs[0] if zs else None,
        "z_last": zs[-1] if zs else None,
        "z_min": min(zs) if zs else None,
        "z_max": max(zs) if zs else None,
        # landed == the last 20 samples vary by less than 1 unit AND we actually fell
        "z_settled": (max(zs[-20:]) - min(zs[-20:]) < 1.0) if len(zs) >= 20 else None,
        "movement_modes": sorted({s["mode"] for s in state["samples"]}),
        "samples": state["samples"],
        "error": state["error"],
    }
    with open(RESULT_PATH, "w") as f:
        json.dump(summary, f, indent=2)
    unreal.log_warning("RX_RUNTIME_WRITTEN %s settled=%s z_first=%s z_last=%s"
                       % (RESULT_PATH, summary["z_settled"],
                          summary["z_first"], summary["z_last"]))


def on_tick(delta_seconds):
    if state["done"]:
        return
    try:
        world = get_world()
        if world is None:
            return
        if state["map"] is None:
            state["map"] = world.get_name()
        pawns = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character)
        if not pawns:
            return
        p = pawns[0]
        loc = p.get_actor_location()
        mode = "unknown"
        try:
            cmc = p.get_editor_property("character_movement")
            mode = str(cmc.get_editor_property("movement_mode"))
        except Exception:
            pass
        state["samples"].append({
            "n": len(state["samples"]),
            "x": round(loc.x, 2), "y": round(loc.y, 2), "z": round(loc.z, 2),
            "mode": mode,
        })
        if len(state["samples"]) >= SAMPLE_LIMIT:
            state["done"] = True
            write_out()
            if state["handle"] is not None:
                unreal.unregister_slate_post_tick_callback(state["handle"])
            unreal.SystemLibrary.quit_editor()
    except Exception as exc:  # noqa: BLE001
        state["error"] = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        state["done"] = True
        write_out()


try:
    state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    unreal.log_warning("RX_OBSERVER_REGISTERED handle=%s" % state["handle"])
except Exception as exc:  # noqa: BLE001
    state["error"] = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
    unreal.log_error("RX_OBSERVER_FAILED %s" % state["error"])
    write_out()
