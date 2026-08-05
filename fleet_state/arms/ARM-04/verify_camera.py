#!/usr/bin/env python3
"""ARM-04 Checkpoint A criterion "camera operates" — assert it from measured runtime data.

Usage: verify_camera.py <cam_floor.json> <cam_nofloor.json>

The claim being tested is NOT "a CameraComponent exists on the Blueprint" (that was
already known from structural inspection). It is the stronger claim: the player's view
target IS the pawn, and the camera actually TRACKS it at the configured spring-arm
length while the pawn moves.

The no-floor control carries the decisive tracking evidence: the pawn falls ~55,800 uu,
and if the camera were static the camera-to-pawn distance would grow to ~55,000. It
stays at the spring-arm length instead.

Exit 0 = all checks pass, 1 = at least one failed, 2 = usage/IO error.
"""

import json
import sys

SPRING_ARM_LENGTH = 400.0   # measured off BP_ThirdPersonCharacter (target_arm_length)
TOLERANCE = 1.0


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        floor = json.load(open(sys.argv[1]))
        ctrl = json.load(open(sys.argv[2]))
    except (OSError, ValueError) as exc:
        print("IO/parse error: %s" % exc, file=sys.stderr)
        return 2

    floor_pawn_x = floor["max_forward_x"]
    floor_trail = floor_pawn_x - floor["camera_final_x"]
    ctrl_fall = abs(ctrl["min_z"] - ctrl["spawn_z"])

    checks = {
        "floor: view target is the pawn":
            floor["view_target_is_pawn"] is True,
        "floor: camera exists at runtime":
            floor["camera_found"] is True,
        "floor: camera trails pawn by the spring-arm length (400)":
            abs(floor_trail - SPRING_ARM_LENGTH) < TOLERANCE,
        "floor: camera-pawn distance never exceeds the arm length":
            floor["camera_max_distance"] < SPRING_ARM_LENGTH + TOLERANCE,
        "control: view target is the pawn":
            ctrl["view_target_is_pawn"] is True,
        # The decisive one: the pawn fell tens of thousands of units. A static camera
        # would show a distance of that same order. It stayed at the arm length.
        "control: pawn fell far (>10000 uu)":
            ctrl_fall > 10000.0,
        "control: camera followed the fall (distance stayed ~400, not ~55000)":
            ctrl["camera_max_distance"] < SPRING_ARM_LENGTH + TOLERANCE,
    }

    for name, ok in checks.items():
        print("  %s %s" % ("PASS" if ok else "FAIL", name))
    print("  measured floor_pawn_x=%.3f camera_final_x=%.3f trail=%.3f"
          % (floor_pawn_x, floor["camera_final_x"], floor_trail))
    print("  measured control_fall=%.1f uu camera_max_distance=%.3f"
          % (ctrl_fall, ctrl["camera_max_distance"]))
    print("CAMERA_GATE_OK=%s" % all(checks.values()))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
