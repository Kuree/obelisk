// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=auto | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// CHECK: 1 1 1 1

!nibbles = !obelisk_sim.unpacked_array<2 : 0 x !obelisk_sim.logic<4>>
!ascending = !obelisk_sim.unpacked_array<0 : 3 x !obelisk_sim.logic<4>>
!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "head", type = i4, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "tail", type = !nibbles, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @aggregate_bitstream_runtime {
    obelisk_sim.scope.decl 0 hierarchy "aggregate_bitstream_runtime"
    obelisk_sim.code_unit.decl 9920000 in 0 root_initializer
        hierarchy "aggregate_bitstream_runtime.root"
    obelisk_sim.code_unit.decl 9920001 in 0 initial
        hierarchy "aggregate_bitstream_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %head = arith.constant 10 : i4
      %first = obelisk_sim.logic.constant 15 : i4, 3 : i4 :
          !obelisk_sim.logic<4>
      %second = obelisk_sim.logic.constant 12 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %third = obelisk_sim.logic.constant 13 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %tail = obelisk_sim.aggregate.construct %first, %second, %third :
          (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>,
           !obelisk_sim.logic<4>) -> !nibbles
      %source = obelisk_sim.aggregate.construct %head, %tail :
          (i4, !nibbles) -> !record

      %logic = obelisk_sim.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!record) -> !obelisk_sim.logic<16>
      %expected_logic = obelisk_sim.logic.constant 45005 : i16, 768 : i16 :
          !obelisk_sim.logic<16>
      %logic_ok = obelisk_sim.logic.compare case_eq %logic, %expected_logic :
          (!obelisk_sim.logic<16>, !obelisk_sim.logic<16>) -> i1

      %bits = obelisk_sim.aggregate.export_bitstream %source plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!record) -> i16
      %expected_bits = arith.constant 44237 : i16
      %bits_ok = arith.cmpi eq, %bits, %expected_bits : i16

      // Ascending and descending array ranges both use ordinal order: the
      // first element maps to the most-significant destination bits.
      %known_first = obelisk_sim.logic.constant 11 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %known_head = obelisk_sim.logic.from_bits %head :
          i4 -> !obelisk_sim.logic<4>
      %ascending = obelisk_sim.aggregate.construct
          %known_head, %known_first, %second, %third :
          (!obelisk_sim.logic<4>, !obelisk_sim.logic<4>,
           !obelisk_sim.logic<4>, !obelisk_sim.logic<4>) -> !ascending
      %known = obelisk_sim.aggregate.export_bitstream %ascending plan
          [5407724624, 2, 16, 16,
           4294967298, 0, 4, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!ascending) -> !obelisk_sim.logic<16>
      %expected_known = obelisk_sim.logic.constant 43981 : i16, 0 : i16 :
          !obelisk_sim.logic<16>
      %order_ok = obelisk_sim.logic.compare case_eq %known, %expected_known :
          (!obelisk_sim.logic<16>, !obelisk_sim.logic<16>) -> i1

      // Import is the exact inverse plan. It also performs the final
      // four-state coercion independently for each mixed-state leaf.
      %import_input = obelisk_sim.logic.constant 45005 : i16, 62208 : i16 :
          !obelisk_sim.logic<16>
      %round_trip = obelisk_sim.aggregate.import_bitstream %import_input plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           3, 0, 4, 0, 0, 4] : (!obelisk_sim.logic<16>) -> !record
      %round_bits = obelisk_sim.aggregate.export_bitstream %round_trip plan
          [5407724624, 3, 16, 16,
           1, 0, 4, 0, 0, 4,
           4294967298, 4, 3, 4, 4, 4,
           1, 0, 4, 0, 0, 4] : (!record) -> !obelisk_sim.logic<16>
      %expected_round = obelisk_sim.logic.constant 4045 : i16, 768 : i16 :
          !obelisk_sim.logic<16>
      %round_ok = obelisk_sim.logic.compare case_eq %round_bits, %expected_round :
          (!obelisk_sim.logic<16>, !obelisk_sim.logic<16>) -> i1

      %format = obelisk_sim.bytes.constant "%0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %logic_ok, %bits_ok, %order_ok, %round_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
