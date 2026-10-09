"""Check accepted idempotence proofs by exhaustive finite-state execution.

LRM 4.6 and 9.2.2.2.1 require preserving source-order snapshots and retained
state; always_comb is not a license to evaluate a state transition twice.
The oracle executes F twice, independently of the analysis's identity proof.
"""

import itertools
import re
import subprocess
import sys


actions = (
    "hold", "replace", "counter", "swap", "snapshot", "copy",
    "sequential", "hold_derived", "overwritten_snapshot", "partial_hold",
)
controls = ("input", "old_a", "old_b", "xor_ab")
cases = list(itertools.product(controls, actions, actions, (False, True)))


def emit_action(action, prefix):
    lines = []

    def store(value, target):
        lines.append(f"simulation.ref.store %{value} to %{target} : i2, !simulation.ref<i2>")

    if action == "hold":
        store("old_a", "a")
        store("old_b", "b")
    elif action == "replace":
        store("input", "a")
        store("input_xor", "b")
    elif action == "counter":
        store("increment", "a")
        store("old_b", "b")
    elif action == "swap":
        store("old_b", "a")
        store("old_a", "b")
    elif action == "snapshot":
        store("input", "a")
        store("old_a", "b")
    elif action == "copy":
        store("old_b", "a")
        store("old_b", "b")
    elif action == "sequential":
        store("input", "a")
        lines.append(f"%{prefix}_fresh = simulation.ref.load %a : !simulation.ref<i2> -> i2")
        store(f"{prefix}_fresh", "b")
    elif action == "hold_derived":
        store("old_a", "a")
        store("derived", "b")
    elif action == "overwritten_snapshot":
        store("old_a", "b")
        store("input", "a")
        store("input", "b")
    elif action == "partial_hold":
        store("input", "a")
    else:
        raise AssertionError(action)
    lines.append("cf.br ^exit")
    return "\n".join(lines)


def execute(case, input_value, a, b, publications=False):
    control, yes, no, switch = case
    condition = {"input": input_value, "old_a": a, "old_b": b, "xor_ab": a ^ b}[control]
    action = yes if condition == 0 else no
    if publications:
        # Record source-order writes, including overwritten publications. The
        # final-state oracle below deliberately has a different observation.
        return {
            "hold": ((1, a), (2, b)),
            "replace": ((1, input_value), (2, input_value ^ 1)),
            "counter": ((1, (a + 1) % 4), (2, b)),
            "swap": ((1, b), (2, a)),
            "snapshot": ((1, input_value), (2, a)),
            "copy": ((1, b), (2, b)),
            "sequential": ((1, input_value), (2, input_value)),
            "hold_derived": ((1, a), (2, a ^ input_value)),
            "overwritten_snapshot": ((2, a), (1, input_value), (2, input_value)),
            "partial_hold": ((1, input_value),),
        }[action]
    return {
        "hold": (a, b),
        "replace": (input_value, input_value ^ 1),
        "counter": ((a + 1) % 4, b),
        "swap": (b, a),
        "snapshot": (input_value, a),
        "copy": (b, b),
        "sequential": (input_value, input_value),
        "hold_derived": (a, a ^ input_value),
        "overwritten_snapshot": (input_value, input_value),
        "partial_hold": (input_value, b),
    }[action]


lines = ["module { simulation.design @oracle { simulation.scope.decl 0"]
for root in range(3):
    lines.append(f"simulation.storage.decl {root} in 0 : i2 design")
