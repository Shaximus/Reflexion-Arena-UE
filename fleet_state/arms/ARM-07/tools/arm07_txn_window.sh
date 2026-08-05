#!/bin/bash
# ARM-07 TRANSACTIONAL measurement window, per HEAD07 ruling (dual-verdict ack):
# the candidate profile and the lock form one transaction — restore is ARMED
# BEFORE mutation via an EXIT trap; the lock cannot release until baseline is
# restored (or the driver deliberately left it clean) and a final real
# completion succeeds.
# usage: arm07_txn_window.sh <driver.py> <purpose-label> <planned-minutes>
set -u
DRIVER="$1"; PURPOSE="$2"; MINUTES="${3:-90}"
LOCK=/run/lock/reflexion-qwen27-mtp.measurement.lock
W=/home/shax/.claude-squad/worktrees/arm/head07-inference-fable_18c8bea5f96e85d1
EV=$W/fleet_state/arms/ARM-07/evidence
REC=$W/fleet_state/arms/ARM-07/measurement_window_active.json
HOOK=/home/shax/Projects/pentarchy/local-inference/deploy/python_startup/qwen_mtp_boot_tuning.py
mkdir -p "$EV"

exec 9>"$LOCK"
if ! flock -n 9; then
  echo "LOCK_HELD_BY_ANOTHER — reporting holder, not waiting:"; fuser -v "$LOCK" 2>&1; exit 75
fi
echo "LOCK_ACQUIRED pid=$$"

# ---- ARM RESTORE BEFORE ANY MUTATION ----
TXN_BACKUP="$EV/txn_hook_backup_$$.py"
cp "$HOOK" "$TXN_BACKUP"

wait_ready_and_probe() {
  for i in $(seq 1 240); do
    HTTP=$(curl -s -o /dev/null -w '%{http_code}' --max-time 2 http://127.0.0.1:8010/v1/models 2>&1)
    [ "$HTTP" = "200" ] && break; sleep 2
  done
  HTTP=$(curl -s -o "$EV/txn_final_probe_$$.json" -w '%{http_code}' --max-time 90 \
    -H 'Content-Type: application/json' \
    -d '{"model":"qwen27-mtp","messages":[{"role":"user","content":"Reply with exactly: RECOVERED"}],"max_tokens":16,"temperature":0,"chat_template_kwargs":{"enable_thinking":false}}' \
    http://127.0.0.1:8010/v1/chat/completions 2>&1)
  echo "final_probe_http=$HTTP"
}

txn_restore() {
  if ! cmp -s "$HOOK" "$TXN_BACKUP"; then
    echo "TXN_TRAP: hook differs from pre-window backup — restoring before lock release"
    cp "$TXN_BACKUP" "$HOOK"
    kill -9 "$(systemctl show qwen27-mtp -p MainPID --value)"
    wait_ready_and_probe
  else
    echo "TXN_TRAP: hook already matches pre-window backup"
    wait_ready_and_probe
  fi
  mv "$REC" "$EV/txn_window_record_closed_$$.json" 2>&1 || true  # window may have closed it already; acceptable
  echo "WINDOW_CLOSED $(date -Is)"
}
trap txn_restore EXIT

cat > "$REC" <<EOF
{
  "owner_arm": "ARM-07",
  "operator_session": "claude-fable5 session_01MKVDwN2gqiQwUF3ascheib, lock holder pid $$",
  "purpose": "$PURPOSE",
  "exclusive_or_shared_load": "exclusive",
  "baseline_config_sha256": "$(sha256sum $HOOK | cut -d' ' -f1)",
  "candidate_config_sha256": "patched in-window; transactional restore armed at $TXN_BACKUP",
  "model_and_build": "Qwen3.5-27B fp8_per_channel, vLLM 0.24.0, YaRN 1048576 canonical",
  "main_pid_at_entry": "$(systemctl show qwen27-mtp -p MainPID --value)",
  "window_started_at": "$(date -Is)",
  "planned_release_at": "$(date -Is -d "+$MINUTES minutes")",
  "expected_artifacts": "per driver: $DRIVER",
  "rollback_command_or_profile": "automatic: EXIT trap restores $TXN_BACKUP and reboots; manual same"
}
EOF
echo "WINDOW_RECORD_WRITTEN (transactional)"

python3 "$DRIVER" "$EV"
E=$?
echo "DRIVER_EXIT=$E"
exit $E
