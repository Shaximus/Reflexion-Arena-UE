"""Narrow startup override for the root-owned qwen27-mtp system service.

The service unit still passes native MTP K=5 and max-num-seqs=5. This module
is loaded by a venv ``.pth`` file and rewrites that one exact production
invocation to the measured K=6 winner with a four-active-sequence safety cap.
The qualified online-FP8 profile is enabled by an explicit runtime marker.
It does nothing for other models, ports, modules, or already-explicit values.

Remove ``qwen_mtp_boot_tuning.pth`` from the venv site-packages directory to
roll back instantly to the unit's K=5 argument.
"""

from __future__ import annotations

import json
import os
import sys
from collections.abc import MutableSequence, Sequence
from pathlib import Path


TARGET_MODULE = "vllm.entrypoints.openai.api_server"
TARGET_MODEL = "/home/shax/mnt_data/models/Qwen/Qwen3.5-27B"
TARGET_PORT = "8010"
TARGET_SERVED_MODEL = "qwen27-mtp"
CONTROL_K = 5
TUNED_K = 6
CONTROL_MAX_NUM_SEQS = "5"
SAFE_MAX_NUM_SEQS = "4"
CONTROL_KV_OFFLOAD_BACKEND = "native"
CONTROL_KV_OFFLOAD_SIZE = "64"
ONLINE_FP8_ENABLED_MARKER = Path(
    "/home/shax/Projects/pentarchy/local-inference/"
    "runtime/qwen27-mtp-online-fp8.enabled"
)
ONLINE_FP8_METHOD = "fp8_per_channel"
ONLINE_FP8_QUANTIZATION_CONFIG: dict[str, object] = {}
FLASHINFER_CTK_COMPAT_FLAG = "-DCCCL_DISABLE_CTK_COMPATIBILITY_CHECK"
CONTROL_ATTENTION_BACKEND = "FLASHINFER"
CANDIDATE_ATTENTION_BACKEND = "TRITON_ATTN"


def _argument_value(argv: Sequence[str], option: str) -> str | None:
    try:
        index = argv.index(option)
    except ValueError:
        return None
    if index + 1 >= len(argv):
        return None
    return argv[index + 1]


def _is_target_invocation(argv: Sequence[str]) -> bool:
    if TARGET_MODULE not in argv:
        return False
    if _argument_value(argv, "--model") != TARGET_MODEL:
        return False
    if _argument_value(argv, "--port") != TARGET_PORT:
        return False
    if _argument_value(argv, "--served-model-name") != TARGET_SERVED_MODEL:
        return False
    return True


def _rewrite_control_width(argv: MutableSequence[str]) -> bool:
    try:
        option_index = argv.index("--speculative-config")
        value_index = option_index + 1
        config = json.loads(argv[value_index])
    except (ValueError, IndexError, json.JSONDecodeError, TypeError):
        return False

    if config.get("method") != "mtp":
        return False
    if config.get("num_speculative_tokens") != CONTROL_K:
        return False

    config["num_speculative_tokens"] = TUNED_K
    argv[value_index] = json.dumps(config, separators=(",", ":"))
    return True


def cap_unsafe_concurrency(argv: MutableSequence[str]) -> bool:
    """Cap the exact production engine below its fatal five-sequence path."""
    try:
        option_index = argv.index("--max-num-seqs")
        value_index = option_index + 1
        current = argv[value_index]
    except (ValueError, IndexError):
        return False

    if current != CONTROL_MAX_NUM_SEQS:
        return False
    argv[value_index] = SAFE_MAX_NUM_SEQS
    return True


