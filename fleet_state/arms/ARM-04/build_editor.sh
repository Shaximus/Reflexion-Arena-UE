#!/usr/bin/env bash
# ARM-04 item 2 — repeatable build command for ReflexionArena (UE 5.8.1 source build).
#
# MEASURED FACTS this script encodes:
#   - EngineAssociation in ReflexionArena.uproject is "" (empty) and is NOT consulted
#     on this path. Proven: a bogus GUID association still built successfully.
#     The engine is located solely by the absolute path to Build.sh plus -Project=.
#   - dotnet is NOT on PATH; Build.sh sources SetupEnvironment.sh and uses the
#     engine-bundled DotNet SDK 10.0. Do not pre-install dotnet to "fix" this.
#
# Diagnostics are never suppressed: no 2>/dev/null, no || true.
# Exit code is the producer's exit code, propagated verbatim.

set -u
set -o pipefail

ENGINE_ROOT="${ENGINE_ROOT:-/home/shax/Projects/UnrealEngine/UE-5.8}"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)}"
UPROJECT="$PROJECT_ROOT/ReflexionArena.uproject"
TARGET="${1:-ReflexionArenaEditor}"
CONFIG="${2:-Development}"
OUT_DIR="${OUT_DIR:-$PROJECT_ROOT/fleet_state/arms/ARM-04/evidence/item2}"
# RUN_ID keeps a re-run from silently overwriting evidence a manifest already hashed.
RUN_ID="${RUN_ID:-$(date +%Y%m%dT%H%M%S)}"

mkdir -p "$OUT_DIR"

# Fail loudly on a missing premise rather than working around it.
for required in "$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" "$UPROJECT"; do
  if [ ! -e "$required" ]; then
    echo "PREMISE MISSING: $required does not exist. Not proceeding." >&2
    exit 2
  fi
done

echo "ENGINE_ROOT=$ENGINE_ROOT"
echo "UPROJECT=$UPROJECT"
echo "TARGET=$TARGET  CONFIG=$CONFIG  PLATFORM=Linux"

"$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
  "$TARGET" Linux "$CONFIG" \
  -Project="$UPROJECT" \
  -WaitMutex \
  > "$OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stdout" \
  2> "$OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stderr"
BUILD_EXIT=$?

echo "BUILD_EXIT=$BUILD_EXIT"
echo "stdout: $OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stdout"
echo "stderr: $OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stderr"
tail -6 "$OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stdout"
if [ -s "$OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stderr" ]; then
  echo "--- stderr (non-empty) ---" >&2
  cat "$OUT_DIR/build_${TARGET}_${CONFIG}_${RUN_ID}.stderr" >&2
fi

exit $BUILD_EXIT
