"""The test suite in parallel: one process per test module, several at once, the slowest modules first.

    python -m tools.run_tests              all of tests/, the same tests as `python -m unittest discover -s tests`
    python -m tools.run_tests --jobs 4     at most 4 modules at once (default: logical cores - 2)

Each module runs exactly as discover runs it (`-s tests -p <module>.py`), so imports and fixtures behave the same.
SERIAL modules run alone after the rest (they share something with other modules). Module times are remembered in
evidence/test-times.json to start the slowest first next time. The last lines read like unittest's own
("Ran N tests in X s" / "OK (skipped=k)" / "FAILED (...)"); the output of every failing module is printed in full.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
TIMES = ROOT / "evidence" / "test-times.json"
SERIAL: set[str] = set()   # modules that must not overlap with others (none known yet)
# Modules with one long test split into parts that run side by side (the test reads COF_TEST_SHARD="i/n"): the module
# runs once with "0/n" (that test does none of it), and the test runs n more times, part 1..n each.
SHARDED = {"test_map_merge_groups": ("test_every_group_builds_as_recorded_and_passes_the_planner", 8)}


def modules() -> list[str]:
    return sorted(p.stem for p in TESTS.glob("test_*.py"))


def known_times() -> dict:
    try:
        return json.loads(TIMES.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def jobs_for(name: str) -> list[tuple[str, str | None, int, str | None]]:
    """(label, shard, tests counted from it, -k filter) for each process a module runs as."""
    if name not in SHARDED:
        return [(name, None, 1, None)]
    test, count = SHARDED[name]
    return [(name, f"0/{count}", 1, None)] + [(f"{name} [{i}/{count}]", f"{i}/{count}", 0, test)
                                              for i in range(1, count + 1)]


def run_module(name: str, shard: str | None = None, counts: int = 1, only: str | None = None) -> dict:
    started = time.monotonic()
    command = [sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", f"{name}.py"]
    if only:
        command += ["-k", only]
    env = dict(os.environ, COF_TEST_SHARD=shard) if shard else None
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            env=env)
    output = result.stdout + result.stderr
    ran = re.search(r"^Ran (\d+) tests? in", output, re.M)
    skipped = re.search(r"skipped=(\d+)", output)
    return {"name": name, "code": result.returncode, "seconds": time.monotonic() - started,
            "tests": int(ran.group(1)) * counts if ran else 0,
            "skipped": int(skipped.group(1)) * counts if skipped else 0, "output": output}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    args = parser.parse_args(argv)
    names, times = modules(), known_times()
    jobs = [(name, *job) for name in names if name not in SERIAL for job in jobs_for(name)]
    jobs.sort(key=lambda j: -times.get(j[1], times.get(j[0], 60)))
    started, results, seconds = time.monotonic(), [], {}
    print(f"{len(names)} test modules, {args.jobs} at a time", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_module, name, shard, counts, only): label
                   for name, label, shard, counts, only in jobs}
        for future in as_completed(futures):
            done, label = future.result(), futures[future]
            results.append(done)
            seconds[label] = round(done["seconds"], 1)
            print(f"  {'ok  ' if done['code'] == 0 else 'FAIL'} {label}: {done['tests']} tests, "
                  f"{done['seconds']:.0f} s", flush=True)
    for name in sorted(n for n in names if n in SERIAL):
        done = run_module(name)
        results.append(done)
        seconds[name] = round(done["seconds"], 1)
        print(f"  {'ok  ' if done['code'] == 0 else 'FAIL'} {name} (alone): {done['tests']} tests, "
              f"{done['seconds']:.0f} s", flush=True)
    try:
        TIMES.parent.mkdir(parents=True, exist_ok=True)
        TIMES.write_text(json.dumps({**times, **seconds}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except OSError:
        pass
    failed = [r for r in results if r["code"] != 0]
    for r in failed:
        print(f"\n===== {r['name']} =====\n{r['output'].strip()}", flush=True)
    longest = max(seconds, key=seconds.get) if seconds else "-"
    total, skipped = sum(r["tests"] for r in results), sum(r["skipped"] for r in results)
    print(f"\n{'-' * 70}\nRan {total} tests in {time.monotonic() - started:.3f}s (longest: {longest})\n")
    if failed:
        print(f"FAILED (modules={len(failed)}: {', '.join(sorted({r['name'] for r in failed}))})")
        return 1
    print(f"OK (skipped={skipped})" if skipped else "OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