def use_triton_for_target_and_draft(argv: MutableSequence[str]) -> bool:
    """Use one full-graph-capable attention backend on both spec-decode sides."""
    changed = False
    try:
        backend_index = argv.index("--attention-backend") + 1
        if argv[backend_index] == CONTROL_ATTENTION_BACKEND:
            argv[backend_index] = CANDIDATE_ATTENTION_BACKEND
            changed = True
    except (ValueError, IndexError):
        return False

    try:
        spec_index = argv.index("--speculative-config") + 1
        config = json.loads(argv[spec_index])
    except (ValueError, IndexError, json.JSONDecodeError, TypeError):
        return False
    if config.get("method") != "mtp":
        return False
    if config.get("attention_backend") != CANDIDATE_ATTENTION_BACKEND:
        config["attention_backend"] = CANDIDATE_ATTENTION_BACKEND
        argv[spec_index] = json.dumps(config, separators=(",", ":"))
        changed = True
    return changed


def disable_single_stream_kv_offload(argv: MutableSequence[str]) -> bool:
    """Keep the 1.3M-token GPU KV pool local for the single-stream lane."""
    expected = (
        ("--kv-offloading-backend", CONTROL_KV_OFFLOAD_BACKEND),
        ("--kv-offloading-size", CONTROL_KV_OFFLOAD_SIZE),
    )
    indexes: list[int] = []
    for option, value in expected:
        try:
            index = argv.index(option)
        except ValueError:
            return False
        if index + 1 >= len(argv) or argv[index + 1] != value:
            return False
        indexes.append(index)
    for index in sorted(indexes, reverse=True):
        del argv[index : index + 2]
    return True


def disable_mamba_prefix_cache_for_decode(argv: MutableSequence[str]) -> bool:
    """Remove align-mode state-copy bookkeeping from the decode-only lane."""
    try:
        prefix_index = argv.index("--enable-prefix-caching")
        mode_index = argv.index("--mamba-cache-mode") + 1
    except ValueError:
        return False
    if mode_index >= len(argv) or argv[mode_index] != "align":
        return False
    del argv[prefix_index]
    # The mode index may shift when the flag appeared before it.
    mode_index = argv.index("--mamba-cache-mode") + 1
    argv[mode_index] = "none"
    return True


def add_online_fp8_quantization(argv: MutableSequence[str]) -> bool:
    """Add online FP8 only when production has no explicit quantization."""
    if "--quantization" in argv:
        return False
    try:
        insertion_index = argv.index("--speculative-config")
    except ValueError:
        return False
    quantization_args = [
        "--quantization",
        ONLINE_FP8_METHOD,
    ]
    if ONLINE_FP8_QUANTIZATION_CONFIG:
        quantization_args.extend(
            [
                "--quantization-config",
                json.dumps(ONLINE_FP8_QUANTIZATION_CONFIG, separators=(",", ":")),
            ]
        )
    argv[insertion_index:insertion_index] = quantization_args
    return True


def enable_flashinfer_jit_compatibility() -> bool:
    """Permit CUDA 13.2 nvcc with the installed CUDA 13.0 runtime headers."""
    variable = "FLASHINFER_EXTRA_CUDAFLAGS"
    current = os.environ.get(variable, "").strip()
    if FLASHINFER_CTK_COMPAT_FLAG in current.split():
        return False
    os.environ[variable] = " ".join(
        value for value in (current, FLASHINFER_CTK_COMPAT_FLAG) if value
    )
    return True


def tune_argv(argv: MutableSequence[str]) -> bool:
    """Rewrite only the known production K=5 command; return whether changed."""
    if not _is_target_invocation(argv):
        return False
    return _rewrite_control_width(argv)


