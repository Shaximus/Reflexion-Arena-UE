#!/usr/bin/env bash
# ARM-04 — produce the Kestrel-required checkpoint evidence in ONE run, from scratch,
# so nothing in the report is assembled from stale artifacts.
#
# Emits, with real exit statuses and full logs:
#   1. measured engine installation + version   (static AND from the running process)
#   2. actual project path, branch, HEAD
#   3. repeatable build command  -> exit status
#   4. repeatable open/launch command -> exit status
#   5. log-derived verdict + its negative control
#
# No diagnostics suppressed: no 2>/dev/null, no || true.

set -u
set -o pipefail

ENGINE_ROOT="${ENGINE_ROOT:-/home/shax/Projects/UnrealEngine/UE-5.8}"
ARM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$ARM_DIR/../../.." && pwd)"
OUT="$ARM_DIR/evidence/checkpoint"
# Pin a stable RUN_ID so the checkpoint's own evidence filenames are predictable,
# while still not colliding with ad-hoc runs (which get a timestamp).
export RUN_ID="${RUN_ID:-checkpoint}"
mkdir -p "$OUT"

echo "############ 1. ENGINE — MEASURED ############"
echo "--- Engine/Build/Build.version ---"
cat "$ENGINE_ROOT/Engine/Build/Build.version"; echo "cat_exit=$?"
echo "--- Build.sh present and executable? ---"
ls -l "$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"; echo "ls_exit=$?"
test -x "$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"; echo "is_executable_exit=$? (0 == yes)"
echo "--- engine git identity ---"
git -C "$ENGINE_ROOT" rev-parse HEAD; echo "exit=$?"
git -C "$ENGINE_ROOT" branch --show-current; echo "exit=$?"

echo "############ 2. PROJECT — MEASURED ############"
echo "PROJECT_ROOT=$PROJECT_ROOT"
ls -l "$PROJECT_ROOT/ReflexionArena.uproject"; echo "ls_exit=$?"
sha256sum "$PROJECT_ROOT/ReflexionArena.uproject"; echo "exit=$?"
echo "--- EngineAssociation (measured, not assumed) ---"
python3 -c "import json;print(repr(json.load(open('$PROJECT_ROOT/ReflexionArena.uproject'))['EngineAssociation']))"
echo "exit=$?"
git -C "$PROJECT_ROOT" branch --show-current; echo "exit=$?"
git -C "$PROJECT_ROOT" rev-parse HEAD; echo "exit=$?"

echo "############ 3. BUILD ############"
"$ARM_DIR/build_editor.sh" ReflexionArenaEditor Development
BUILD_EXIT=$?
echo "BUILD_EXIT=$BUILD_EXIT"

echo "############ 4. OPEN / LAUNCH FIRST TEST MAP ############"
RX_SECONDS="${RX_SECONDS:-45}" "$ARM_DIR/launch_playtest.sh" RxTestMap
LAUNCH_EXIT=$?
echo "LAUNCH_EXIT=$LAUNCH_EXIT   (124 == ran the full session then was terminated: EXPECTED)"

echo "############ 5. LOG VERDICT + NEGATIVE CONTROL ############"
PLAYLOG="$ARM_DIR/evidence/item7/playtest_RxTestMap_${RUN_ID}.stdout"
python3 "$ARM_DIR/verify_playtest.py" "$PLAYLOG" --expect-map RxTestMap \
        --json "$OUT/verdict.json"
VERDICT_EXIT=$?
echo "VERDICT_EXIT=$VERDICT_EXIT (0 == all required signals present)"

python3 "$ARM_DIR/verify_playtest.py" "$PLAYLOG" --expect-map MapThatWasNeverLoaded \
        > "$OUT/negative_control.txt" 2>&1
NEGCTL_EXIT=$?
echo "NEGCTL_EXIT=$NEGCTL_EXIT (MUST be 1 — proves the verdict can fail)"

echo "############ 6. RUNTIME MOVEMENT + COLLISION (in-process observer) ############"
rm -f "$OUT/obs_floor.json" "$OUT/obs_nofloor.json"
"$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT_ROOT/ReflexionArena.uproject" \
  /Game/Maps/RxTestMap -game -unattended -nosplash -nullrhi \
  -RxObserve="$OUT/obs_floor.json" -RxObserveSeconds=16 \
  > "$OUT/obs_floor.stdout" 2> "$OUT/obs_floor.stderr"
