// RUN: talos-opt %s --test-talos-activation-memory -o /dev/null 2>&1 | FileCheck %s
// RUN: talos-opt %s --test-talos-access-footprints='print-handles' -o /dev/null 2>&1 | FileCheck %s --check-prefix=HANDLES

// A block without an executability proof remains conservative, including
// when a range bound could in principle prove it dead. A live opaque reference
// invalidates the summary; dead callees must not reintroduce opaque effects.
module {
  simulation.design @executability {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "unproved_range"
    simulation.code_unit.decl 2 in 0 function hierarchy "range_live"
    simulation.code_unit.decl 3 in 0 function hierarchy "call_dead"
    simulation.code_unit.decl 4 in 0 function hierarchy "read_dead"
    simulation.code_unit.decl 5 in 0 function hierarchy "caller"
    simulation.func private @external(!simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32}

    // CHECK-LABEL: activation @unproved_range valid=0
    // CHECK: live-in{{$}}
    // HANDLES-LABEL: unproved_range
    // HANDLES: block 1 executable=1
    // HANDLES: resource=0
    simulation.func @unproved_range(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %n: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %mask = arith.constant 7 : i32
      %bounded = arith.andi %n, %mask : i32
      %bad = arith.cmpi ugt, %bounded, %mask : i32
      cf.cond_br %bad, ^dead, ^done
    ^dead:
      %opaque = ub.poison : !simulation.ref<i32>
      %v = simulation.ref.load %opaque : !simulation.ref<i32> -> i32
      simulation.call @external(%ctx) : (!simulation.context) -> ()
      cf.br ^done
    ^done:
      simulation.return
    }

    // CHECK-LABEL: activation @range_live valid=0
    // HANDLES-LABEL: range_live
    // HANDLES: block 1 executable=1
    // HANDLES: resource=0
    simulation.func @range_live(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %n: i32 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %mask = arith.constant 7 : i32
      %bounded = arith.andi %n, %mask : i32
      %possible = arith.cmpi eq, %bounded, %mask : i32
      cf.cond_br %possible, ^live, ^done
    ^live:
      %opaque = ub.poison : !simulation.ref<i32>
      %v = simulation.ref.load %opaque : !simulation.ref<i32> -> i32
      cf.br ^done
    ^done:
      simulation.return
    }

    // A read-only call summary skips dead side effects in its callee too.
    // CHECK-LABEL: activation @call_dead valid=1
    simulation.func @call_dead(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %false = arith.constant false
      cf.cond_br %false, ^dead, ^done
    ^dead:
      %opaque = ub.poison : !simulation.ref<i32>
      %v = simulation.ref.load %opaque : !simulation.ref<i32> -> i32
      simulation.call @external(%ctx) : (!simulation.context) -> ()
      cf.br ^done
    ^done:
      simulation.return
    }

    // CHECK-LABEL: activation @read_dead valid=1
    simulation.func @read_dead(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %false = arith.constant false
      cf.cond_br %false, ^dead, ^done
    ^dead:
      %opaque = ub.poison : !simulation.ref<i32>
      %v = simulation.ref.load %opaque : !simulation.ref<i32> -> i32
      cf.br ^done
    ^done:
      simulation.return
    }

    // CHECK-LABEL: activation @caller valid=1
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      simulation.call @call_dead(%ctx) : (!simulation.context) -> ()
      simulation.call @read_dead(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}
