"""Check termination marks against exhaustive execution of small bit vectors."""

import itertools
import re
import subprocess
import sys


width = 3
modulus = 1 << width
predicates = ("eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge")


def continues(predicate, value, bound):
    if predicate.startswith("s"):
        value = value if value < modulus // 2 else value - modulus
        bound = bound if bound < modulus // 2 else bound - modulus
    comparison = predicate if predicate in ("eq", "ne") else predicate[1:]
    return {
        "eq": value == bound,
        "ne": value != bound,
        "lt": value < bound,
        "le": value <= bound,
        "gt": value > bound,
        "ge": value >= bound,
    }[comparison]


def terminates(predicate, start, bound, stride, subtract):
    # This oracle follows the actual finite state machine. It deliberately
    # does not use a closed-form trip count or the optimizer's arithmetic proof.
    seen = set()
    value = start
    while continues(predicate, value, bound):
        if value in seen:
            return False
        seen.add(value)
        value = (value - stride if subtract else value + stride) % modulus
    return True


cases = list(itertools.product(predicates, range(modulus), range(modulus),
                               range(modulus), (False, True)))
lines = ["module { obelisk_sim.design @proof { obelisk_sim.scope.decl 0"]
for index, _ in enumerate(cases):
    lines.append(f'obelisk_sim.code_unit.decl {index + 1} in 0 initial hierarchy "f{index}"')
for index, (predicate, start, bound, stride, subtract) in enumerate(cases):
    step_op = "subi" if subtract else "addi"
    lines.append(f"""
obelisk_sim.func @f{index}(%ctx: !obelisk_sim.context {{obelisk_sim.capture_kind = 0 : i32}})
    attributes {{entry_kind = 1 : i32, code_unit_id = {index + 1} : i64}} {{
  %start = arith.constant {start} : i{width}
  cf.br ^head(%start : i{width})
^head(%i: i{width}):
  %bound = arith.constant {bound} : i{width}
  %test = arith.cmpi {predicate}, %i, %bound : i{width}
  cf.cond_br %test, ^body, ^exit
^body:
  %step = arith.constant {stride} : i{width}
  %next = arith.{step_op} %i, %step : i{width}
  cf.br ^head(%next : i{width})
^exit:
  obelisk_sim.return
}}
""")
lines.append("} }")
result = subprocess.run(
    [sys.argv[1], "--pass-pipeline=builtin.module(obelisk_sim.design("
     "obelisk_sim.func(obelisk-sim-mark-bounded-loops)))"],
    input="\n".join(lines), text=True, capture_output=True)
assert result.returncode == 0, result.stderr
functions = re.findall(r"obelisk_sim.func @f(\d+)\((.*?)(?=obelisk_sim.func|\Z)",
                       result.stdout, re.S)
assert len(functions) == len(cases), (len(functions), len(cases))
marked_count = 0
for index, body in functions:
    case = cases[int(index)]
    marked = "obelisk_sim.bounded_loop_latch" in body
    finite = terminates(*case)
    assert not marked or finite, f"Nonterminating loop incorrectly marked: {case}"
    # Equality reachability and zero-trip recognition should be complete.
    if case[0] in ("eq", "ne") or not continues(case[0], case[1], case[2]):
        assert marked == finite, f"Missed exact termination proof: {case}"
    marked_count += marked
assert marked_count > len(cases) // 2
print(f"Checked {len(cases)} loops; {marked_count} sound termination marks")
