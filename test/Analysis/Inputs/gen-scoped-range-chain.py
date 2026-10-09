#!/usr/bin/env python3
"""Independent selectors must not accumulate in every later CFG snapshot."""

print("""!array = !simulation.unpacked_array<0 : 15 x i8>
module {
  simulation.design @chain {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "selectors"
    simulation.func @selectors(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64, test.scoped_max = 16 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %mask = arith.constant 15 : i8
      cf.br ^block0""")
for index in range(512):
    print(f"""    ^block{index}:
      %index{index} = arith.andi %seed, %mask : i8
      %view{index} = simulation.ref.array_element %root[%index{index}] : (!simulation.ref<!array>, i8) -> !simulation.ref<i8>
      %value{index} = simulation.ref.load %view{index} : !simulation.ref<i8> -> i8
      simulation.ref.store %value{index} to %view{index} : i8, !simulation.ref<i8>
      cf.br ^block{index + 1}""")
print("""    ^block512:
      simulation.return
    }
  }
}""")