for index, (control, yes, no, switch) in enumerate(cases):
    branch = (
        f"cf.switch %{control} : i2, [default: ^no, 0: ^yes]"
        if switch else
        f"%test = arith.cmpi eq, %{control}, %zero : i2\ncf.cond_br %test, ^yes, ^no"
    )
    lines.append(f"""
simulation.code_unit.decl {index + 1} in 0 always_comb hierarchy "f{index}"
simulation.func @f{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}})
    attributes {{entry_kind = 4 : i32, code_unit_id = {index + 1} : i64}} {{
  %in = simulation.context.storage %ctx[0] : !simulation.ref<i2>
  %a = simulation.context.storage %ctx[1] : !simulation.ref<i2>
  %b = simulation.context.storage %ctx[2] : !simulation.ref<i2>
  %zero = arith.constant 0 : i2
  %one = arith.constant 1 : i2
  cf.br ^body
^body:
  %input = simulation.ref.load %in : !simulation.ref<i2> -> i2
  %old_a = simulation.ref.load %a : !simulation.ref<i2> -> i2
  %old_b = simulation.ref.load %b : !simulation.ref<i2> -> i2
  %increment = arith.addi %old_a, %one : i2
  %input_xor = arith.xori %input, %one : i2
  %xor_ab = arith.xori %old_a, %old_b : i2
  %derived = arith.xori %old_a, %input : i2
  {branch}
^yes:
  {emit_action(yes, 'yes')}
^no:
  {emit_action(no, 'no')}
^exit:
  simulation.suspend.change %in to ^body : !simulation.ref<i2>
}}
""")
lines.append("} }")
result = subprocess.run(
    [sys.argv[1], "--test-obelisk-activation-regions=idempotence=true reaching-values=true inputs=true", "-o", "/dev/null"],
    input="\n".join(lines), text=True, capture_output=True, check=True,
)
proofs = re.findall(r"regions @f(\d+) valid=1\n  idempotent=([01])", result.stderr)
assert len(proofs) == len(cases), result.stderr
accepted = rejected_nonidempotent = 0
for index, admitted in proofs:
    case = cases[int(index)]
    idempotent = True
    for input_value, a, b in itertools.product(range(4), repeat=3):
        first = execute(case, input_value, a, b)
        second = execute(case, input_value, *first)
        if first != second:
            idempotent = False
            assert admitted == "0", (case, input_value, a, b, first, second)
    accepted += admitted == "1"
    rejected_nonidempotent += not idempotent
# Prevent a vacuous all-reject implementation from passing the oracle.
assert accepted >= 100, accepted
assert rejected_nonidempotent >= 100, rejected_nonidempotent
print(f"Checked {len(cases)} transitions over 64 states each: "
      f"{accepted} sound proofs, {rejected_nonidempotent} nonidempotent rejections")


def summaries(output):
    result = {}
    for index, body in re.findall(r"regions @f(\d+) valid=1\n(.*?)(?=regions @|\Z)", output, re.S):
        assert "  values valid=1" in body or "  inputs valid=1" in body, body
        ranges = {}
        for key in ("value-input", "final-input", "may-write"):
            line = re.search(rf"^  {key}(.*)$", body, re.M).group(1)
            ranges[key] = {
                (int(root), bit)
                for root, low, high in re.findall(r"s(\d+)\[(\d+),(\d+)\)", line)
                for bit in range(int(low), int(high))
            }
        result[int(index)] = ranges
    return result


full = summaries(result.stderr)
result = subprocess.run(
    [sys.argv[1], "--test-obelisk-activation-regions=input-dependencies=true inputs=true", "-o", "/dev/null"],
    input="\n".join(lines), text=True, capture_output=True, check=True,
)
inputs = summaries(result.stderr)
assert len(full) == len(inputs) == len(cases)
states = list(itertools.product(range(4), repeat=3))
for index, case in enumerate(cases):
    assert full[index]["value-input"] == inputs[index]["value-input"], case
    written = {root for root, _ in full[index]["may-write"]}
    traces = {s: execute(case, *s, publications=True) for s in states}
    final = {s: tuple(value for root, value in enumerate(execute(case, *s), 1)
                      if root in written) for s in states}
    for state in states:
        for root, bit in itertools.product(range(3), range(2)):
            other = list(state)
            other[root] ^= 1 << bit
            other = tuple(other)
            if traces[state] != traces[other]:
                assert (root, bit) in full[index]["value-input"], (
                    "missing publication input", case, state, other, root, bit)
            if final[state] != final[other]:
                assert (root, bit) in full[index]["final-input"], (
                    "missing final input", case, state, other, root, bit)
print(f"Checked publication and final-state dependencies for {len(cases)} programs")
