// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// CHECK: native-aot eligible=true fully=false
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @native
// CHECK-NEXT: bytecode @managed bb0
// CHECK-NEXT: bytecode @real_reactive bb0
// CHECK-NEXT: reason managed or string state is present
// CHECK-NEXT: reason real-valued reactive state requires bytecode

module {
  obelisk_sim.design @hybrid {
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "native"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "managed"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "real_reactive"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native = obelisk_sim.spawn @native(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %managed = obelisk_sim.spawn @managed(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %real = obelisk_sim.spawn @real_reactive(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @native(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @managed(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %text = obelisk_sim.string.literal "bytecode"
      obelisk_sim.return
    }

    obelisk_sim.func @real_reactive(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0.0 : f64
      obelisk_sim.return
    }
  }
}
