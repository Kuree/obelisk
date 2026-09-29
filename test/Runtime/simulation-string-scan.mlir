// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// %c consumes exactly one byte and advances the input cursor. The following
// whitespace prefix must therefore begin at the byte after that character.
// CHECK: 61:62:6364:1:3:6:1:1:1
// CHECK-NEXT: 01xz
// CHECK-NEXT: 0:0x:1:0z:3:7:11:15
// CHECK-NEXT: 4:1:4:1:51:5:1
// CHECK-NEXT: 0:1
// CHECK-NEXT: 1.10:1.30:-1.30:1.25:1300.00

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @string_scan {
    simulation.scope.decl 0 hierarchy "string_scan"
    simulation.code_unit.decl 9980000 in 0 root_initializer
        hierarchy "string_scan.root"
    simulation.code_unit.decl 9980001 in 0 initial
        hierarchy "string_scan.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980001 : i64} {
      %input = simulation.string.literal "a b cd e"
      %zero = arith.constant 0 : i32
      %x_field, %x_cursor, %x_ok = simulation.string.scan_field
          %input, %zero {prefix = "", specifier = 99 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %x = simulation.string.to_packed %x_field :
          (!simulation.string) -> i8
      %y_field, %y_cursor, %y_ok = simulation.string.scan_field
          %input, %x_cursor {prefix = " ", specifier = 99 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %y = simulation.string.to_packed %y_field :
          (!simulation.string) -> i8
      %z_field, %z_cursor, %z_ok = simulation.string.scan_field
          %input, %y_cursor {prefix = " ", specifier = 115 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %z = simulation.string.to_packed %z_field :
          (!simulation.string) -> i24

      %format = simulation.bytes.constant
          "%0h:%0h:%0h:%0d:%0d:%0d:%0d:%0d:%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %x, %y, %z, %x_cursor, %y_cursor, %z_cursor,
          %x_ok, %y_ok, %z_ok)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i8, i8, i24, i32, i32, i32, i32, i32, i32

      %logic_input = simulation.string.literal "01xz"
      %logic_field, %logic_cursor, %logic_ok = simulation.string.scan_field
          %logic_input, %zero {prefix = "", specifier = 98 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %logic = simulation.string.parse_logic %logic_field radix = <binary> :
          !simulation.logic<64>
      %logic_format = simulation.bytes.constant "%04b"
      simulation.display %ctx to %stdout(%logic_format, %logic)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<64>

      // Table 21-6 mnemonic and digit-pair examples normalize to their
      // four-state logic component while each conversion consumes exactly
      // three bytes. Uppercase %V has identical scan-field semantics.
      %strength_input = simulation.string.literal "520 65X PuH HiZ"
      %range_field, %range_cursor, %range_ok =
          simulation.string.scan_field %strength_input, %zero
          {prefix = "", specifier = 118 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %range = simulation.string.parse_logic %range_field radix = <binary> :
          !simulation.logic<64>
      %unequal_field, %unequal_cursor, %unequal_ok =
          simulation.string.scan_field %strength_input, %range_cursor
          {prefix = " ", specifier = 86 : i32, width = 3 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %unequal = simulation.string.parse_logic %unequal_field radix = <binary> :
          !simulation.logic<64>
      %high_field, %high_cursor, %high_ok =
          simulation.string.scan_field %strength_input, %unequal_cursor
          {prefix = " ", specifier = 118 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %high = simulation.string.parse_logic %high_field radix = <binary> :
          !simulation.logic<64>
      %impedance_field, %impedance_cursor, %impedance_ok =
          simulation.string.scan_field %strength_input, %high_cursor
          {prefix = " ", specifier = 118 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %impedance = simulation.string.parse_logic %impedance_field radix = <binary> :
          !simulation.logic<64>
      %strength_format = simulation.bytes.constant
          "%0b:%0b:%0b:%0b:%0d:%0d:%0d:%0d"
      simulation.display %ctx to %stdout(
          %strength_format, %range, %unequal, %high, %impedance,
          %range_cursor, %unequal_cursor, %high_cursor, %impedance_cursor)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, !simulation.logic<64>,
          !simulation.logic<64>, !simulation.logic<64>,
          !simulation.logic<64>, i32, i32, i32, i32

      // %m matches its literal prefix but consumes no field bytes. The next
      // %c therefore reads Q at the cursor immediately after "tag=".
      %hierarchy_input = simulation.string.literal "tag=Q"
      %hierarchy_field, %hierarchy_cursor, %hierarchy_ok =
          simulation.string.scan_field %hierarchy_input, %zero
          {prefix = "tag=", specifier = 109 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %second_hierarchy_field, %second_hierarchy_cursor, %second_hierarchy_ok =
          simulation.string.scan_field %hierarchy_input, %hierarchy_cursor
          {prefix = "", specifier = 77 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %character_field, %character_cursor, %character_ok =
          simulation.string.scan_field %hierarchy_input,
          %second_hierarchy_cursor
          {prefix = "", specifier = 99 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %character = simulation.string.to_packed %character_field :
          (!simulation.string) -> i8
      %hierarchy_format = simulation.bytes.constant
          "%0d:%0d:%0d:%0d:%0h:%0d:%0d"
      simulation.display %ctx to %stdout(
          %hierarchy_format, %hierarchy_cursor, %hierarchy_ok,
          %second_hierarchy_cursor, %second_hierarchy_ok, %character,
          %character_cursor, %character_ok) newline = true radix = <decimal>
          flags = [0, 0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i32, i32, i32, i32, i8, i32, i32

      %empty_input = simulation.string.literal ""
      %empty_field, %empty_cursor, %empty_ok = simulation.string.scan_field
          %empty_input, %zero
          {prefix = "", specifier = 109 : i32, width = 0 : i64} :
          (!simulation.string, i32) -> (!simulation.string, i32, i32)
      %empty_format = simulation.bytes.constant "%0d:%0d"
      simulation.display %ctx to %stdout(
          %empty_format, %empty_cursor, %empty_ok)
          newline = true radix = <decimal> flags = [0, 0, 0] :
          !simulation.bytes, i32, i32

      // A %t field is parsed as a real before this O(1) scaling operation.
      // The first value observes the inactive default; later values prove
      // design-global $timeformat changes and units on both sides of the
      // caller's 1 ns unit (10 design ticks at 100 ps precision).
      %default_input = arith.constant 10.5 : f64
      %default_time = simulation.time.scan_scale %ctx, %default_input
          time_multiplier = 10 time_precision = -10
      %units_ns = arith.constant -9 : i32
      %one_digit = arith.constant 1 : i32
      %empty_suffix = simulation.bytes.constant ""
      %width = arith.constant 0 : i32
      "simulation.time.format"(%ctx, %units_ns, %one_digit, %empty_suffix,
          %width) : (!simulation.context, i32, i32, !simulation.bytes, i32) -> ()
      %positive_input = arith.constant 1.25 : f64
      %negative_input = arith.constant -1.25 : f64
      %positive_time = simulation.time.scan_scale %ctx, %positive_input
          time_multiplier = 10 time_precision = -10
      %negative_time = simulation.time.scan_scale %ctx, %negative_input
          time_multiplier = 10 time_precision = -10
      %units_ps = arith.constant -12 : i32
      %zero_digits = arith.constant 0 : i32
      "simulation.time.format"(%ctx, %units_ps, %zero_digits, %empty_suffix,
          %width) : (!simulation.context, i32, i32, !simulation.bytes, i32) -> ()
      %fine_input = arith.constant 1250.0 : f64
      %fine_time = simulation.time.scan_scale %ctx, %fine_input
          time_multiplier = 10 time_precision = -10
      %units_us = arith.constant -6 : i32
      "simulation.time.format"(%ctx, %units_us, %one_digit, %empty_suffix,
          %width) : (!simulation.context, i32, i32, !simulation.bytes, i32) -> ()
      %coarse_time = simulation.time.scan_scale %ctx, %positive_input
          time_multiplier = 10 time_precision = -10
      %time_format = simulation.bytes.constant
          "%.2f:%.2f:%.2f:%.2f:%.2f"
      simulation.display %ctx to %stdout(
          %time_format, %default_time, %positive_time, %negative_time,
          %fine_time, %coarse_time)
          newline = true radix = <decimal> flags = [0, 4, 4, 4, 4, 4] :
          !simulation.bytes, f64, f64, f64, f64, f64
      simulation.return
    }
  }
}