def apply_startup_tuning() -> None:
    # During CPython site initialization for ``python -m``, sys.argv[0] is
    # still "-m"; the module name exists only in sys.orig_argv.  Authorize
    # against that immutable launch receipt, then rewrite the live arguments
    # that argparse will consume.
    original = getattr(sys, "orig_argv", sys.argv)
    if not _is_target_invocation(original):
        return

    # Opt-out for a bounded measurement window. Defaults to OFF, so an unset environment
    # behaves exactly as before this line existed — the tuned profile is unchanged unless
    # someone deliberately asks for it.
    #
    # Why it exists: this wrapper strips --enable-prefix-caching, which is correct for the
    # qualified single-stream decode path but makes one required measurement impossible —
    # a single request that traverses draft -> speculative -> verify -> accepted prefix AND
    # reuses KV. Both halves are proven separately; they have never run together, because the
    # only server that could do it has the flag removed before argparse sees it.
    #
    # The alternative was moving the .pth aside, which ALSO reverts K 6->5, max-num-seqs 4->5,
    # TRITON_ATTN->FLASHINFER and drops fp8_per_channel. That returns a materially different
    # engine and re-enables a path commented "known fatal MTP batch-5". A named, defaulted-off
    # switch is narrower than that and says what it does.
    if os.environ.get("QWEN_MTP_KEEP_PREFIX_CACHE", "").strip() == "1":
        print(
            "[qwen-mtp-boot-tuning] QWEN_MTP_KEEP_PREFIX_CACHE=1 — leaving "
            "--enable-prefix-caching and --mamba-cache-mode as given. All other tuning "
            "still applies.",
            file=sys.stderr,
            flush=True,
        )
        _rewrite_control_width(sys.argv)
        cap_unsafe_concurrency(sys.argv)
        use_triton_for_target_and_draft(sys.argv)
        disable_single_stream_kv_offload(sys.argv)
        enable_flashinfer_jit_compatibility()
        if ONLINE_FP8_ENABLED_MARKER.is_file():
            add_online_fp8_quantization(sys.argv)
        return
    changed = _rewrite_control_width(sys.argv)
    concurrency_changed = cap_unsafe_concurrency(sys.argv)
    attention_changed = use_triton_for_target_and_draft(sys.argv)
    offload_changed = disable_single_stream_kv_offload(sys.argv)
    prefix_cache_changed = disable_mamba_prefix_cache_for_decode(sys.argv)
    flashinfer_jit_changed = enable_flashinfer_jit_compatibility()
    fp8_changed = False
    if ONLINE_FP8_ENABLED_MARKER.is_file():
        fp8_changed = add_online_fp8_quantization(sys.argv)

    if changed:
        _rewrite_control_width(original)
        print(
            f"[qwen-mtp-boot-tuning] native MTP K=5 -> K={TUNED_K} "
            "(qualified single-stream width)",
            file=sys.stderr,
            flush=True,
        )
    if concurrency_changed:
        cap_unsafe_concurrency(original)
        print(
            "[qwen-mtp-boot-tuning] max-num-seqs 5 -> 4 "
            "(protects known fatal MTP batch-5 path)",
            file=sys.stderr,
            flush=True,
        )
    if attention_changed:
        use_triton_for_target_and_draft(original)
        print(
            "[qwen-mtp-boot-tuning] target+MTP attention "
            "FLASHINFER -> TRITON_ATTN (qualified full-graph path)",
            file=sys.stderr,
            flush=True,
        )
    if offload_changed:
        disable_single_stream_kv_offload(original)
        print(
            "[qwen-mtp-boot-tuning] disabled CPU KV offload "
            "(qualified single-stream resident-KV path)",
            file=sys.stderr,
            flush=True,
        )
    if prefix_cache_changed:
        disable_mamba_prefix_cache_for_decode(original)
        print(
            "[qwen-mtp-boot-tuning] disabled experimental Mamba align "
            "prefix cache (qualified single-stream decode path)",
            file=sys.stderr,
            flush=True,
        )
    if flashinfer_jit_changed:
        print(
            "[qwen-mtp-boot-tuning] enabled CUDA 13.2/13.0 CCCL "
            "compatibility for FlashInfer JIT",
            file=sys.stderr,
            flush=True,
        )
    if fp8_changed:
        add_online_fp8_quantization(original)
        print(
            "[qwen-mtp-boot-tuning] online quantization "
            f"BF16 -> {ONLINE_FP8_METHOD} (single-stream winner)",
            file=sys.stderr,
            flush=True,
        )


apply_startup_tuning()
