// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  func.func @strings(%input: !obelisk_sim.string, %bits: i24,
                     %index: i64, %character: i8) -> i32 {
    %literal = obelisk_sim.string.literal "ab\00c"
    %converted = obelisk_sim.string.from_packed %bits :
      (i24) -> !obelisk_sim.string
    %packed = obelisk_sim.string.to_packed %literal :
      (!obelisk_sim.string) -> i40
    %joined = obelisk_sim.string.concat %literal, %input, %converted :
      (!obelisk_sim.string, !obelisk_sim.string, !obelisk_sim.string) ->
      !obelisk_sim.string
    %count = arith.constant 3 : i64
    %repeated = obelisk_sim.string.repeat %joined, %count :
      (!obelisk_sim.string, i64) -> !obelisk_sim.string
    %length = obelisk_sim.string.length %repeated :
      (!obelisk_sim.string) -> i64
    %byte = obelisk_sim.string.getc %repeated, %index :
      (!obelisk_sim.string, i64) -> i8
    %updated = obelisk_sim.string.putc %repeated, %index, %character :
      (!obelisk_sim.string, i64, i8) -> !obelisk_sim.string
    %substring = obelisk_sim.string.substr %updated, %index, %length :
      (!obelisk_sim.string, i64, i64) -> !obelisk_sim.string
    %comparison = obelisk_sim.string.compare %substring, %input
      case_insensitive = true
    %lower = obelisk_sim.string.case_convert %substring to_upper = false
    %parsed = obelisk_sim.string.parse_integer %input radix = 16
    %real = "obelisk_sim.string.parse_real"(%input) :
      (!obelisk_sim.string) -> f64
    %formatted = obelisk_sim.string.format_integer %length
      radix = 10 signed = false
    %formatted_real = "obelisk_sim.string.format_real"(%real) :
      (f64) -> !obelisk_sim.string
    return %comparison : i32
  }
}

// CHECK: %[[LITERAL:.*]] = obelisk_sim.string.literal "ab\00c"
// CHECK: obelisk_sim.string.from_packed
// CHECK: obelisk_sim.string.to_packed
// CHECK: obelisk_sim.string.concat
// CHECK: obelisk_sim.string.repeat
// CHECK: obelisk_sim.string.length
// CHECK: obelisk_sim.string.getc
// CHECK: obelisk_sim.string.putc
// CHECK: obelisk_sim.string.substr
// CHECK: obelisk_sim.string.compare
// CHECK: obelisk_sim.string.case_convert
// CHECK: obelisk_sim.string.parse_integer
// CHECK: obelisk_sim.string.parse_real
// CHECK: obelisk_sim.string.format_integer
// CHECK: obelisk_sim.string.format_real
