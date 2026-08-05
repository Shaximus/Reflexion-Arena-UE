#!/usr/bin/env bash
# ARM-04 item 7 — repeatable launch / playtest command.
#
#   ./launch_playtest.sh                       headless, 60s, RxTestMap
#   ./launch_playtest.sh RxTestMap_NoFloor     headless, 60s, negative-control map
#   RX_SECONDS=20 ./launch_playtest.sh         shorter session
#   RX_HEADLESS=0 ./launch_playtest.sh         windowed, for a human to actually play
#
# MEASURED FACTS this encodes:
#   - The engine is reached by absolute path; EngineAssociation is empty and inert.
#   - -nullrhi is required for headless runs on this box (no GPU context in CI/agent use).
#   - The process does NOT self-exit: a headless -game session runs until killed, so
#     `timeout` is the terminator and exit 124 is the EXPECTED success code.
#   - Exit code is NOT a health signal for this project (see verify_playtest.py);
#     the log is. Do not gate on $? alone.
#
# No diagnostics are suppressed: no 2>/dev/null, no || true.

set -u
set -o pipefail

ENGINE_ROOT="${ENGINE_ROOT:-/home/shax/Projects/UnrealEngine/UE-5.8}"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
UPROJECT="$PROJECT_ROOT/ReflexionArena.uproject"
MAP="${1:-RxTestMap}"
SECONDS_TO_RUN="${RX_SECONDS:-60}"
HEADLESS="${RX_HEADLESS:-1}"
OUT_DIR="${OUT_DIR:-$PROJECT_ROOT/fleet_state/arms/ARM-04/evidence/item7}"
# RUN_ID keeps a re-run from silently overwriting evidence a manifest already hashed.
RUN_ID="${RUN_ID:-$(date +%Y%m%dT%H%M%S)}"

mkdir -p "$OUT_DIR"

EDITOR_CMD="$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
for required in "$EDITOR_CMD" "$UPROJECT" "$PROJECT_ROOT/Content/Maps/$MAP.umap"; do
  if [ ! -e "$required" ]; then
    echo "PREMISE MISSING: $required does not exist. Not proceeding." >&2
    exit 2
  fi
done

RHI_ARGS="-nullrhi"
if [ "$HEADLESS" = "0" ]; then
  RHI_ARGS=""
fi

STDOUT="$OUT_DIR/playtest_${MAP}_${RUN_ID}.stdout"
STDERR="$OUT_DIR/playtest_${MAP}_${RUN_ID}.stderr"

echo "ENGINE_ROOT=$ENGINE_ROOT"
echo "UPROJECT=$UPROJECT"
echo "MAP=/Game/Maps/$MAP  SECONDS=$SECONDS_TO_RUN  HEADLESS=$HEADLESS"

# shellcheck disable=SC2086
timeout --signal=TERM --kill-after=20 "$SECONDS_TO_RUN" \
  "$EDITOR_CMD" "$UPROJECT" \
  "/Game/Maps/$MAP" -game -unattended -nosplash $RHI_ARGS \
  -LogCmds="LogGameMode Verbose, LogCharacterMovement Verbose" \
  > "$STDOUT" 2> "$STDERR"
RUN_EXIT=$?

echo "RUN_EXIT=$RUN_EXIT   (124 = ran the full ${SECONDS_TO_RUN}s and was terminated: EXPECTED)"
echo "stdout: $STDOUT"
echo "stderr: $STDERR"
if [ -s "$STDERR" ]; then
  echo "--- stderr (non-empty) ---" >&2
  cat "$STDERR" >&2
fi

exit $RUN_EXIT
