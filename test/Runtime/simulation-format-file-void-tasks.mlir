// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// A string is an unsigned packed byte sequence for numeric conversions. For
// %s, embedded null bytes become spaces, while the 0 flag suppresses leading
// null bytes. File close and flush are void system tasks, so a stale descriptor
// records an I/O error without terminating the process. MCD zero writes nowhere.
// CHECK: :00610062:610062: a b:a b:
// CHECK-NEXT: value=12         97
// CHECK-NEXT: 7
// CHECK-NEXT: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @format_file_void_tasks {
    simulation.scope.decl 0 hierarchy "format_file_void_tasks"
    simulation.code_unit.decl 9970000 in 0 root_initializer
        hierarchy "format_file_void_tasks.root"
    simulation.code_unit.decl 9970001 in 0 initial
        hierarchy "format_file_void_tasks.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64} {
      %path = simulation.bytes.constant "/dev/null"
      %mode = simulation.bytes.constant "w"
      %fd = simulation.file.open %ctx, %path, %mode :
          (!simulation.context, !simulation.bytes, !simulation.bytes) -> i32
      simulation.file.close %ctx, %fd : (!simulation.context, i32) -> ()
      simulation.file.close %ctx, %fd : (!simulation.context, i32) -> ()
      simulation.file.flush %ctx, %fd : (!simulation.context, i32) -> ()

      %format = simulation.bytes.constant ":%x:%0x:%s:%0s:"
      %s0 = simulation.string.literal "\00a\00b"
      %s1 = simulation.string.literal "\00a\00b"
      %s2 = simulation.string.literal "\00a\00b"
      %s3 = simulation.string.literal "\00a\00b"
      %formatted = simulation.string.output_format %ctx(
          %format, %s0, %s1, %s2, %s3)
          radix = <decimal> flags = [32, 8, 8, 8, 8] :
          !simulation.bytes, !simulation.string, !simulation.string,
          !simulation.string, !simulation.string
      %as_string = simulation.bytes.constant "%0s"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%as_string, %formatted)
          newline = true radix = <decimal> flags = [0, 8] :
          !simulation.bytes, !simulation.string

      // IEEE 1800-2017 21.2.1: a designated format consumes the arguments
      // named by its conversions. Remaining output-list items continue with
      // the default radix instead of being discarded.
      %designated_format = simulation.bytes.constant "value=%0d"
      %twelve = arith.constant 12 : i32
      %ninety_seven = arith.constant 97 : i32
      %designated = simulation.string.output_format %ctx(
          %designated_format, %twelve, %ninety_seven)
          radix = <decimal> flags = [32, 0, 1] :
          !simulation.bytes, i32, i32
      simulation.display %ctx to %stdout(%as_string, %designated)
          newline = true radix = <decimal> flags = [0, 8] :
          !simulation.bytes, !simulation.string

      // IEEE 1800-2017 21.2.1.3: an explicit conversion width overrides the
      // minimum width installed by $timeformat.
      %units = arith.constant -9 : i32
      %digits = arith.constant 0 : i32
      %suffix = simulation.bytes.constant "ns"
      %minimum_width = arith.constant 5 : i32
      simulation.time.format %ctx, %units, %digits, %suffix, %minimum_width :
          (!simulation.context, i32, i32, !simulation.bytes, i32) -> ()
      %time_format = simulation.bytes.constant "%7t"
      %time = simulation.logic.constant 0 : i64, 0 : i64 :
          !simulation.logic<64>
      %formatted_time = simulation.string.output_format %ctx(
          %time_format, %time) radix = <decimal> flags = [32, 0]
          {time_multiplier = 1 : i64, time_precision = -9 : i32} :
          !simulation.bytes, !simulation.logic<64>
      %time_width = simulation.string.length %formatted_time :
          (!simulation.string) -> i64
      %decimal = simulation.bytes.constant "%0d"
      simulation.display %ctx to %stdout(%decimal, %time_width)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i64

      %zero = arith.constant 0 : i32
      %discarded = simulation.bytes.constant "must not be written"
      simulation.display %ctx to %zero(%discarded)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      %passed = simulation.bytes.constant "PASSED"
      simulation.display %ctx to %stdout(%passed)
          newline = true radix = <decimal> flags = [0] : !simulation.bytes
      simulation.return
    }
  }
}
