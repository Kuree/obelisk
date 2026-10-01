// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  func.func @strings(%input: !simulation.string, %bits: i24,
                     %index: i64, %character: i8) -> i32 {
    %literal = simulation.string.literal "ab\00c"
    %converted = simulation.string.from_packed %bits :
      (i24) -> !simulation.string
    %packed = simulation.string.to_packed %literal :
      (!simulation.string) -> i40
    %joined = simulation.string.concat %literal, %input, %converted :
      (!simulation.string, !simulation.string, !simulation.string) ->
      !simulation.string
    %count = arith.constant 3 : i64
    %repeated = simulation.string.repeat %joined, %count :
      (!simulation.string, i64) -> !simulation.string
    %length = simulation.string.length %repeated :
      (!simulation.string) -> i64
    %byte = simulation.string.getc %repeated, %index :
      (!simulation.string, i64) -> i8
    %updated = simulation.string.putc %repeated, %index, %character :
      (!simulation.string, i64, i8) -> !simulation.string
    %substring = simulation.string.substr %updated, %index, %length :
      (!simulation.string, i64, i64) -> !simulation.string
    %comparison = simulation.string.compare %substring, %input
      case_insensitive = true
    %lower = simulation.string.case_convert %substring to_upper = false
    %parsed = simulation.string.parse_integer %input radix = <hex>
    %real = simulation.string.parse_real %input :
      (!simulation.string) -> f64
    %formatted = simulation.string.format_integer %length
      radix = <decimal> signed = false
    %formatted_real = simulation.string.format_real %real :
      (f64) -> !simulation.string
    return %comparison : i32
  }
}

// CHECK: %[[LITERAL:.*]] = simulation.string.literal "ab\00c"
// CHECK: simulation.string.from_packed
// CHECK: simulation.string.to_packed
// CHECK: simulation.string.concat
// CHECK: simulation.string.repeat
// CHECK: simulation.string.length
// CHECK: simulation.string.getc
// CHECK: simulation.string.putc
// CHECK: simulation.string.substr
// CHECK: simulation.string.compare
// CHECK: simulation.string.case_convert
// CHECK: simulation.string.parse_integer
// CHECK: simulation.string.parse_real
// CHECK: simulation.string.format_integer
// CHECK: simulation.string.format_real
