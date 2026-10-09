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
lines = ["module { simulation.design @proof { simulation.scope.decl 0"]
for index, _ in enumerate(cases):
    lines.append(f'simulation.code_unit.decl {index + 1} in 0 initial hierarchy "f{index}"')
for index, (predicate, start, bound, stride, subtract) in enumerate(cases):
    step_op = "subi" if subtract else "addi"
    lines.append(f"""
simulation.func @f{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}})
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
  simulation.return
}}
""")
lines.append("} }")
result = subprocess.run(
    [sys.argv[1], "--pass-pipeline=builtin.module(simulation.design("
     "simulation.func(obelisk-sim-mark-bounded-loops)))"],
    input="\n".join(lines), text=True, capture_output=True)
assert result.returncode == 0, result.stderr
functions = re.findall(r"simulation.func @f(\d+)\((.*?)(?=simulation.func|\Z)",
                       result.stdout, re.S)
assert len(functions) == len(cases), (len(functions), len(cases))
marked_count = 0
for index, body in functions:
    case = cases[int(index)]
    marked = "schedule.bounded_loop_latch" in body
    finite = terminates(*case)
    assert not marked or finite, f"Nonterminating loop incorrectly marked: {case}"
    # Equality reachability and zero-trip recognition should be complete.
    if case[0] in ("eq", "ne") or not continues(case[0], case[1], case[2]):
        assert marked == finite, f"Missed exact termination proof: {case}"
    marked_count += marked
assert marked_count > len(cases) // 2
print(f"Checked {len(cases)} loops; {marked_count} sound termination marks")

# The initializer now varies across an enclosing induction. Exhaust every
# nonempty interval of two-bit initializers, predicate, bound and modular step.
# Check each admitted nested loop against execution from *every* initializer,
# including interior residue classes and entries that execute zero iterations.
width = 2
modulus = 1 << width
intervals = [(low, high) for low in range(modulus)
             for high in range(low, modulus)]
nested_cases = list(itertools.product(predicates, intervals, range(modulus),
                                      range(modulus), (False, True)))
lines = ["module { simulation.design @nested_proof { simulation.scope.decl 0"]
for index, _ in enumerate(nested_cases):
    lines.append(f'simulation.code_unit.decl {index + 1} in 0 initial hierarchy "f{index}"')
for index, (predicate, (low, high), bound, stride, subtract) in enumerate(nested_cases):
    step_op = "subi" if subtract else "addi"
    lines.append(f"""
simulation.func @f{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}})
    attributes {{entry_kind = 1 : i32, code_unit_id = {index + 1} : i64}} {{
  %low = arith.constant {low} : i4
  %end = arith.constant {high + 1} : i4
  %one = arith.constant 1 : i4
  cf.br ^outer(%low : i4)
^outer(%i: i4):
  %more = arith.cmpi ult, %i, %end : i4
  cf.cond_br %more, ^enter, ^exit
^enter:
  %start = arith.trunci %i : i4 to i{width}
  cf.br ^head(%start : i{width})
^head(%k: i{width}):
  %bound = arith.constant {bound} : i{width}
  %test = arith.cmpi {predicate}, %k, %bound : i{width}
  cf.cond_br %test, ^body, ^advance
^body:
  %step = arith.constant {stride} : i{width}
  %next = arith.{step_op} %k, %step : i{width}
  cf.br ^head(%next : i{width})
^advance:
  %inext = arith.addi %i, %one : i4
  cf.br ^outer(%inext : i4)
^exit:
  simulation.return
}}
""")
lines.append("} }")
result = subprocess.run(
    [sys.argv[1], "--pass-pipeline=builtin.module(simulation.design("
     "simulation.func(obelisk-sim-mark-bounded-loops)))"],
    input="\n".join(lines), text=True, capture_output=True)
