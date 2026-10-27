// RUN: talos-opt %s --test-talos-activation-regions='thread-control=true' -o /dev/null 2>&1 | FileCheck %s

// Without a loop-carried increment, thread-control cannot prove termination.
// CHECK: regions @break_dispatch valid=0 reason=termination

module {
  simulation.design @shared_latch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "break_dispatch"
    simulation.func @break_dispatch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %zero = arith.constant 0 : i32
      %one = arith.constant 0 : i32
      %limit = arith.constant 8 : i32
      %true = arith.constant true
      cf.br ^activate
    ^activate:
      %stop = simulation.ref.load %input : !simulation.ref<i32> -> i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^dispatch(%zero, %zero : i32, i32)
    ^body:
      simulation.ref.store %i to %output : i32, !simulation.ref<i32>
      %stop_now = arith.cmpi eq, %i, %stop : i32
      %continue = arith.xori %stop_now, %true : i1
      %tag = arith.extui %continue : i1 to i32
      cf.cond_br %stop_now, ^dispatch(%zero, %tag : i32, i32), ^increment
    ^increment:
      %next = arith.addi %i, %one : i32
      cf.br ^dispatch(%next, %tag : i32, i32)
    ^dispatch(%value: i32, %flag: i32):
      %again = arith.trunci %flag : i32 to i1
      cf.cond_br %again, ^loop(%value : i32), ^wait
    ^wait:
      simulation.suspend.change %input to ^activate : !simulation.ref<i32>
    }
  }
}
