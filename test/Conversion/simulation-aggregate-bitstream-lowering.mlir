// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=FEATURE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

!nibbles = !simulation.unpacked_array<2 : 0 x !simulation.logic<4>>
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "head", type = i4, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "tail", type = !nibbles, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @fixed_bitstream {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.cast"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.cast_again"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.uncast"
    simulation.func @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !record {simulation.capture_kind = 2 : i32})
        -> (!simulation.logic<16>, i16)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %logic = simulation.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
                     1, 0, 4, 0, 0, 4,
                     4294967298, 4, 3, 4, 4, 4,
                     1, 0, 4, 0, 0, 4] : (!record) -> !simulation.logic<16>
      %bits = simulation.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
                     1, 0, 4, 0, 0, 4,
                     4294967298, 4, 3, 4, 4, 4,
                     1, 0, 4, 0, 0, 4] : (!record) -> i16
      simulation.return %logic, %bits : !simulation.logic<16>, i16
    }

    simulation.func @cast_again(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !record {simulation.capture_kind = 2 : i32}) -> i16
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %bits = simulation.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!record) -> i16
      simulation.return %bits : i16
    }

    simulation.func @uncast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.logic<16> {simulation.capture_kind = 2 : i32})
        -> !record
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %result = simulation.aggregate.import_bitstream %source plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           3, 0, 4, 0, 0, 4] : (!simulation.logic<16>) -> !record
      simulation.return %result : !record
    }
  }
}

// NATIVE-COUNT-1: llvm.mlir.global internal constant @__obelisk_aggregate_bitstream_plan_
// NATIVE-COUNT-3: llvm.call @obelisk_rt_v1_aggregate_export_bitstream
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_aggregate_import_bitstream
// FEATURE: obelisk.feature.container_bitstream
// BYTECODE: intrinsic 0: id=0x00010465 inputs=2 outputs=1 flags=0
// BYTECODE: intrinsic 1: id=0x00010466 inputs=2 outputs=1 flags=0
// BYTECODE-COUNT-3: site {{[0-9]+}}: signature=0 id=0x00010465 inputs=
// BYTECODE-COUNT-1: site {{[0-9]+}}: signature=1 id=0x00010466 inputs=
