// RUN: obelisk-opt %s --test-obelisk-region-value-dependencies -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

// A native word region exposes its execute-once control flow and captures to
// the same interface-based dependency analysis used for SCF regions.
// CHECK-LABEL: values @word_body valid=1
// CHECK: arith.xori result 0: arg1 arg2{{$}}
// CHECK: simulation.ref.store effect: arg1 arg2 arg0{{$}}
func.func @word_body(%output: !simulation.ref<i8>, %a: i8, %b: i8) {
  schedule.word.region 8 {
    %x = arith.xori %a, %b {test.values} : i8
    simulation.ref.store %x to %output {test.values} : i8, !simulation.ref<i8>
    schedule.word.yield
  }
  return
}

// Generic sparse range propagation must enter all reachable CFG blocks in
// the region. This does not turn its inner CFG into multiple region entries.
// RUN: obelisk-opt %s --test-obelisk-integer-interfaces -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE
func.func @word_cfg(%condition: i1, %input: !simulation.logic<8>, %control: !simulation.logic<1>, %output: !simulation.ref<i32>) {
  schedule.word.region 32 {
    cf.cond_br %condition, ^left, ^right
  ^left:
    %count = simulation.logic.count_bits %input matching %control {test.fact = "count-in-region"} : (!simulation.logic<8>, !simulation.logic<1>) -> i32
    cf.br ^exit(%count : i32)
  ^right:
    %zero = arith.constant 0 : i32
    cf.br ^exit(%zero : i32)
  ^exit(%merged: i32):
    %one = arith.constant 1 : i32
    %adjusted = arith.addi %merged, %one {test.fact = "joined-in-region"} : i32
    simulation.ref.store %adjusted to %output : i32, !simulation.ref<i32>
    schedule.word.yield
  }
  return
}
// RANGE: count-in-region umin=0 umax=8
// RANGE: joined-in-region umin=1 umax=9
