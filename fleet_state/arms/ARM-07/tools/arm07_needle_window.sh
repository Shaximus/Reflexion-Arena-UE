#!/bin/bash
# ARM-07 corrected-needle follow-up window: completes the run-2 needle leg that
# the 800-token budget invalidated. Same lock discipline.
LOCK=/run/lock/reflexion-qwen27-mtp.measurement.lock
W=/home/shax/.claude-squad/worktrees/arm/head07-inference-fable_18c8bea5f96e85d1
EV=$W/fleet_state/arms/ARM-07/evidence
REC=$W/fleet_state/arms/ARM-07/measurement_window_active.json
exec 9>"$LOCK"
if ! flock -n 9; then
  echo "LOCK_HELD_BY_ANOTHER — reporting holder, not waiting:"; fuser -v "$LOCK" 2>&1; exit 75
fi
echo "LOCK_ACQUIRED pid=$$"
HOOK=/home/shax/Projects/pentarchy/local-inference/deploy/python_startup/qwen_mtp_boot_tuning.py
cat > "$REC" <<EOF
{
  "owner_arm": "ARM-07",
  "operator_session": "claude-fable5 session_01MKVDwN2gqiQwUF3ascheib, lock holder pid $$",
  "purpose": "Corrected beyond-native needle pair on 5b (run-2 leg was instrument-invalid: 800-token budget consumed by reasoning)",
  "exclusive_or_shared_load": "exclusive",
  "baseline_config_sha256": "$(sha256sum $HOOK | cut -d' ' -f1)",
  "candidate_config_sha256": "5b patch applied in-window; receipts 10_needlewin_*",
  "model_and_build": "Qwen3.5-27B fp8_per_channel, vLLM 0.24.0, YaRN 1048576",
  "main_pid_at_entry": "$(systemctl show qwen27-mtp -p MainPID --value)",
  "window_started_at": "$(date -Is)",
  "planned_release_at": "$(date -Is -d '+45 minutes')",
  "expected_artifacts": "10_5b_needle_corrected.json, 10_needlewin_restart.json, 10_needlewin_restore.json",
  "rollback_command_or_profile": "cp evidence/10_hook_backup_prewindow.py.needlewin \$HOOK && kill -9 MainPID"
}
EOF
echo "WINDOW_RECORD_WRITTEN"
python3 "$W/fleet_state/arms/ARM-07/tools/arm07_needle_corrected.py" "$EV"
E=$?
echo "DRIVER_EXIT=$E"
mv "$REC" "$EV/10_needlewin_record_closed.json"
echo "WINDOW_CLOSED $(date -Is)"
exit $E
