// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' \
// RUN:   | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)),convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)),encode-obelisk-sim-to-bytecode)' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py --state \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 6.8 and 29.3.2-29.3.4: a sequential UDP's output is its
// variable state, whose default is X unless an explicit known initializer
// changes it. The initial driver contribution must therefore be X, not the
// ordinary undriven Z sentinel.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @sequential_udp_default_state {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "top.udp"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @udp(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.primitive_name = "udp_default_x",
                    obelisk_sim.udp_metadata = {
                      is_edge_sensitive = false, is_sequential = true,
                      name = "udp_default_x",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "input"],
                      table_edges = array<i64: 0>, table_inputs = ["?"],
                      table_outputs = array<i64: 45>,
                      table_states = array<i64: 63>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.input", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.out", argument = 2, kind = lvalue_only, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {
          node_id = 4 : i64, referenced_path = "top.input",
          referenced_symbol = @input, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}

// LOWER: obelisk_sim.driver.drive_changed {{.*}} {obelisk_sim.initial_driver_x}

// Independently addressable storage, net, and driver roots occupy separate
// bytes. The unresolved net is Z; the storage and certified UDP driver are X.
// NATIVE: llvm.mlir.global internal @__obelisk_state_unknown("\01\01\01\00\00\00\00\00\00\00\00")
// NATIVE: llvm.mlir.global internal @__obelisk_state_value("\00\01\00\00\00\00\00\00\00\00\00")

// BYTECODE: kind=driver flags=17337
