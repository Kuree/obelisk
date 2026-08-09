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
# A feature containing "{}" is a template filled from the rule's first capture
# group, which is how one rule can name a whole family (every system task, every
# still-unnamed construct mnemonic).
RULES: list[tuple[str, str, str]] = [
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
    (r"DPI export '[^']*' is not supported", "DPI export", "IEEE 1800 Ch. 35"),
    (r"!obelisk\.chandle\b", "chandle", "IEEE 1800 Ch. 35"),

    # Virtual interfaces are their own body of work: plain interfaces and
    # modports elaborate today, the class-held handle does not.
    (r"obelisk\.sv\.type\.virtual_interface|!obelisk\.virtual_interface<",
     "Virtual interfaces", "IEEE 1800 Ch. 25"),
    (r"obelisk\.sv\.(?:symbol\.interface|symbol\.modport)",
     "Interfaces and modports", "IEEE 1800 Ch. 25"),
    (r"obelisk\.sv\.symbol\.package", "Packages and imports", "IEEE 1800 Ch. 26"),
    (r"obelisk\.sv\.(?:symbol\.primitive|symbol\.udp)", "User-defined primitives", "IEEE 1800 Ch. 30"),
    (r"unsupported built-in primitive|primitive (?:delays|strengths) are not supported"
     r"|primitive '[^']*' requires exactly one output"
     r"|only simple expressions are allowed for primitive port connections",
     "Gate-level primitives", "IEEE 1800 Ch. 28"),
    (r"(?:specify block|timing check|parallel path connection)", "Specify blocks and timing checks", "IEEE 1800 Ch. 31"),
    (r"(?:does not have a time scale defined|DPI time scale must be|\$printtimescale target scope)",
     "Timescale handling", "IEEE 1800 Ch. 3"),

    # --- assertions -------------------------------------------------------
    (r"obelisk\.sv\.statement\.immediate_assertion", "Immediate assertions", "IEEE 1800 Ch. 16"),
    (r"obelisk\.sv\.(assertion\.|statement\.concurrent_assertion|expression\.assertion_instance"
     r"|symbol\.local_assertion_var|symbol\.assertion_port)|!obelisk\.(sequence|property)\b"
     r"|concurrent assertions require typed Preponed sampling",
     "Concurrent assertions / SVA", "IEEE 1800 Ch. 16"),

    # --- OOP --------------------------------------------------------------
    (r"obelisk\.sv\.symbol\.generic_class_def", "Parameterized classes", "IEEE 1800 Ch. 8"),
    (r"obelisk\.sv\.symbol\.method_prototype", "Virtual methods / prototypes", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.process", "std::process", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.mailbox", "std::mailbox", "IEEE 1800 Ch. 8"),
    (r"class_handle<@\S*std::\S*\.semaphore", "std::semaphore", "IEEE 1800 Ch. 8"),
    # The built-in classes also surface post-import as an unresolved mangled
    # symbol rather than a class_handle type.
    (r"@__obelisk_class_\w*?_(process|mailbox|semaphore)\b", "std::{}", "IEEE 1800 Ch. 8"),
    (r"obelisk\.sv\.(type\.class_type|expression\.new_class|symbol\.class_property)|class_handle<",
     "Classes / objects", "IEEE 1800 Ch. 8"),

    # --- randomization / coverage -----------------------------------------
    (r"obelisk\.sv\.constraint\.|symbol\.constraint_block"
     r"|random properties must be packed integral"
     r"|constraint expression is outside the total side-effect-free executable boundary",
     "Constraints / randomize()", "IEEE 1800 Ch. 18"),
    (r"obelisk\.sv\.(symbol\.rand_seq_production|statement\.rand_sequence)", "randsequence", "IEEE 1800 Ch. 18"),
    (r"obelisk\.sv\.statement\.rand_case", "randcase", "IEEE 1800 Ch. 18"),
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
    (r"obelisk\.sv\.statement\.procedural_assign"
     r"|(?:signal-dependent )?force and procedural assign"
     r"|lvalue of (?:force/release|procedural assign/deassign)",
     "procedural assign / force / release", "IEEE 1800 Ch. 10"),
    (r"assignment destination is not a reference or driver"
     r"|variable declaration has no reference binding"
     r"|expected a reference to a net",
     "Assignment lvalue binding", "IEEE 1800 Ch. 10"),
    (r"conditional_op", "Ternary ?: operator", "IEEE 1800 Ch. 11"),
    (r"expression\.streaming|node streaming", "Streaming operators {<<,>>}", "IEEE 1800 Ch. 11"),
    (r"structured_assignment_pattern", "Assignment patterns '{...}", "IEEE 1800 Ch. 11"),
    (r"replication \(nonconstant", "Non-constant replication", "IEEE 1800 Ch. 11"),
    (r"range_select \(dynamic|range_select \(selection input", "Indexed/dynamic part-select", "IEEE 1800 Ch. 11"),
    # Obelisk promotes these to errors where Icarus/Verilator keep simulating.
    (r"\[-W(?:range-oob|index-oob)\]", "Out-of-bounds select strictness", "Strictness"),
    (r"assignment \(compound assignment\)", "Compound assignment (+=, etc)", "IEEE 1800 Ch. 11"),

    # --- elaboration ------------------------------------------------------
    (r"named value has no frozen unit-local binding", "Generate-scope / param binding", "IEEE 1800 Ch. 27"),
    (r"cannot refer to type names via hierarchical reference", "Hierarchical type references", "IEEE 1800 Ch. 23"),
    (r"could not resolve hierarchical path name", "Hierarchical path resolution", "IEEE 1800 Ch. 23"),
    (r"is not a valid type for a net", "Unsupported net data type", "IEEE 1800 Ch. 6"),
    (r"unknown module", "Module library lookup (-y/+libext)", "IEEE 1800 Ch. 23"),
    (r"has range metadata even though the range is absent", "Range metadata bug", "Bug"),
    (r"imported Slang dialect IR failed verification", "Slang IR verifier failure", "Bug"),
    (r"rejected image: ", "Bytecode image validation", "Bug"),
    (r"stable code-unit ID collision", "Code-unit ID collision", "Bug"),
    (r"invalid semantic AST node", "Frontend rejected the AST", "Frontend"),

    # --- frontend rejections ----------------------------------------------
    # Slang refused the source outright. Not a lowering gap: the construct never
    # reached Obelisk, so these need a frontend fix (or are genuinely invalid).
    (r"member access input is not a matching aggregate", "Struct/union member access", "IEEE 1800 Ch. 7"),
    (r"port '[^']*' does not exist in|wrong number of port connections"
     r"|port declaration '[^']*' does not match any port",
     "Port connection mismatch", "Frontend"),
    (r"\bexpected (?:a |an )?(?:declaration name|identifier|expression|enum base type"
     r"|method name|data type|net type|statement|'\S+')"
     r"|use of undeclared identifier|identifier '[^']*' used before its declaration"
     r"|unknown macro or compiler directive|missing '[^']*' in parameter list",
     "Frontend parse/name error", "Frontend"),
    (r"cannot be assigned to type|no implicit conversion from|operand is not a packed value",
     "Frontend type error", "Frontend"),

    # --- harness, not language -------------------------------------------
    (r"compile exceeded \d+", "Compile timeout", "Performance"),
    (r"No such file or directory", "Missing input file", "Harness"),
    (r"undefined symbol: |undefined reference to ", "Native link dependency", "Harness"),

    # --- misc system tasks ------------------------------------------------
    (r"system call (\$\w+)", "system task {}", "System tasks"),
    (r"unknown system name '([^']*)'", "system task {}", "System tasks"),

    # --- generic fallbacks ------------------------------------------------
    # Every unhandled construct reaches the user through one of three shapes.
    # Naming the bucket after the mnemonic keeps the long tail grouped by what
    # is actually missing instead of collapsing to one opaque row; the "unnamed"
    # prefix marks it as a gap in this table, not a described feature.
    (r"unsupported semantic (?:node|construct) in the first simulation slice: "
     r"obelisk\.sv\.\w+\.(\w+)", "unnamed construct {}", "Other"),
    (r"unsupported semantic type in the first simulation slice: '!obelisk[\w.]*?\.(\w+)",
     "unnamed type {}", "Other"),
    (r"cannot resolve \"\w+\" @(\S+)", "unnamed symbol {}", "Other"),
    # An op-level diagnostic escaping to the user means Obelisk built IR its own
    # verifier rejects — a defect, whatever the construct that provoked it.
    (r"'(obelisk\w*[\w.]*)' op ", "IR verifier / op invariant", "Bug"),
]