assert result.returncode == 0, result.stderr
functions = re.findall(r"simulation.func @f(\d+)\((.*?)(?=simulation.func|\Z)",
                       result.stdout, re.S)
assert len(functions) == len(nested_cases)
marked_count = 0
for index, body in functions:
    predicate, (low, high), bound, stride, subtract = nested_cases[int(index)]
    marks = body.count("schedule.bounded_loop_latch")
    assert marks in (0, 2), (nested_cases[int(index)], marks)
    if marks:
        assert all(terminates(predicate, start, bound, stride, subtract)
                   for start in range(low, high + 1)), nested_cases[int(index)]
        marked_count += 1
assert marked_count > len(nested_cases) // 4
print(f"Checked {len(nested_cases)} initializer intervals; {marked_count} sound marks")

# Independently vary the bound instead. In particular, an extreme bound may
# be reached without wrap while another stride residue or signed bound cannot.
# Also check correlated initializers/bounds with wrapping invariant arithmetic.
# LRM 11.6.1 sizes arithmetic; 12.7.1 tests before each iteration; 12.8 permits
# early exits but provides no assumption that an arbitrary recurrence finishes.
bound_cases = list(itertools.product(predicates, intervals, range(modulus),
                                     range(modulus), (False, True)))
correlated_cases = list(itertools.product(predicates, intervals, (0, 1, 3),
                                          range(modulus), (False, True)))
for correlated, matrix in ((False, bound_cases), (True, correlated_cases)):
    lines = ["module { simulation.design @bound_proof { simulation.scope.decl 0"]
    for index, (predicate, (low, high), parameter, stride, subtract) in enumerate(matrix):
        initial = "%outer_value" if correlated else "%parameter"
        bound = "%sum" if correlated else "%outer_value"
        step_op = "subi" if subtract else "addi"
        lines.append(f"""
simulation.code_unit.decl {index + 1} in 0 initial hierarchy "f{index}"
simulation.func @f{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}})
    attributes {{entry_kind = 1 : i32, code_unit_id = {index + 1} : i64}} {{
  %low = arith.constant {low} : i4
  %end = arith.constant {high + 1} : i4
  %one = arith.constant 1 : i4
  %parameter = arith.constant {parameter} : i{width}
  cf.br ^outer(%low : i4)
^outer(%i: i4):
  %more = arith.cmpi ult, %i, %end : i4
  cf.cond_br %more, ^enter, ^exit
^enter:
  %outer_value = arith.trunci %i : i4 to i{width}
  %sum = arith.addi %outer_value, %parameter : i{width}
  cf.br ^head({initial} : i{width})
^head(%k: i{width}):
  %test = arith.cmpi {predicate}, %k, {bound} : i{width}
  cf.cond_br %test, ^body, ^advance
^body:
  %step = arith.constant {stride} : i{width}
  %next = arith.{step_op} %k, %step : i{width}
  cf.br ^head(%next : i{width})
^advance:
  %inext = arith.addi %i, %one : i4
  cf.br ^outer(%inext : i4)
^exit:
  simulation.return
}}
""")
    lines.append("} }")
    result = subprocess.run(
        [sys.argv[1], "--pass-pipeline=builtin.module(simulation.design("
         "simulation.func(obelisk-sim-mark-bounded-loops)))"],
        input="\n".join(lines), text=True, capture_output=True, check=True)
    functions = re.findall(r"simulation.func @f(\d+)\((.*?)(?=simulation.func|\Z)",
                           result.stdout, re.S)
    assert len(functions) == len(matrix)
    marked_count = 0
    for index, body in functions:
        predicate, (low, high), parameter, stride, subtract = matrix[int(index)]
        marks = body.count("schedule.bounded_loop_latch")
        assert marks in (0, 2), (matrix[int(index)], marks)
        if marks:
            for outer in range(low, high + 1):
                start = outer if correlated else parameter
                bound = (outer + parameter) % modulus if correlated else outer
                assert terminates(predicate, start, bound, stride, subtract), (
                    correlated, matrix[int(index)], outer)
            marked_count += 1
    assert marked_count > len(matrix) // 10, (correlated, marked_count)
    kind = "correlated intervals" if correlated else "bound intervals"
    print(f"Checked {len(matrix)} {kind}; {marked_count} sound marks")

