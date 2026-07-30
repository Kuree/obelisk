"""Mapping Obelisk diagnostics onto named language features.

A suite run produces a per-test compile log. This module turns those logs into a
feature-level picture: which missing Obelisk features gate which tests, so the
harness can answer "what should I build next" rather than just "how many fail".
The rule table is shared across suites — Verilator and ivtest emit the same
Obelisk diagnostics, so neither driver should carry its own copy.

Three per-test counters drive the roadmap:

  blocks  tests whose compile log mentions this feature at all (cannot pass
          until the feature exists)
  first   tests where it is the earliest diagnostic
  only    tests where it is the sole missing feature, so implementing it alone
          is expected to carry the test to simulation

The unclassified-tail count is kept deliberately visible: it is the honest
measure of how much the rule table still misses.
"""

from __future__ import annotations

import collections
import re

ANSI = re.compile(r"\x1b\[[0-9;]*m")

# (regex, feature, area). First match wins, so order runs specific -> general.
# A feature of None means the capture group names the feature (system tasks).
RULES: list[tuple[str, str | None, str]] = [
    # --- time / scheduling ------------------------------------------------
    (r"system call \$time\b", "$time / $stime / $realtime", "IEEE 1800 Ch. 20"),
    (r"conversion 'i32' to '!obelisk_sim\.time'", "$time / $stime / $realtime", "IEEE 1800 Ch. 20"),
    (r"continuous-assignment delays", "Continuous-assign delays (assign #N)", "IEEE 1800 Ch. 10"),
    (r"continuous-assignment strengths", "Continuous-assign strengths", "IEEE 1800 Ch. 10"),
    (r"wait_order occurrence sequencing", "wait_order sequencing", "IEEE 1800 Ch. 9"),
    (r"scheduler-owned deferred action", "Deferred nonblocking event assignment", "IEEE 1800 Ch. 9"),
    (r"(?:event timing control|event expression inventory|event-list member|repeated-event inventory|empty event list)",
     "Event timing controls", "IEEE 1800 Ch. 9"),
    (r"(?:wait inventory|computed wait condition|wait condition is not directly watchable)",
     "wait statements", "IEEE 1800 Ch. 9"),
    (r"system call \$(display|write|monitor|strobe)", "$monitor / $strobe", "IEEE 1800 Ch. 21"),

    (r"(?:real and realtime nets|unsupported net resolution kind|net strengths are not supported|net delays are not supported)",
     "Net types, strengths, and delays", "IEEE 1800 Ch. 6"),
    (r"packed type has unsupported width|unsupported normalized conversion|elaborated constant has unsupported normalized type",
     "Packed data types and conversions", "IEEE 1800 Ch. 6"),
    (r"unsupported string built-in method|timed or nonblocking string compound assignment is unsupported",
     "string operations", "IEEE 1800 Ch. 6"),
    (r"unsupported literal base", "Integer literal base", "IEEE 1800 Ch. 5"),
    (r"DPI (?:import formal type|import return type)|DPI ref formals", "DPI type and direction restrictions", "IEEE 1800 Ch. 35"),

    (r"obelisk\.sv\.(?:symbol\.interface|symbol\.modport|type\.virtual_interface)",
     "Interfaces and modports", "IEEE 1800 Ch. 25"),
    (r"obelisk\.sv\.symbol\.package", "Packages and imports", "IEEE 1800 Ch. 26"),
    (r"obelisk\.sv\.(?:symbol\.primitive|symbol\.udp)", "User-defined primitives", "IEEE 1800 Ch. 30"),
    (r"(?:specify block|timing check)", "Specify blocks and timing checks", "IEEE 1800 Ch. 31"),

    # --- assertions -------------------------------------------------------
    (r"obelisk\.sv\.statement\.immediate_assertion", "Immediate assertions", "IEEE 1800 Ch. 16"),
    (r"obelisk\.sv\.(assertion\.|statement\.concurrent_assertion|expression\.assertion_instance"
     r"|symbol\.local_assertion_var|symbol\.assertion_port)", "Concurrent assertions / SVA", "IEEE 1800 Ch. 16"),

    # --- OOP --------------------------------------------------------------
    (r"obelisk\.sv\.symbol\.generic_class_def", "Parameterized classes", "IEEE 1800 Ch. 8"),
    (r"obelisk\.sv\.symbol\.method_prototype", "Virtual methods / prototypes", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.process", "std::process", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.mailbox", "std::mailbox", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.semaphore", "std::semaphore", "IEEE 1800 Ch. 8"),
    (r"obelisk\.sv\.(type\.class_type|expression\.new_class|symbol\.class_property)|class_handle<",
     "Classes / objects", "IEEE 1800 Ch. 8"),

    # --- randomization / coverage -----------------------------------------
    (r"obelisk\.sv\.constraint\.|symbol\.constraint_block", "Constraints / randomize()", "IEEE 1800 Ch. 18"),
    (r"obelisk\.sv\.(symbol\.rand_seq_production|statement\.rand_sequence)", "randsequence", "IEEE 1800 Ch. 18"),
    (r"obelisk\.sv\.symbol\.(coverage_bin|coverpoint|covergroup)", "Functional coverage", "IEEE 1800 Ch. 19"),

    # --- dynamic types ----------------------------------------------------
    (r"!obelisk\.queue<", "Queues", "IEEE 1800 Ch. 7"),
    (r"!obelisk\.dynarray<", "Dynamic arrays", "IEEE 1800 Ch. 7"),
    (r"!obelisk\.assoc<", "Associative arrays", "IEEE 1800 Ch. 7"),
    (r"!obelisk\.string\b", "string type", "IEEE 1800 Ch. 6"),
    (r"!obelisk\.real\b|system call \$(realtobits|bitstoreal|rtoi|itor)", "real / shortreal", "IEEE 1800 Ch. 6"),

    # --- statements / expressions -----------------------------------------
    (r"node do_while_loop|do_while_loop", "do-while loop", "IEEE 1800 Ch. 12"),
    (r"for_loop \(expected condition", "for loop (general form)", "IEEE 1800 Ch. 12"),
    (r"forever_loop", "forever loop", "IEEE 1800 Ch. 12"),
    (r"obelisk\.sv\.statement\.procedural_assign", "procedural assign / force / release", "IEEE 1800 Ch. 10"),
    (r"conditional_op", "Ternary ?: operator", "IEEE 1800 Ch. 11"),
    (r"expression\.streaming|node streaming", "Streaming operators {<<,>>}", "IEEE 1800 Ch. 11"),
    (r"structured_assignment_pattern", "Assignment patterns '{...}", "IEEE 1800 Ch. 11"),
    (r"replication \(nonconstant", "Non-constant replication", "IEEE 1800 Ch. 11"),
    (r"range_select \(dynamic|range_select \(selection input", "Indexed/dynamic part-select", "IEEE 1800 Ch. 11"),
    (r"assignment \(compound assignment\)", "Compound assignment (+=, etc)", "IEEE 1800 Ch. 11"),

    # --- elaboration ------------------------------------------------------
    (r"named value has no frozen unit-local binding", "Generate-scope / param binding", "IEEE 1800 Ch. 27"),
    (r"cannot refer to type names via hierarchical reference", "Hierarchical type references", "IEEE 1800 Ch. 23"),
    (r"could not resolve hierarchical path name", "Hierarchical path resolution", "IEEE 1800 Ch. 23"),
    (r"is not a valid type for a net", "Unsupported net data type", "IEEE 1800 Ch. 6"),
    (r"unknown module", "Module library lookup (-y/+libext)", "IEEE 1800 Ch. 23"),
    (r"has range metadata even though the range is absent", "Range metadata bug", "Bug"),
    (r"imported Slang dialect IR failed verification", "Slang IR verifier failure", "Bug"),

    # --- misc system tasks ------------------------------------------------
    (r"system call (\$\w+)", None, "System tasks"),
    (r"unknown system name '([^']*)'", None, "System tasks"),
]

COMPILED = [(re.compile(pattern), feature, area) for pattern, feature, area in RULES]

CRASH_MARKERS = ("PLEASE submit a bug report", "Stack dump", "terminate called")
CRASH_FEATURE = "Compiler crash"
UNCLASSIFIED = "Unclassified long tail"


def classify_line(line: str) -> tuple[str, str] | None:
    """Classify one diagnostic line into (feature, area), or None if unmatched."""
    for regex, feature, area in COMPILED:
        match = regex.search(line)
        if match:
            if feature is None:  # capture group names the feature
                return ("system task " + match.group(1), area)
            return (feature, area)
    return None


def features_in_log(text: str) -> list[str]:
    """Return the ordered, de-duplicated features a compile log demands.

    A compiler crash is treated as its own blocking feature; an error-bearing log
    that matches no rule contributes the unclassified-tail marker so nothing
    silently vanishes from the totals.
    """
    text = ANSI.sub("", text)
    seen: list[str] = []
    if any(marker in text for marker in CRASH_MARKERS):
        seen.append(CRASH_FEATURE)
    matched_any = False
    for line in text.splitlines():
        if "error:" not in line and "unsupported" not in line:
            continue
        hit = classify_line(line)
        if hit:
            matched_any = True
            if hit[0] not in seen:
                seen.append(hit[0])
    if not matched_any and CRASH_FEATURE not in seen:
        # There were errors (the log is a failure) but no rule matched: a
        # long-tail parse/elaboration gap that needs reading individually.
        seen.append(UNCLASSIFIED)
    return seen


def area_of(feature: str) -> str:
    """Return the area a feature belongs to (for table grouping)."""
    if feature == CRASH_FEATURE:
        return "Bug"
    if feature == UNCLASSIFIED:
        return "Other"
    for _, rule_feature, area in RULES:
        if rule_feature == feature:
            return area
    if feature.startswith("system task "):
        return "System tasks"
    return "Other"


def tally(logs: dict[str, str]) -> dict:
    """Compute blocks/first/only counters over a map of test-name -> compile log.

    Only failing logs (those that yield at least one feature) contribute.
    """
    blocks: collections.Counter = collections.Counter()
    first: collections.Counter = collections.Counter()
    only: collections.Counter = collections.Counter()
    needs: dict[str, set[str]] = {}
    for name, text in logs.items():
        seen = features_in_log(text)
        if not seen:
            continue
        needs[name] = set(seen)
        first[seen[0]] += 1
        for feature in seen:
            blocks[feature] += 1
        if len(seen) == 1:
            only[seen[0]] += 1
    return {"blocks": blocks, "first": first, "only": only, "needs": needs}


def format_table(counters: dict) -> str:
    """Render the blocks/first/only feature table as text."""
    blocks = counters["blocks"]
    if not blocks:
        return "No classified blockers.\n"
    first, only = counters["first"], counters["only"]
    rows = sorted(blocks.items(), key=lambda kv: -kv[1])
    width = max(len(feature) for feature in blocks)
    lines = [f"{'Feature':<{width}}  {'Area':<14} {'blocks':>7} {'first':>6} {'only':>6}",
             "-" * (width + 40)]
    for feature, count in rows:
        lines.append(f"{feature:<{width}}  {area_of(feature):<14} {count:>7} "
                     f"{first.get(feature, 0):>6} {only.get(feature, 0):>6}")
    return "\n".join(lines) + "\n"


def greedy_order(needs: dict[str, set[str]], steps: int = 15) -> str:
    """Render the greedy set-cover order: which features unblock most tests soonest.

    A test unblocks once every feature it needs is implemented. At each step the
    feature that completes the most still-blocked tests is chosen. This is what
    turns a flat blocker list into an implementation sequence.
    """
    total = len(needs)
    if total == 0:
        return "No blocked tests to order.\n"
    remaining = dict(needs)
    done: set[str] = set()
    lines = [f"{total} blocked tests",
             f"{'#':>2}  {'Implement next':<40} {'unblocked':>10} {'cumulative':>11}",
             "-" * 70]
    cumulative = 0
    for step in range(1, steps + 1):
        candidates = {feature for need in remaining.values() for feature in need} - done
        best, best_gain = None, 0
        for feature in candidates:
            gain = sum(1 for need in remaining.values() if need <= done | {feature})
            if gain > best_gain:
                best, best_gain = feature, gain
        if not best or best_gain == 0:
            break
        done.add(best)
        cumulative += best_gain
        remaining = {name: need for name, need in remaining.items() if not need <= done}
        lines.append(f"{step:>2}  {best:<40} {best_gain:>10} "
                     f"{cumulative:>7} ({100 * cumulative / total:.0f}%)")
    lines.append(f"\nStill blocked after {len(done)} features: {len(remaining)} "
                 f"({100 * len(remaining) / total:.0f}%)")
    return "\n".join(lines) + "\n"
