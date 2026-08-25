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
// CHECK-NEXT: 4:1:4:1:51:5:1
// CHECK-NEXT: 0:1
// CHECK-NEXT: 1.10:1.30:-1.30:1.25:1300.00

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
          %input, %zero {prefix = "", specifier = 99 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %x = obelisk_sim.string.to_packed %x_field :
          (!obelisk_sim.string) -> i8
      %y_field, %y_cursor, %y_ok = obelisk_sim.string.scan_field
          %input, %x_cursor {prefix = " ", specifier = 99 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %y = obelisk_sim.string.to_packed %y_field :
          (!obelisk_sim.string) -> i8
      %z_field, %z_cursor, %z_ok = obelisk_sim.string.scan_field
          %input, %y_cursor {prefix = " ", specifier = 115 : i32, width = 0 : i64} :
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
          %logic_input, %zero {prefix = "", specifier = 98 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %logic = obelisk_sim.string.parse_logic %logic_field radix = 2 :
          !obelisk_sim.logic<64>
      %logic_format = obelisk_sim.bytes.constant "%04b"
      obelisk_sim.display %ctx to %stdout(%logic_format, %logic)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<64>

      // %m matches its literal prefix but consumes no field bytes. The next
      // %c therefore reads Q at the cursor immediately after "tag=".
      %hierarchy_input = obelisk_sim.string.literal "tag=Q"
      %hierarchy_field, %hierarchy_cursor, %hierarchy_ok =
          obelisk_sim.string.scan_field %hierarchy_input, %zero
          {prefix = "tag=", specifier = 109 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %second_hierarchy_field, %second_hierarchy_cursor, %second_hierarchy_ok =
          obelisk_sim.string.scan_field %hierarchy_input, %hierarchy_cursor
          {prefix = "", specifier = 77 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %character_field, %character_cursor, %character_ok =
          obelisk_sim.string.scan_field %hierarchy_input,
          %second_hierarchy_cursor
          {prefix = "", specifier = 99 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %character = obelisk_sim.string.to_packed %character_field :
          (!obelisk_sim.string) -> i8
      %hierarchy_format = obelisk_sim.bytes.constant
          "%0d:%0d:%0d:%0d:%0h:%0d:%0d"
      obelisk_sim.display %ctx to %stdout(
          %hierarchy_format, %hierarchy_cursor, %hierarchy_ok,
          %second_hierarchy_cursor, %second_hierarchy_ok, %character,
          %character_cursor, %character_ok) newline = true radix = 10
          flags = [0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i32, i32, i32, i32, i8, i32, i32

      %empty_input = obelisk_sim.string.literal ""
      %empty_field, %empty_cursor, %empty_ok = obelisk_sim.string.scan_field
          %empty_input, %zero
          {prefix = "", specifier = 109 : i32, width = 0 : i64} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.string, i32, i32)
      %empty_format = obelisk_sim.bytes.constant "%0d:%0d"
      obelisk_sim.display %ctx to %stdout(
          %empty_format, %empty_cursor, %empty_ok)
          newline = true radix = 10 flags = [0, 0, 0] :
          !obelisk_sim.bytes, i32, i32

      // A %t field is parsed as a real before this O(1) scaling operation.
      // The first value observes the inactive default; later values prove
      // design-global $timeformat changes and units on both sides of the
      // caller's 1 ns unit (10 design ticks at 100 ps precision).
      %default_input = arith.constant 10.5 : f64
      %default_time = obelisk_sim.time.scan_scale %ctx, %default_input
          time_multiplier = 10 time_precision = -10
      %units_ns = arith.constant -9 : i32
      %one_digit = arith.constant 1 : i32
      %empty_suffix = obelisk_sim.bytes.constant ""
      %width = arith.constant 0 : i32
      "obelisk_sim.time.format"(%ctx, %units_ns, %one_digit, %empty_suffix,
          %width) : (!obelisk_sim.context, i32, i32, !obelisk_sim.bytes, i32) -> ()
      %positive_input = arith.constant 1.25 : f64
      %negative_input = arith.constant -1.25 : f64
      %positive_time = obelisk_sim.time.scan_scale %ctx, %positive_input
          time_multiplier = 10 time_precision = -10
      %negative_time = obelisk_sim.time.scan_scale %ctx, %negative_input
          time_multiplier = 10 time_precision = -10
      %units_ps = arith.constant -12 : i32
      %zero_digits = arith.constant 0 : i32
      "obelisk_sim.time.format"(%ctx, %units_ps, %zero_digits, %empty_suffix,
          %width) : (!obelisk_sim.context, i32, i32, !obelisk_sim.bytes, i32) -> ()
      %fine_input = arith.constant 1250.0 : f64
      %fine_time = obelisk_sim.time.scan_scale %ctx, %fine_input
          time_multiplier = 10 time_precision = -10
      %units_us = arith.constant -6 : i32
      "obelisk_sim.time.format"(%ctx, %units_us, %one_digit, %empty_suffix,
          %width) : (!obelisk_sim.context, i32, i32, !obelisk_sim.bytes, i32) -> ()
      %coarse_time = obelisk_sim.time.scan_scale %ctx, %positive_input
          time_multiplier = 10 time_precision = -10
      %time_format = obelisk_sim.bytes.constant
          "%.2f:%.2f:%.2f:%.2f:%.2f"
      obelisk_sim.display %ctx to %stdout(
          %time_format, %default_time, %positive_time, %negative_time,
          %fine_time, %coarse_time)
          newline = true radix = 10 flags = [0, 4, 4, 4, 4, 4] :
          !obelisk_sim.bytes, f64, f64, f64, f64, f64
      obelisk_sim.return
    }
  }
}
