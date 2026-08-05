#!/bin/bash
# HEAD-07 controlled failure + recovery timer.
# Kills qwen27-mtp MainPID, then times systemd auto-recovery to API-ready and
# to first real completion. No stderr is suppressed; everything is captured.
LABEL="$1"
OUT="$2"
SCRATCH="/tmp/claude-1000/-home-shax--claude-squad-worktrees-arm-head07-inference-fable-18c8bea5f96e85d1/cd1d5ec6-5470-46d6-a897-53b73e9915b1/scratchpad"
{
echo "== controlled failure '$LABEL' =="
OLDPID=$(systemctl show qwen27-mtp -p MainPID --value)
echo "old_mainpid=$OLDPID"
T0=$(date +%s.%N)
kill -9 "$OLDPID"
echo "kill_exit=$? t_kill=$(date -Is) t0=$T0"

# Phase 1: API answers /v1/models
READY=""
for i in $(seq 1 300); do
  HTTP=$(curl -s -o "$SCRATCH/rec_models.json" -w '%{http_code}' --max-time 2 http://127.0.0.1:8010/v1/models 2>"$SCRATCH/rec_curl_err.txt")
  CURL_EXIT=$?
  if [ "$CURL_EXIT" -eq 0 ] && [ "$HTTP" = "200" ]; then READY=1; break; fi
  sleep 1
done
T1=$(date +%s.%N)
if [ -z "$READY" ]; then
  echo "FAIL: API never became ready within 300s; last curl exit=$CURL_EXIT http=$HTTP"
  cat "$SCRATCH/rec_curl_err.txt"
  exit 1
fi
echo "api_ready_after_s=$(echo "$T1 $T0" | awk '{printf "%.1f", $1-$2}')"

# Phase 2: first real completion
DONE=""
for i in $(seq 1 120); do
  HTTP=$(curl -s -o "$SCRATCH/rec_comp.json" -w '%{http_code}' --max-time 30 \
    -H 'Content-Type: application/json' \
    -d '{"model":"qwen27-mtp","messages":[{"role":"user","content":"Reply with exactly: RECOVERED"}],"max_tokens":16,"temperature":0,"chat_template_kwargs":{"enable_thinking":false}}' \
    http://127.0.0.1:8010/v1/chat/completions 2>"$SCRATCH/rec_curl2_err.txt")
  CURL_EXIT=$?
  if [ "$CURL_EXIT" -eq 0 ] && [ "$HTTP" = "200" ]; then DONE=1; break; fi
  sleep 1
done
T2=$(date +%s.%N)
if [ -z "$DONE" ]; then
  echo "FAIL: no successful completion within timeout; last curl exit=$CURL_EXIT http=$HTTP"
  cat "$SCRATCH/rec_curl2_err.txt"
  exit 1
fi
echo "first_completion_after_s=$(echo "$T2 $T0" | awk '{printf "%.1f", $1-$2}')"
echo "completion_body=$(cat "$SCRATCH/rec_comp.json")"
NEWPID=$(systemctl show qwen27-mtp -p MainPID --value)
echo "new_mainpid=$NEWPID (old was $OLDPID)"
echo "restart_counter=$(systemctl show qwen27-mtp -p NRestarts --value)"
} > "$OUT" 2>&1
EXIT=$?
cat "$OUT"
exit $EXIT
