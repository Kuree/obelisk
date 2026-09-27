// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 1800-2017 21.3.4.3 makes $fscanf consume from the descriptor's current
// stream position. The enabled input prevents later conversions from reading
// after an earlier mismatch, and EOF remains distinct from a match failure.

// CHECK-LABEL: llvm.func @scan(
// CHECK: llvm.call @obelisk_rt_v1_file_scan_field
// CHECK-SAME: %arg1, %arg2
// CHECK: llvm.load
// CHECK: llvm.load
// CHECK: llvm.load

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @file_scan {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "file_scan.scan"

    simulation.func private @scan(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %descriptor: i32 {simulation.capture_kind = 2 : i32},
        %enabled: i32 {simulation.capture_kind = 2 : i32})
        -> (!simulation.string, i32, i32)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %field, %ok, %eof = simulation.file.scan_field
          %ctx, %descriptor, %enabled
          {prefix = " ", specifier = 100 : i32, width = 0 : i64} :
          (!simulation.context, i32, i32) -> (!simulation.string, i32, i32)
      simulation.return %field, %ok, %eof : !simulation.string, i32, i32
    }
  }
}
