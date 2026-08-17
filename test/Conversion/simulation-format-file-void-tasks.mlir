// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
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
// CHECK-NEXT: PASSED

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @format_file_void_tasks {
    obelisk_sim.scope.decl 0 hierarchy "format_file_void_tasks"
    obelisk_sim.code_unit.decl 9970000 in 0 root_initializer
        hierarchy "format_file_void_tasks.root"
    obelisk_sim.code_unit.decl 9970001 in 0 initial
        hierarchy "format_file_void_tasks.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64} {
      %path = obelisk_sim.bytes.constant "/dev/null"
      %mode = obelisk_sim.bytes.constant "w"
      %fd = obelisk_sim.file.open %ctx, %path, %mode :
          (!obelisk_sim.context, !obelisk_sim.bytes, !obelisk_sim.bytes) -> i32
      obelisk_sim.file.close %ctx, %fd : (!obelisk_sim.context, i32) -> ()
      obelisk_sim.file.close %ctx, %fd : (!obelisk_sim.context, i32) -> ()
      obelisk_sim.file.flush %ctx, %fd : (!obelisk_sim.context, i32) -> ()

      %format = obelisk_sim.bytes.constant ":%x:%0x:%s:%0s:"
      %s0 = obelisk_sim.string.literal "\00a\00b"
      %s1 = obelisk_sim.string.literal "\00a\00b"
      %s2 = obelisk_sim.string.literal "\00a\00b"
      %s3 = obelisk_sim.string.literal "\00a\00b"
      %formatted = obelisk_sim.string.output_format %ctx(
          %format, %s0, %s1, %s2, %s3)
          radix = 10 flags = [32, 8, 8, 8, 8] :
          !obelisk_sim.bytes, !obelisk_sim.string, !obelisk_sim.string,
          !obelisk_sim.string, !obelisk_sim.string
      %as_string = obelisk_sim.bytes.constant "%0s"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%as_string, %formatted)
          newline = true radix = 10 flags = [0, 8] :
          !obelisk_sim.bytes, !obelisk_sim.string

      %zero = arith.constant 0 : i32
      %discarded = obelisk_sim.bytes.constant "must not be written"
      obelisk_sim.display %ctx to %zero(%discarded)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      %passed = obelisk_sim.bytes.constant "PASSED"
      obelisk_sim.display %ctx to %stdout(%passed)
          newline = true radix = 10 flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }
  }
}
