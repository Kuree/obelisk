#!/usr/bin/env python3
"""Entry capture handles need one value node, not a proxy at every CFG block."""

count = 128
print("module {\n  simulation.design @chain {\n    simulation.scope.decl 0")
for index in range(count + 1):
    print(f"    simulation.storage.decl {index} in 0 : i1 design")
print(f"""    simulation.code_unit.decl 1 in 0 always_comb hierarchy "chain"
    simulation.func @chain(%ctx: !simulation.context {{simulation.capture_kind = 0 : i32}}) attributes {{entry_kind = 4 : i32, code_unit_id = 1 : i64, test.activation_max_value_nodes = {16 * count} : i64}} {{""")
for index in range(count + 1):
    print(f"      %root{index} = simulation.context.storage %ctx[{index}] : !simulation.ref<i1>")
print(f"""      %zero = arith.constant false
      %one = arith.constant true
      cf.br ^body
    ^body:
      %flag = simulation.ref.load %root{count} : !simulation.ref<i1> -> i1
      cf.br ^block0""")
for index in range(count):
    print(f"""    ^block{index}:
      cf.cond_br %flag, ^yes{index}, ^no{index}
    ^yes{index}:
      simulation.ref.store %one to %root{index} : i1, !simulation.ref<i1>
      cf.br ^block{index + 1}
    ^no{index}:
      simulation.ref.store %zero to %root{index} : i1, !simulation.ref<i1>
      cf.br ^block{index + 1}""")
print(f"""    ^block{count}:
      simulation.suspend.change %root{count} to ^body : !simulation.ref<i1>
    }}
  }}
}}""")