COMPILED = [(re.compile(pattern), feature, area) for pattern, feature, area in RULES]

# A line is worth classifying if it carries one of these. "error:"/"unsupported"
# cover compiler diagnostics; the rest are harness-side failures the runner
# reports in place of a diagnostic (a timeout has no error line at all).
DIAGNOSTIC_MARKERS = ("error:", "unsupported", "exceeded", "rejected image:")

CRASH_MARKERS = ("PLEASE submit a bug report", "Stack dump", "terminate called")
CRASH_FEATURE = "Compiler crash"
UNCLASSIFIED = "Unclassified long tail"


def classify_line(line: str) -> tuple[str, str] | None:
    """Classify one diagnostic line into (feature, area), or None if unmatched."""
    for regex, feature, area in COMPILED:
        match = regex.search(line)
        if match:
            if "{}" in feature:  # capture group names the feature
                return (feature.format(match.group(1)), area)
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
        if not any(marker in line for marker in DIAGNOSTIC_MARKERS):
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
        # Templated rules name a family; match the fixed part around the hole.
        if "{}" in rule_feature:
            prefix, suffix = rule_feature.split("{}", 1)
            if feature.startswith(prefix) and feature.endswith(suffix):
                return area
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
    width = max(len("Feature"), max(len(feature) for feature in blocks))
    areas = max(len("Area"), max(len(area_of(feature)) for feature in blocks))
    lines = [f"{'Feature':<{width}}  {'Area':<{areas}} {'blocks':>7} {'first':>6} {'only':>6}",
             "-" * (width + areas + 24)]
    for feature, count in rows:
        lines.append(f"{feature:<{width}}  {area_of(feature):<{areas}} {count:>7} "
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