# Packed four-state counters initialized with known planes. Arithmetic may be
# widened, but assignment truncates back to the two-bit recurrence. Compare
# every admitted proof to that narrow finite state machine, not a wider one.
packed_cases = list(itertools.product(
    ("eq", "ne", "ult", "ule", "ugt", "uge"), range(modulus), range(modulus),
    range(modulus), (False, True), (width, width + 2)))
lines = ["!counter = !simulation.packed_array<1 : 0 x !simulation.logic<1>>",
         "module { simulation.design @packed_proof { simulation.scope.decl 0"]
for index, _ in enumerate(packed_cases):
    lines.append(f'simulation.code_unit.decl {index + 1} in 0 initial hierarchy "f{index}"')
for index, (predicate, start, bound, stride, subtract, wide) in enumerate(packed_cases):
    step_op = "sub" if subtract else "add"
    lines.append(f"""
simulation.func @f{index}(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}})
    attributes {{entry_kind = 1 : i32, code_unit_id = {index + 1} : i64}} {{
  %bits = simulation.logic.constant {start} : i{width}, 0 : i{width} : !simulation.logic<{width}>
  %start = simulation.packed.unflatten %bits : (!simulation.logic<{width}>) -> !counter
  cf.br ^head(%start : !counter)
^head(%i: !counter):
  %narrow = simulation.packed.flatten %i : (!counter) -> !simulation.logic<{width}>
  %value = simulation.logic.resize %narrow signed = false : !simulation.logic<{width}> -> !simulation.logic<{wide}>
  %bound = simulation.logic.constant {bound} : i{wide}, 0 : i{wide} : !simulation.logic<{wide}>
  %compare = simulation.logic.compare {predicate} %value, %bound : (!simulation.logic<{wide}>, !simulation.logic<{wide}>) -> !simulation.logic<1>
  %test = simulation.logic.is_true %compare : !simulation.logic<1>
  cf.cond_br %test, ^body, ^exit
^body:
  %step = simulation.logic.constant {stride} : i{wide}, 0 : i{wide} : !simulation.logic<{wide}>
  %sum = simulation.logic.binary {step_op} %value, %step : !simulation.logic<{wide}>
  %nextbits = simulation.logic.resize %sum signed = false : !simulation.logic<{wide}> -> !simulation.logic<{width}>
  %next = simulation.packed.unflatten %nextbits : (!simulation.logic<{width}>) -> !counter
  cf.br ^head(%next : !counter)
^exit:
  simulation.return
}}
""")
lines.append("} }")
result = subprocess.run(
    [sys.argv[1], "--pass-pipeline=builtin.module(simulation.design("
     "simulation.func(obelisk-sim-mark-bounded-loops)))"],
    input="\n".join(lines), text=True, capture_output=True)
assert result.returncode == 0, result.stderr
functions = re.findall(r"simulation.func @f(\d+)\((.*?)(?=simulation.func|\Z)",
                       result.stdout, re.S)
assert len(functions) == len(packed_cases)
marked_count = 0
for index, body in functions:
    case = packed_cases[int(index)][:5]
    marked = "schedule.bounded_loop_latch" in body
    finite = terminates(*case)
    assert not marked or finite, f"Packed recurrence wraps: {case}"
    if case[0] in ("eq", "ne") or not continues(case[0], case[1], case[2]):
        assert marked == finite, f"Missed exact packed proof: {case}"
    marked_count += marked
assert marked_count > len(packed_cases) // 2
print(f"Checked {len(packed_cases)} packed recurrences; {marked_count} sound marks")
