// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// %c consumes exactly one byte and advances the input cursor. The following
// whitespace prefix must therefore begin at the byte after that character.
// CHECK: 61:62:6364:1:3:6:1:1:1
// CHECK-NEXT: 01xz

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @string_scan {
    obelisk_sim.scope.decl 0 hierarchy "string_scan"
    obelisk_sim.code_unit.decl 9980000 in 0 root_initializer
        hierarchy "string_scan.root"
    obelisk_sim.code_unit.decl 9980001 in 0 initial
        hierarchy "string_scan.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980001 : i64} {
      %input = obelisk_sim.string.literal "a b cd e"
      %zero = arith.constant 0 : i32
      %x_field, %x_cursor, %x_ok = obelisk_sim.string.scan_field
          %input, %zero {prefix = "", specifier = 99 : i32} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %x = obelisk_sim.string.to_packed %x_field :
          (!obelisk_sim.string) -> i8
      %y_field, %y_cursor, %y_ok = obelisk_sim.string.scan_field
          %input, %x_cursor {prefix = " ", specifier = 99 : i32} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %y = obelisk_sim.string.to_packed %y_field :
          (!obelisk_sim.string) -> i8
      %z_field, %z_cursor, %z_ok = obelisk_sim.string.scan_field
          %input, %y_cursor {prefix = " ", specifier = 115 : i32} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %z = obelisk_sim.string.to_packed %z_field :
          (!obelisk_sim.string) -> i24

      %format = obelisk_sim.bytes.constant
          "%0h:%0h:%0h:%0d:%0d:%0d:%0d:%0d:%0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %x, %y, %z, %x_cursor, %y_cursor, %z_cursor,
          %x_ok, %y_ok, %z_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i8, i8, i24, i32, i32, i32, i32, i32, i32

      %logic_input = obelisk_sim.string.literal "01xz"
      %logic_field, %logic_cursor, %logic_ok = obelisk_sim.string.scan_field
          %logic_input, %zero {prefix = "", specifier = 98 : i32} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %logic = obelisk_sim.string.parse_logic %logic_field radix = 2 :
          !obelisk_sim.logic<64>
      %logic_format = obelisk_sim.bytes.constant "%04b"
      obelisk_sim.display %ctx to %stdout(%logic_format, %logic)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<64>
      obelisk_sim.return
    }
  }
}