echo "OBS_FLOOR_EXIT=$?"
"$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT_ROOT/ReflexionArena.uproject" \
  /Game/Maps/RxTestMap_NoFloor -game -unattended -nosplash -nullrhi \
  -RxObserve="$OUT/obs_nofloor.json" -RxObserveSeconds=16 \
  > "$OUT/obs_nofloor.stdout" 2> "$OUT/obs_nofloor.stderr"
echo "OBS_NOFLOOR_EXIT=$?"
python3 - "$OUT/obs_floor.json" "$OUT/obs_nofloor.json" <<'PYGATE'
import json, sys
floor = json.load(open(sys.argv[1]))
ctrl  = json.load(open(sys.argv[2]))
checks = {
  "floor: pawn landed":            floor["ever_on_ground"] is True,
  "floor: mode Walking":           floor["last_movement_mode"] == "Walking",
  "floor: input moved the pawn":   floor["travelled_x"] > 100.0,
  "floor: wall stopped it at 515": abs(floor["travelled_x"] - 515.0) < 1.0,
  "control: never landed":         ctrl["ever_on_ground"] is False,
  "control: mode Falling":         ctrl["last_movement_mode"] == "Falling",
  "control: fell far":             ctrl["min_z"] < -1000.0,
}
for k,v in checks.items():
    print(("  PASS " if v else "  FAIL ")+k)
print("RUNTIME_GATE_OK=%s" % all(checks.values()))
sys.exit(0 if all(checks.values()) else 1)
PYGATE
RUNTIME_EXIT=$?
echo "RUNTIME_EXIT=$RUNTIME_EXIT (0 == movement AND collision proven, control fired)"

echo "############ 7. CAMERA OPERATES (Checkpoint A criterion) ############"
# Structural inspection only proved a CameraComponent exists. This proves the player's
# view target IS the pawn and the camera tracks it at the spring-arm length.
CAMDIR="$ARM_DIR/evidence/item5_runtime"
mkdir -p "$CAMDIR"
"$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT_ROOT/ReflexionArena.uproject" \
  /Game/Maps/RxTestMap -game -unattended -nosplash -nullrhi \
  -RxObserve="$CAMDIR/cam_floor.json" -RxObserveSeconds=16 \
  > "$CAMDIR/cam_floor.stdout" 2> "$CAMDIR/cam_floor.stderr"
echo "CAM_FLOOR_EXIT=$?"
"$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT_ROOT/ReflexionArena.uproject" \
  /Game/Maps/RxTestMap_NoFloor -game -unattended -nosplash -nullrhi \
  -RxObserve="$CAMDIR/cam_nofloor.json" -RxObserveSeconds=16 \
  > "$CAMDIR/cam_nofloor.stdout" 2> "$CAMDIR/cam_nofloor.stderr"
echo "CAM_NOFLOOR_EXIT=$?"
python3 "$ARM_DIR/verify_camera.py" "$CAMDIR/cam_floor.json" "$CAMDIR/cam_nofloor.json"
CAMERA_EXIT=$?
echo "CAMERA_EXIT=$CAMERA_EXIT (0 == camera operates: view target is the pawn and it tracks)"

echo "############ RUNTIME ENGINE VERSION (from the process, not a file) ############"
grep -m1 'LogInit: Engine Version:' "$PLAYLOG"; echo "grep_exit=$?"

echo "############ SUMMARY ############"
echo "BUILD_EXIT=$BUILD_EXIT LAUNCH_EXIT=$LAUNCH_EXIT VERDICT_EXIT=$VERDICT_EXIT NEGCTL_EXIT=$NEGCTL_EXIT RUNTIME_EXIT=$RUNTIME_EXIT CAMERA_EXIT=$CAMERA_EXIT"
if [ "$BUILD_EXIT" -eq 0 ] && [ "$LAUNCH_EXIT" -eq 124 ] \
   && [ "$VERDICT_EXIT" -eq 0 ] && [ "$NEGCTL_EXIT" -eq 1 ] && [ "$RUNTIME_EXIT" -eq 0 ] \
   && [ "$CAMERA_EXIT" -eq 0 ]; then
  echo "CHECKPOINT=UE_PROJECT_OPENED_OR_BUILT"
  exit 0
fi
echo "CHECKPOINT=NOT_SATISFIED"
exit 1
