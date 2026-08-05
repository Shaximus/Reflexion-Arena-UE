#!/usr/bin/env bash
# ARM-08 item 4 — mutation controls for the E11 admission gate.
#
# The BEFORE/AFTER pair proves the test goes 1 -> 0 when the gate is added, but a
# missing header failing to compile is a weak "fail". These controls prove the
# test fails for SEMANTIC reasons: each one breaks the shipped allow-list in a
# specific way and the test must catch it. A gate that has never been observed
# refusing is not a gate — and a test that has never been observed failing on a
# live build is not a test.
#
# Mutations are applied to COPIES under $MUT. Source/ is never modified.
# No stderr suppression anywhere: build logs are captured whole and printed.

set -u
EV=fleet_state/arms/ARM-08/evidence
MUT=/tmp/arm08_mutation_controls
mkdir -p "$MUT" "$EV"
OUT="$EV/04_mutation_controls.txt"
: > "$OUT"

FAILED_CONTROLS=0

run_mut () {
  local label="$1" sed_expr="$2" expect="$3" tag="$4"
  local d="$MUT/$tag"
  mkdir -p "$d"
  cp Source/ReflexionArena/Sim/RxCounterAuthority.h "$d/"
  sed "$sed_expr" Source/ReflexionArena/Sim/RxCounterAuthority.cpp > "$d/RxCounterAuthority.cpp"

  {
    echo "=== MUTATION [$tag]: $label ==="
    echo "--- diff vs shipped registry ---"
    diff Source/ReflexionArena/Sim/RxCounterAuthority.cpp "$d/RxCounterAuthority.cpp"
  } >> "$OUT" 2>&1
  # Guard: a sed expression that matches nothing produces an identical file and
  # the test would then "pass" while testing NOTHING. That is a broken control,
  # not a passing one — score it as a failure and say so.
  if diff -q Source/ReflexionArena/Sim/RxCounterAuthority.cpp "$d/RxCounterAuthority.cpp" > /dev/null; then
    {
      echo "*** CONTROL INVALID — the mutation did not apply (files identical)."
      echo "*** sed expression matched nothing: $sed_expr"
      echo
    } >> "$OUT"
    FAILED_CONTROLS=$((FAILED_CONTROLS + 1))
    return
  fi

  g++ -std=c++17 -Wall -Wextra -I"$d" -o "$d/t" \
      fleet_state/arms/ARM-08/tests/test_e11_admission.cpp "$d/RxCounterAuthority.cpp" \
      > "$d/build.log" 2>&1
  local brc=$?
  if [ $brc -ne 0 ]; then
    { echo "BUILD_EXIT=$brc — mutation did not compile; control INVALID"; cat "$d/build.log"; } >> "$OUT" 2>&1
    FAILED_CONTROLS=$((FAILED_CONTROLS + 1))
    return
  fi

  "$d/t" >> "$OUT" 2>&1
  local rrc=$?
  {
    echo "MUTATED_RUN_EXIT=$rrc  (expected $expect)"
    if [ "$rrc" = "$expect" ]; then
      echo "MUTATION CONTROL OK — the test detected the break"
    else
      echo "*** MUTATION CONTROL FAILED — the test did NOT detect the break ***"
    fi
    echo
  } >> "$OUT"
  [ "$rrc" = "$expect" ] || FAILED_CONTROLS=$((FAILED_CONTROLS + 1))
}

# A — reopen THE EXPLOIT §4.0 names: boss_stability becomes A2-writable.
run_mut "boss_stability -> A2-writable (reopens the hole)" \
        's/{ "boss_stability",          false,/{ "boss_stability",          true ,/' 1 A

# B — break shipped canon: boss_release_delay stops being writable.
run_mut "boss_release_delay -> NOT writable (breaks the strike-interrupt window)" \
        's/{ "boss_release_delay",      true,/{ "boss_release_delay",      false,/' 1 B

# C — allow-list with no scope at all: every counter writable.
run_mut "every counter A2-writable (allow-list that allows everything)" \
        's/,          false,/,          true ,/; s/,       false,/,       true ,/; s/,  *false,/, true ,/' 1 C

# D — deny-by-default removed: unknown counter ids silently admitted.
run_mut "unknown counter_id admitted (deny-by-default removed)" \
        's/return EAdmission::RejectUnknownCounter;/return EAdmission::Admit;/' 1 D

{
  echo "===================================================================="
  echo "MUTATION CONTROLS FAILED: $FAILED_CONTROLS of 4"
  [ "$FAILED_CONTROLS" -eq 0 ] && echo "ALL 4 CONTROLS OK — the test is falsifiable on a live build" \
                               || echo "*** the test is not reliably falsifiable ***"
} >> "$OUT"

cat "$OUT"
exit $([ "$FAILED_CONTROLS" -eq 0 ] && echo 0 || echo 1)
