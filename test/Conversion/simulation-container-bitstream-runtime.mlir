// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | FileCheck %s --check-prefix=LOWER
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// CHECK: 1 1 1 1
// LOWER: llvm.call @obelisk_rt_v1_container_bitstream_link_anchor

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_bitstream_runtime {
    obelisk_sim.scope.decl 0 hierarchy "container_bitstream_runtime"
    obelisk_sim.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "container_bitstream_runtime.root"
    obelisk_sim.code_unit.decl 9910001 in 0 initial
        hierarchy "container_bitstream_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      %three = arith.constant 3 : i64
      %a = obelisk_sim.logic.constant 12 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %b = obelisk_sim.logic.constant 13 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %c = obelisk_sim.logic.constant 14 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %source = obelisk_sim.container.create %three {
        type_id = 9910002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>
      obelisk_sim.container.write %source, %zero, %a :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %source, %one, %b :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %source, %two, %c :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      %packed = obelisk_sim.container.export_bitstream %source :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>) ->
          !obelisk_sim.logic<24>
      %expected = obelisk_sim.logic.constant 789774 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %order_ok = obelisk_sim.logic.compare case_eq %packed, %expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // A four-state stream converted to a two-state target maps X and Z to 0.
      %xz = obelisk_sim.logic.constant -1 : i8, 15 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.container.write %source, %zero, %xz :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      %two_state = obelisk_sim.container.export_bitstream %source :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>) -> i24
      %two_state_expected = arith.constant 15731982 : i24
      %xz_ok = arith.cmpi eq, %two_state, %two_state_expected : i24

      %bits = obelisk_sim.container.create %three {
        type_id = 9910003 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i8>
      %v0 = arith.constant 18 : i8
      %v1 = arith.constant 52 : i8
      %v2 = arith.constant 86 : i8
      obelisk_sim.container.write %bits, %zero, %v0 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      obelisk_sim.container.write %bits, %one, %v1 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      obelisk_sim.container.write %bits, %two, %v2 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      %four_state = obelisk_sim.container.export_bitstream %bits :
          (!obelisk_sim.dynamic_array<i8>) -> !obelisk_sim.logic<24>
      %four_state_expected = obelisk_sim.logic.constant 1193046 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %state_ok = obelisk_sim.logic.compare case_eq
          %four_state, %four_state_expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // Shift a queue head far enough that the final append wraps physically.
      %queue = obelisk_sim.container.create %zero {
        type_id = 9910002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 2 : i32, bound = -1 : i64
      } : (i64) -> !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      obelisk_sim.container.write %queue, %zero, %a :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %queue, %one, %b :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %queue, %two, %c :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.queue.delete %queue[%zero] :
          !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      %d = obelisk_sim.logic.constant 15 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.container.write %queue, %two, %d :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.queue.delete %queue[%zero] :
          !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      obelisk_sim.container.write %queue, %two, %a :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      %queue_packed = obelisk_sim.container.export_bitstream %queue :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>) ->
          !obelisk_sim.logic<24>
      %queue_expected = obelisk_sim.logic.constant 921356 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %queue_ok = obelisk_sim.logic.compare case_eq
          %queue_packed, %queue_expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1
      %format = obelisk_sim.bytes.constant "%0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %order_ok, %xz_ok, %state_ok, %queue_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
