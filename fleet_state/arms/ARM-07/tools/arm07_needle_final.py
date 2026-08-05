#!/usr/bin/env python3
"""ARM-07 final needle attempt on 5b: run the foundry's PROVEN beyond-native
probe verbatim (real-text haystack; passed on baseline). My word-salad variant
failed three times on instrument grounds — even the trivial negative control
consumed the whole thinking budget. Runs inside the flock window wrapper."""
import importlib.util, json, os, shutil, subprocess, sys, time

EV = sys.argv[1]
sys.argv = [sys.argv[0], EV]  # lib reads EV from argv[1] at import time
spec = importlib.util.spec_from_file_location(
    "lib", os.path.join(os.path.dirname(__file__), "arm07_needle_corrected.py"))
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)

PROBE = ("/home/shax/Projects/core-tech/PentaCLI/.claude/worktrees/arm-00-fleet-foundry/"
         "fleet_state/foundry/evidence/head07/yarn-1m/probe_beyond_native.py")

def main():
    shutil.copy(lib.HOOK, lib.HOOK_BACKUP + ".final")
    lib.patch_hook_5b()
    rec, ok = lib.kill_and_wait("needlefinal-to-5b")
    lib.receipt("10_needlefinal_restart.json", rec)
    cfg = lib.effective_config()
    lib.receipt("10_needlefinal_5b_config.json", cfg)
    result = {"probe": PROBE, "started": time.strftime("%FT%T%z")}
    try:
        if not ok or not cfg.get("prefix_caching") or cfg.get("max_seq_len") != 1048576:
            result["FAIL"] = "5b did not come up with invariants; probe not run"
            return 1
        p = subprocess.run([sys.executable, PROBE, "http://127.0.0.1:8010", "qwen27-mtp"],
                           capture_output=True, text=True, timeout=3600)
        result.update({"exit_code": p.returncode,
                       "stdout": p.stdout[-4000:], "stderr": p.stderr[-2000:]})
        return 0
    finally:
        result["finished"] = time.strftime("%FT%T%z")
        lib.receipt("10_5b_needle_foundry_probe.json", result)
        shutil.copy(lib.HOOK_BACKUP + ".final", lib.HOOK)
        rrec, rok = lib.kill_and_wait("needlefinal-restore")
        lib.receipt("10_needlefinal_restore.json",
                    {"restart": rrec, "ok": rok, "cfg": lib.effective_config()})
        print("PROBE exit:", result.get("exit_code"), "restore_ok:", rok, flush=True)

if __name__ == "__main__":
    sys.exit(main() or 0)
