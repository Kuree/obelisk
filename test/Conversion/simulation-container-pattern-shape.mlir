// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

// IEEE 1800-2017 21.2.1.7 requires each unpacked array nested in a managed
// container to retain its assignment-pattern shape. The container descriptor
// records the two-element extent and four-byte stride explicitly.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_pattern_shape {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.create"
    obelisk_sim.func @create(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %one = arith.constant 1 : i64
      %array = obelisk_sim.container.create %one {
        type_id = 123 : i64, element_kind = 7 : i32,
        element_flags = 0 : i32, value_size = 8 : i64,
        alignment = 1 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<
          !obelisk_sim.unpacked_array<0 : 1 x i32>>
      obelisk_sim.return
    }
  }
}

// The eight little-endian words are version, dimension count, leaf kind,
// leaf flags, leaf byte size, leaf bit width, extent, and byte stride.
// NATIVE: llvm.mlir.global internal constant @__obelisk_element_pattern_123("\01\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00{{[ ]}}\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00")
// NATIVE: llvm.call @obelisk_rt_v1_container_create_typed_pattern

// BYTECODE: constants: {{.*}}01000000000000000100000000000000010000000000000000000000000000000400000000000000200000000000000002000000000000000400000000000000
