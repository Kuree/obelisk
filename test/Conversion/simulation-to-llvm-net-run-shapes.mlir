// RUN: obelisk-opt %s --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines --canonicalize --cse \
// RUN:   | FileCheck %s

// Run resolution tabulates every driver state combination, so a bit with more
// contributions than the table bound keeps the scalar resolver: one strength
// combination per contribution.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @five_drivers {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "five_drivers"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 0 : !simulation.logic<1> design
    simulation.func @five_drivers(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<1> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @five_drivers
// CHECK-COUNT-5: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// A reversed alias walks down as the run walks up. Net 1 bits 0-63 and 64-71
// are net 0 bits 71-8 and 7-0, so each 64-bit chunk is one bit-reversed slice
// of net 0's storage at the mirrored offset.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @reversed_offsets {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "reversed_offsets"
    simulation.net.decl 0 in 0 : !simulation.logic<72> design
    simulation.net.decl 1 in 0 : !simulation.logic<72> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[71] width 72 reversed = true
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<72> design
    simulation.func @reversed_offsets(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<72> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<72>>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<72>>, !simulation.logic<72>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @reversed_offsets
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK-DAG: %[[C0:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK-DAG: %[[C8:.*]] = llvm.mlir.constant(8 : i64) : i64
// CHECK-DAG: %[[C64:.*]] = llvm.mlir.constant(64 : i64) : i64
// CHECK: %[[LOW:.*]] = llvm.trunc %arg1 : i72 to i64
// CHECK: %[[REVLOW:.*]] = llvm.intr.bitreverse(%[[LOW]]) : (i64) -> i64
// CHECK: %[[HIGH:.*]] = llvm.trunc %{{.*}} : i72 to i8
// CHECK: %[[REVHIGH:.*]] = llvm.intr.bitreverse(%[[HIGH]]) : (i8) -> i8
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %[[C8]], %[[C64]], %{{.*}}, %{{.*}}, %[[REVLOW]], %{{.*}})
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %[[C0]], %[[C64]], %{{.*}}, %{{.*}}, %[[LOW]], %{{.*}})
// CHECK: %[[REVHIGH64:.*]] = llvm.zext %[[REVHIGH]] : i8 to i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %[[C0]], %[[C8]], %{{.*}}, %{{.*}}, %[[REVHIGH64]], %{{.*}})
// CHECK: %[[HIGH64:.*]] = llvm.zext %[[HIGH]] : i8 to i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %[[C64]], %[[C8]], %{{.*}}, %{{.*}}, %[[HIGH64]], %{{.*}})
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// -----

// Integer nets have no unknown plane: an x or z resolution publishes 0
// (IEEE 1800-2017 6.11.2) with a zero unknown plane on every member.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @integer_member {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "integer_member"
    simulation.net.decl 0 in 0 : i8 design
    simulation.net.decl 1 in 0 : i8 design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 8 reversed = false
    simulation.driver.decl 0 in 0 drives 0 : i8 design
    simulation.driver.decl 1 in 0 drives 1 : i8 design
    simulation.func @integer_member(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<i8>
      simulation.driver.drive %driver = %value : !simulation.driver<i8>, i8
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @integer_member
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: %[[VALUES:.*]] = llvm.mlir.addressof @__obelisk_state_value
// CHECK: %[[OLD:.*]] = llvm.load %[[VALUES]] {{.*}} -> i8
// CHECK-NEXT: %[[KNOWN:.*]] = llvm.xor %{{.*}}, %{{.*}} : i8
// CHECK-NEXT: %[[VALUE:.*]] = llvm.and %{{.*}}, %[[KNOWN]] : i8
// CHECK: %[[NEW:.*]] = llvm.zext %[[VALUE]] : i8 to i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ZERO]], %[[NEW]], %[[ZERO]])
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ZERO]], %[[NEW]], %[[ZERO]])
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK: llvm.return

// -----

// A two-state drive's own contribution has a constant zero unknown plane, so
// run resolution cofactors it away: against the other strong wire driver, the
// net's value plane is one AND of the two value planes.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @two_state_own {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "two_state_own"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<8> design
    simulation.func @two_state_own(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bits: i8 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<8>>
      %value = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @two_state_own
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: %[[OTHERV:.*]] = llvm.load %{{.*}} -> i8
// CHECK: llvm.load %{{.*}} -> i8
// CHECK-NOT: llvm.load
// CHECK: %[[VALUE:.*]] = llvm.and %[[OTHERV]], %arg1 : i8
// CHECK: %[[NEW:.*]] = llvm.zext %[[VALUE]] : i8 to i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[NEW]], %{{.*}})
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return

// -----

// IEEE 1800-2017 6.6.5: tri1 adds a pull1 contribution, so a z driver leaves
// 1 and only an x driver leaves x. Tri0 is the mirror image.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @tri1 {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "tri1"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design {
      resolution_kind = 6 : i32
    }
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design
        {resolution_kind = 6 : i32}
    simulation.func @tri1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<4> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<4>>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<4>>, !simulation.logic<4>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @tri1
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: %[[NOTV:.*]] = llvm.xor %arg1, %{{.*}} : i4
// CHECK-NEXT: %[[UNKNOWN:.*]] = llvm.and %arg2, %[[NOTV]] : i4
// CHECK: %[[NEWV:.*]] = llvm.zext %arg1 : i4 to i64
// CHECK-NEXT: %[[NEWU:.*]] = llvm.zext %[[UNKNOWN]] : i4 to i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[NEWV]], %[[NEWU]])
// CHECK: llvm.return

// -----

// A supply0 net's implicit supply 0 overrides every strong driver, and a
// strong 1 on wor overrides any other strong value (Table 6-4): neither
// publication depends on the other drivers.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @constant_kinds {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "supply0"
    simulation.code_unit.decl 2 in 0 function hierarchy "wor"
    simulation.net.decl 0 in 0 : !simulation.logic<2> design {
      resolution_kind = 7 : i32
    }
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
        {resolution_kind = 7 : i32}
    simulation.net.decl 1 in 0 : !simulation.logic<2> design {
      resolution_kind = 4 : i32
    }
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<2> design
        {resolution_kind = 4 : i32}
    simulation.driver.decl 2 in 0 drives 1 : !simulation.logic<2> design
        {resolution_kind = 4 : i32}
    simulation.func @supply0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<2> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<2>>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<2>>, !simulation.logic<2>
      simulation.return
    }
    simulation.func @wor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %driver = simulation.context.driver %ctx[1] :
          !simulation.driver<!simulation.logic<2>>
      %ones = simulation.logic.constant 3 : i2, 0 : i2 : !simulation.logic<2>
      simulation.driver.drive %driver = %ones :
          !simulation.driver<!simulation.logic<2>>, !simulation.logic<2>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @supply0
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK-NOT: llvm.and
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ZERO]], %[[ZERO]])
// CHECK: llvm.return

// CHECK-LABEL: llvm.func @wor
// CHECK-DAG: %[[ONES:.*]] = llvm.mlir.constant(3 : i64) : i64
// CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK-NOT: llvm.and
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition({{.*}}, %[[ONES]], %[[ZERO]])
// CHECK: llvm.return
