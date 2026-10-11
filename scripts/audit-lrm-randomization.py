#!/usr/bin/env python3
"""Check the Clause 18 audit inventory against a local IEEE 1800-2023 text.

This checks audit coverage and evidence paths, not language conformance. The
licensed LRM stays local and is not copied into the repository.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lrm", type=Path, help="local lrm-2023.txt")
    parser.add_argument(
        "--audit", type=Path,
        default=Path(__file__).resolve().parents[1] / "docs/lrm-randomization-audit.json",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    audit = json.loads(args.audit.read_text())
    sections = audit["sections"]
    identifiers = [section["section"] for section in sections]
    if len(identifiers) != len(set(identifiers)):
        parser.error("duplicate LRM subsection")
    if args.lrm:
        text = args.lrm.read_text()
        chapter = re.search(r"^18\. Constrained random value generation\n", text, re.M)
        if not chapter:
            parser.error("cannot find Clause 18 in the supplied LRM text")
        text = text[chapter.end():]
        end = re.search(r"^19\. ", text, re.M)
        if end:
            text = text[:end.start()]
        expected = set(re.findall(r"^(18\.\d+(?:\.\d+)*) [^\n]+$", text, re.M))
        missing = expected - set(identifiers)
        extra = set(identifiers) - expected
        if missing or extra:
            parser.error(f"uncovered subsections: {sorted(missing)}; unknown: {sorted(extra)}")
    statuses = Counter()
    for section in sections:
        if not section["features"]:
            parser.error(f"empty subsection {section['section']}")
        for feature in section["features"]:
            status = feature["status"]
            if status not in {"supported", "partial", "missing", "review", "overview", "illegal"}:
                parser.error(f"invalid status {status}")
            statuses[status] += 1
        for path in section["evidence"]["sources"] + section["evidence"]["tests"]:
            if not (root / path).is_file():
                parser.error(f"missing evidence {path}")
    print(f"{len(sections)} LRM subsections; {sum(statuses.values())} feature checks")
    print("; ".join(f"{status}: {count}" for status, count in sorted(statuses.items())))
    print("Inventory coverage verified; partial, missing, and review items remain open.")


if __name__ == "__main__":
    main()
