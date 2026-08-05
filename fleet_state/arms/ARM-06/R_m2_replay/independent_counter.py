#!/usr/bin/env python3
"""ARM-06 independent replay of the fleet turn counter — built from raw JSONL.

Deliberately NOT a re-run of Prime's instrument. Definitions are declared here
and applied to raw records:

  completed turn   assistant record with message.stop_reason == 'end_turn'
                   (defect 1: counting records that merely contain text would
                   count every tool_use step as a turn)

  tool result      user record whose content list holds a tool_result block.
                   NEVER human input (defect 2)

  hook session     a transcript file whose ONLY non-tool_result user messages
                   carry promptSource == 'sdk'. These are security-review hook
                   invocations, not arm turns (defect 5)

  injection        non-tool_result user message whose text begins with a known
                   continuation prefix

  human-typed      DECLARED UNRESOLVABLE from metadata — see the marker audit.
                   Counted by content, never by subtraction (defect 3)

  interrupt        '[Request interrupted by user]' — a human ACTION but NOT a
                   continuation (defect 6)

Every transcript file in every arm dir is read (defect 4).
"""
import json
import pathlib
from collections import Counter

ROOT = pathlib.Path.home() / '.claude' / 'projects'
CONT_PREFIXES = ('PRIME CONTINUITY',)
INTERRUPT = '[Request interrupted by user]'


def text_of(m):
    c = m.get('content')
    if isinstance(c, str):
        return c
    if isinstance(c, list):
        return ' '.join(b.get('text', '') for b in c
                        if isinstance(b, dict) and b.get('type') == 'text')
    return ''


def is_tool_result(m):
    c = m.get('content')
    return isinstance(c, list) and any(
        isinstance(b, dict) and b.get('type') == 'tool_result' for b in c)


def scan_file(p):
    s = dict(end_turn=0, tool_use=0, other_stop=0, tool_results=0,
             sdk=0, typed=0, meta=0, none_src=0, injections=0,
             interrupts=0, typed_non_injection=0, origin_human=0, records=0)
    typed_texts = []
    for line in p.open(errors='replace'):
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        s['records'] += 1
        t = o.get('type')
        m = o.get('message')
        if not isinstance(m, dict):
            continue
        if t == 'assistant':
            sr = m.get('stop_reason')
            if sr == 'end_turn':
                s['end_turn'] += 1
            elif sr == 'tool_use':
                s['tool_use'] += 1
            elif sr is not None:
                s['other_stop'] += 1
        elif t == 'user':
            if is_tool_result(m):
                s['tool_results'] += 1
                continue
            ps = o.get('promptSource')
            org = o.get('origin')
            txt = text_of(m).strip()
            if isinstance(org, dict) and org.get('kind') == 'human':
                s['origin_human'] += 1
            if o.get('isMeta'):
                s['meta'] += 1
            if ps == 'sdk':
                s['sdk'] += 1
            elif ps == 'typed':
                s['typed'] += 1
                if txt.startswith(CONT_PREFIXES):
                    s['injections'] += 1
                else:
                    s['typed_non_injection'] += 1
                    typed_texts.append(txt[:70].replace('\n', ' '))
            else:
                s['none_src'] += 1
                if INTERRUPT in txt:
                    s['interrupts'] += 1
    return s, typed_texts


def classify(p, s):
    if s['sdk'] > 0 and s['typed'] == 0:
        return 'HOOK'
    if s['typed'] > 0:
        return 'MAIN'
    return 'OTHER'


dirs = sorted(d for d in ROOT.iterdir() if d.is_dir() and
              ('claude-squad' in d.name or 'fleet-foundry' in d.name))

print('=' * 108)
print('ARM-06 INDEPENDENT COUNT — every transcript file, raw records')
print('=' * 108)

grand = {}
for d in dirs:
    label = d.name.split('worktrees-')[-1]
    files = sorted(d.glob('*.jsonl'))
    agg = Counter()
    kinds = Counter()
    all_typed = []
    for f in files:
        s, tt = scan_file(f)
        k = classify(f, s)
        kinds[k] += 1
        if k == 'MAIN':
            for key, v in s.items():
                agg[key] += v
            all_typed += tt
        else:
            agg['hook_end_turn'] += s['end_turn']
            agg['hook_interrupts'] += s['interrupts']
    grand[label] = (agg, kinds, all_typed, len(files))
    print(f'\n{label[:70]}')
    print(f'  files={len(files):3d}  MAIN={kinds["MAIN"]}  HOOK={kinds["HOOK"]}  OTHER={kinds["OTHER"]}')
    print(f'  MAIN completed turns (stop_reason==end_turn) : {agg["end_turn"]}')
    print(f'  MAIN tool_use steps (NOT turns)              : {agg["tool_use"]}')
    print(f'  MAIN tool_result user records (NOT human)    : {agg["tool_results"]}')
    print(f'  MAIN injections (PRIME CONTINUITY prefix)    : {agg["injections"]}')
    print(f'  MAIN typed NON-injection messages            : {agg["typed_non_injection"]}')
    print(f'  MAIN records with origin.kind == "human"     : {agg["origin_human"]}')
    print(f'  MAIN interrupts (human action, not cont.)    : {agg["interrupts"]}')
    print(f'  HOOK-session end_turns EXCLUDED from arm count: {agg["hook_end_turn"]}')
    if all_typed:
        print('  typed non-injection texts:')
        for t in all_typed[:14]:
            print(f'      - {t}')
        if len(all_typed) > 14:
            print(f'      ... and {len(all_typed)-14} more')
