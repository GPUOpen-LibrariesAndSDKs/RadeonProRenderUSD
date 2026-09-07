"""Check the static fixture's radiance, reset and teardown, not just sample counts."""
import argparse
import json
import math
import re
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("log", type=Path)
args = parser.parse_args()
cycles = []
current = None
deleted = 0
errors = []
for line in args.log.read_text(errors="replace").splitlines():
    match = re.match(r"cycle (\d+) GPU (.+)", line)
    if match:
        current = {"cycle": int(match[1]), "gpu": match[2], "means": {}}
        cycles.append(current)
    if line == "context deleted":
        deleted += 1
    match = re.search(r"-(32|128|reset128|lightoff32)\.rgba32f RGB mean (\S+)$", line)
    # Churn snapshots have additional edit numbers and are not the static scene.
    if match and current is not None and not re.search(r"-edit\d+-", line):
        current["means"][match[1]] = float(match[2])
if not cycles or deleted != len(cycles):
    errors.append("Missing completed context teardown(s)")
for cycle in cycles:
    means = cycle["means"]
    label = f"cycle {cycle['cycle']}"
    if not all(key in means for key in ("32", "128", "reset128")):
        errors.append(f"{label}: missing baseline or reset image")
        continue
    if not all(math.isfinite(value) for value in means.values()):
        errors.append(f"{label}: nonfinite radiance")
        continue
    if means["32"] <= 0:
        errors.append(f"{label}: illuminated fixture is black")
        continue
    # This fixed, area-lit fixture changes far less than 5% through sampling.
    # The large margin catches the 1/N fade without requiring bit-identical GPUs.
    for key in ("128", "reset128"):
        ratio = means[key] / means["32"]
        if not 0.95 <= ratio <= 1.05:
            errors.append(f"{label}: {key}/32 radiance ratio {ratio:.6g}")
    if "lightoff32" in means and means["lightoff32"] != 0:
        errors.append(f"{label}: light-off fixture still has radiance")
print(json.dumps({"passed": not errors, "errors": errors, "contexts_deleted": deleted,
                  "cycles": cycles}, indent=2))
sys.exit(bool(errors))
