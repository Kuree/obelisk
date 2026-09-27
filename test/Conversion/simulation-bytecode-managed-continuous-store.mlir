// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_continuous_store {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.dynamic_array<i32> design
        hierarchy "top.value"
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "top.comb"

    simulation.func private @comb(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!simulation.dynamic_array<i32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %size = arith.constant 2 : i64
      %array = simulation.container.create %size {
        alignment = 4 : i64,
        bit_width = 32 : i64,
        bound = 0 : i64,
        container_kind = #simulation.container_kind<dynamic_array>,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 4 : i64
      } : (i64) -> !simulation.dynamic_array<i32>
      simulation.ref.store %array to %value {
        simulation.continuous_store
      } : !simulation.dynamic_array<i32>,
          !simulation.ref<!simulation.dynamic_array<i32>>
      simulation.return
    }
  }
}

// A managed value has its own change detection and publication path. It must
// not acquire the packed-state continuous-store flag, which is not a legal
// operand combination for STORE_STATE.
// CHECK: opcode=28 flags=0
