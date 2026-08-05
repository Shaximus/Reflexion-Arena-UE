#!/bin/bash
# ARM-07 measurement window per HEAD07 ruling: non-blocking flock held through
# the entire window. Failure to acquire => report holder and stop, never wait.
LOCK=/run/lock/reflexion-qwen27-mtp.measurement.lock
W=/home/shax/.claude-squad/worktrees/arm/head07-inference-fable_18c8bea5f96e85d1
EV=$W/fleet_state/arms/ARM-07/evidence
REC=$W/fleet_state/arms/ARM-07/measurement_window_active.json
mkdir -p "$EV"

exec 9>"$LOCK"
if ! flock -n 9; then
  echo "LOCK_HELD_BY_ANOTHER — reporting holder, not waiting:"
  fuser -v "$LOCK" 2>&1
  exit 75
fi
echo "LOCK_ACQUIRED pid=$$"

HOOK=/home/shax/Projects/pentarchy/local-inference/deploy/python_startup/qwen_mtp_boot_tuning.py
cat > "$REC" <<EOF
{
  "owner_arm": "ARM-07",
  "operator_session": "claude-fable5 session_01MKVDwN2gqiQwUF3ascheib, lock holder pid $$",
  "purpose": "Candidate-5b qualification matrix per HEAD07 ruling ARM07-CKPT-20260805-0812",
  "exclusive_or_shared_load": "exclusive",
  "baseline_config_sha256": "$(sha256sum $HOOK | cut -d' ' -f1)",
  "candidate_config_sha256": "patched in-window; receipts in evidence 10_*",
  "model_and_build": "Qwen3.5-27B fp8_per_channel online, vLLM 0.24.0, YaRN 1048576",
  "main_pid_at_entry": "$(systemctl show qwen27-mtp -p MainPID --value)",
  "window_started_at": "$(date -Is)",
  "planned_release_at": "$(date -Is -d '+3 hours')",
  "expected_artifacts": "fleet_state/arms/ARM-07/evidence/10_*.json (bundle, matrices, semantic, needle, stability, verdict)",
  "rollback_command_or_profile": "cp evidence/10_hook_backup_prewindow.py \$HOOK && kill -9 MainPID (systemd auto-restarts)"
}
EOF
echo "WINDOW_RECORD_WRITTEN $REC"

python3 "$W/fleet_state/arms/ARM-07/tools/arm07_5b_qual.py" "$EV"
DRIVER_EXIT=$?
echo "DRIVER_EXIT=$DRIVER_EXIT"

mv "$REC" "$EV/10_window_record_closed.json"
echo "WINDOW_CLOSED $(date -Is)"
exit $DRIVER_EXIT
