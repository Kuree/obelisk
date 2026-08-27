// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=FEATURE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

!nibbles = !obelisk_sim.unpacked_array<2 : 0 x !obelisk_sim.logic<4>>
!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "head", type = i4, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "tail", type = !nibbles, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @fixed_bitstream {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.cast"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.cast_again"
    obelisk_sim.func @cast(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !record {obelisk_sim.capture_kind = 2 : i32})
        -> (!obelisk_sim.logic<16>, i16)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %logic = obelisk_sim.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
                     1, 0, 4, 0, 0, 4,
                     4294967298, 4, 3, 4, 4, 4,
                     1, 0, 4, 0, 0, 4] : (!record) -> !obelisk_sim.logic<16>
      %bits = obelisk_sim.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
                     1, 0, 4, 0, 0, 4,
                     4294967298, 4, 3, 4, 4, 4,
                     1, 0, 4, 0, 0, 4] : (!record) -> i16
      obelisk_sim.return %logic, %bits : !obelisk_sim.logic<16>, i16
    }

    obelisk_sim.func @cast_again(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !record {obelisk_sim.capture_kind = 2 : i32}) -> i16
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %bits = obelisk_sim.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!record) -> i16
      obelisk_sim.return %bits : i16
    }
  }
}

// NATIVE-COUNT-1: llvm.mlir.global internal constant @__obelisk_aggregate_bitstream_plan_
// NATIVE-COUNT-3: llvm.call @obelisk_rt_v1_aggregate_export_bitstream
// FEATURE: obelisk.feature.container_bitstream
// BYTECODE: intrinsic 0: id=0x00010465 inputs=2 outputs=1 flags=0
// BYTECODE-COUNT-3: site {{[0-9]+}}: signature=0 id=0x00010465 inputs=
