// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt \
// RUN:     -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=auto | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// CHECK: 1 1 1 1 1 1 1
// CHECK: wake 1

!record = !simulation.unpacked_struct<[
  #simulation.field<name = "q", type = !simulation.queue<i8, 0>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "s", type = !simulation.string, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "tail", type = !simulation.unpacked_array<0 : 1 x i8>, ordinal = 2, packedOffset = 0>
]>
!tail = !simulation.unpacked_array<0 : 1 x i8>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @recursive_bitstream_runtime {
    simulation.scope.decl 0 hierarchy "recursive_bitstream_runtime"
    simulation.code_unit.decl 9920000 in 0 root_initializer
        hierarchy "recursive_bitstream_runtime.root"
    simulation.code_unit.decl 9920001 in 0 initial
        hierarchy "recursive_bitstream_runtime.initial"
    simulation.code_unit.decl 9920007 in 0 always
        hierarchy "recursive_bitstream_runtime.observer"
    simulation.code_unit.decl 9920008 in 0 initial
        hierarchy "recursive_bitstream_runtime.mutator"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      %a = simulation.logic.constant 18 : i8, 0 : i8 :
          !simulation.logic<8>
      %b = simulation.logic.constant 52 : i8, 0 : i8 :
          !simulation.logic<8>
      %c = simulation.logic.constant 86 : i8, 0 : i8 :
          !simulation.logic<8>
      %d = simulation.logic.constant 120 : i8, 0 : i8 :
          !simulation.logic<8>
      %left = simulation.container.create %two {
        type_id = 9920002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!simulation.logic<8>>
      %right = simulation.container.create %two {
        type_id = 9920002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!simulation.logic<8>>
      simulation.container.write %left, %zero, %a :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %left, %one, %b :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %right, %zero, %c :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %right, %one, %d :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      %source = simulation.container.create %two {
        type_id = 9920003 : i64, element_kind = #simulation.element_kind<container_handle>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 1 : i64, bit_width = 0 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<
          !simulation.dynamic_array<!simulation.logic<8>>>
      simulation.container.write %source, %zero, %left :
          (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>, i64,
           !simulation.dynamic_array<!simulation.logic<8>>) -> ()
      simulation.container.write %source, %one, %right :
          (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>, i64,
           !simulation.dynamic_array<!simulation.logic<8>>) -> ()
      %packed, %matched, %watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>) ->
              (!simulation.logic<32>, i1, !simulation.managed_watch)
      %expected = simulation.logic.constant 305419896 : i32, 0 : i32 :
          !simulation.logic<32>
      %order_ok = simulation.logic.compare case_eq %packed, %expected :
          (!simulation.logic<32>, !simulation.logic<32>) -> i1

      %xz = simulation.logic.constant -1 : i8, 15 : i8 :
          !simulation.logic<8>
      simulation.container.write %right, %zero, %xz :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %source, %one, %right :
          (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>, i64,
           !simulation.dynamic_array<!simulation.logic<8>>) -> ()
      %bits, %bits_matched, %bits_watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>) ->
              (i32, i1, !simulation.managed_watch)
      %bits_expected = arith.constant 305459320 : i32
      %xz_ok = arith.cmpi eq, %bits, %bits_expected : i32

      %short, %short_matched, %short_watch =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>) ->
              (i24, i1, !simulation.managed_watch)
      %false = arith.constant false
      %mismatch_ok = arith.cmpi eq, %short_matched, %false : i1
      %queue = simulation.container.create %zero {
        type_id = 9920004 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<queue>, bound = -1 : i64
      } : (i64) -> !simulation.queue<i8, 0>
      %q0 = arith.constant 17 : i8
      %q1 = arith.constant 34 : i8
      simulation.container.write %queue, %zero, %q0 :
          (!simulation.queue<i8, 0>, i64, i8) -> ()
      simulation.container.write %queue, %one, %q1 :
          (!simulation.queue<i8, 0>, i64, i8) -> ()
      %string = simulation.string.literal "AB"
      %string_packed, %string_matched, %string_watch =
          simulation.recursive.export_bitstream %string {
            plan = array<i64: 5407724112, 1, 64, 0,
                4, 0, 0, 0, 64, 0>
          } : (!simulation.string) ->
              (i16, i1, !simulation.managed_watch)
      %string_expected = arith.constant 16706 : i16
      %string_order_ok = arith.cmpi eq, %string_packed, %string_expected : i16
      %string_ok = arith.andi %string_order_ok, %string_matched : i1
      %t0 = arith.constant 51 : i8
      %t1 = arith.constant 68 : i8
      %tail = simulation.aggregate.construct %t0, %t1 :
          (i8, i8) -> !tail
      %record = simulation.aggregate.construct %queue, %string, %tail :
          (!simulation.queue<i8, 0>, !simulation.string, !tail) -> !record
      %record_packed, %record_matched, %record_watch =
          simulation.recursive.export_bitstream %record {
            plan = array<i64: 5407724112, 5, 192, 0,
                4294967299, 0, 2, 0, 8, 0,
                1, 0, 8, 0, 0, 8,
                4, 64, 0, 0, 64, 0,
                4294967298, 128, 2, 8, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!record) -> (i48, i1, !simulation.managed_watch)
      %record_expected = arith.constant 18838821417796 : i48
      %record_order_ok = arith.cmpi eq, %record_packed, %record_expected : i48
      %record_ok = arith.andi %record_order_ok, %record_matched : i1
      %assoc = simulation.assoc.create {
        type_id = 9920005 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<unsigned>, key_width = 32 : i64
      } : () -> !simulation.assoc_array<i32, i8, false, false>
      %negative = arith.constant -1 : i32
      %key_zero = arith.constant 0 : i32
      %key_two = arith.constant 2 : i32
      %aa = arith.constant -86 : i8
      %bb = arith.constant -69 : i8
      %cc = arith.constant -52 : i8
      simulation.assoc.write %assoc, %key_two, %cc :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %assoc, %negative, %aa :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %assoc, %key_zero, %bb :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      %assoc_source = simulation.container.create %one {
        type_id = 9920006 : i64, element_kind = #simulation.element_kind<container_handle>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 1 : i64, bit_width = 0 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<
          !simulation.assoc_array<i32, i8, false, false>>
      simulation.container.write %assoc_source, %zero, %assoc :
          (!simulation.dynamic_array<
              !simulation.assoc_array<i32, i8, false, false>>, i64,
           !simulation.assoc_array<i32, i8, false, false>) -> ()
      %assoc_packed, %assoc_matched, %assoc_watch =
          simulation.recursive.export_bitstream %assoc_source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 3, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<
              !simulation.assoc_array<i32, i8, false, false>>) ->
              (i24, i1, !simulation.managed_watch)
      %assoc_expected = arith.constant 12307626 : i24
      %assoc_order_ok = arith.cmpi eq, %assoc_packed, %assoc_expected : i24
      %assoc_ok = arith.andi %assoc_order_ok, %assoc_matched : i1
      %all0 = arith.andi %order_ok, %matched : i1
      %all1 = arith.andi %xz_ok, %bits_matched : i1
      %format = simulation.bytes.constant "%0d %0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %all0, %all1, %mismatch_ok, %bits_matched, %record_ok,
          %assoc_ok, %string_ok) newline = true radix = <decimal>
          flags = [0, 0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1, i1, i1, i1, i1
      %observer = simulation.spawn @observer(%ctx, %source) :
          !simulation.context,
          !simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>> -> !simulation.process
      %mutator = simulation.spawn @mutator(%ctx, %source) :
          !simulation.context,
          !simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>> -> !simulation.process
      simulation.return
    }

    simulation.func private @observer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.dynamic_array<!simulation.dynamic_array<
            !simulation.logic<8>>> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9920007 : i64} {
      %before, %matched0, %watch =
          simulation.recursive.export_bitstream %source {
            observe,
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>) ->
              (i32, i1, !simulation.managed_watch)
      simulation.suspend.any %watch edges [0] to ^wake :
          !simulation.managed_watch
    ^wake:
      %after, %matched1, %unused =
          simulation.recursive.export_bitstream %source {
            plan = array<i64: 5407724112, 3, 64, 0,
                8589934595, 0, 1, 0, 64, 0,
                4294967299, 0, 1, 0, 8, 0,
                1, 0, 8, 0, 0, 8>
          } : (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>) ->
              (i32, i1, !simulation.managed_watch)
      %expected = arith.constant 313192568 : i32
      %value_ok = arith.cmpi eq, %after, %expected : i32
      %ok = arith.andi %value_ok, %matched1 : i1
      %format = simulation.bytes.constant "wake %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ok)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }

    simulation.func private @mutator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.dynamic_array<!simulation.dynamic_array<
            !simulation.logic<8>>> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920008 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^write
    ^write:
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %inner = simulation.container.read %source, %zero :
          (!simulation.dynamic_array<!simulation.dynamic_array<
              !simulation.logic<8>>>, i64) ->
          !simulation.dynamic_array<!simulation.logic<8>>
      %value = simulation.logic.constant -86 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.container.write %inner, %one, %value :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.return
    }
  }
}
