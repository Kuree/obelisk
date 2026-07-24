"""Recording and rendering conformance progress over time.

The point of the harness is to watch the pass rate move as Obelisk gains
features. Each recorded run appends one JSON object to
`benchmark/history/<suite>.jsonl`; these files are tracked in git and are the
durable record of the project's conformance. The renderer shows the curve so a
landed feature's effect is visible at a glance.
"""

from __future__ import annotations

import datetime
import json
from pathlib import Path

HISTORY_DIR = Path(__file__).resolve().parents[1] / "history"


def record_path(suite_name: str) -> Path:
    return HISTORY_DIR / f"{suite_name}.jsonl"


def build_record(suite_name: str, obelisk_rev: str, suite_rev: str,
                 summary: dict, blockers: dict[str, int]) -> dict:
    """Assemble one history record from a run summary.

    `summary` carries the outcome counts a suite driver produced; `blockers` is
    the top of the classified feature table. Dates are absolute (UTC) so records
    stay meaningful independent of when they are read.
    """
    record = {
        "date": datetime.datetime.now(datetime.timezone.utc)
        .replace(microsecond=0).isoformat(),
        "suite": suite_name,
        "obelisk_rev": obelisk_rev,
        "suite_rev": suite_rev,
    }
    record.update(summary)
    record["blockers"] = blockers
    return record


def append_record(suite_name: str, record: dict) -> Path:
    HISTORY_DIR.mkdir(parents=True, exist_ok=True)
    path = record_path(suite_name)
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(record) + "\n")
    return path


def load_records(suite_name: str) -> list[dict]:
    path = record_path(suite_name)
    if not path.exists():
        return []
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            records.append(json.loads(line))
    return records


def format_history(suite_name: str) -> str:
    """Render the recorded runs for a suite as a progress table."""
    records = load_records(suite_name)
    if not records:
        return f"No history recorded for '{suite_name}'.\n"
    lines = [f"Conformance history for '{suite_name}' ({len(records)} runs)",
             f"{'date':<20} {'obelisk':<9} {'suite':<9} {'passed':>7} "
             f"{'total':>6}  top blocker",
             "-" * 78]
    for record in records:
        blockers = record.get("blockers", {})
        top = next(iter(blockers.items()), None)
        top_text = f"{top[0]} ({top[1]})" if top else "-"
        date = record.get("date", "?")[:19]
        lines.append(f"{date:<20} {record.get('obelisk_rev', '?'):<9} "
                     f"{record.get('suite_rev', '?'):<9} "
                     f"{record.get('passed', 0):>7} {record.get('total', 0):>6}  "
                     f"{top_text}")
    return "\n".join(lines) + "\n"
