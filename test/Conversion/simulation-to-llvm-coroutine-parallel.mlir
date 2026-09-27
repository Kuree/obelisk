// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines > %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: FileCheck %s < %t.threaded
// RUN: FileCheck %s --check-prefix=CALLS < %t.threaded

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // Reserve the first managed-literal symbol to exercise collision-free
  // serial name inventory before parallel body conversion.
  llvm.mlir.global internal constant @__obelisk_string_literal.0("occupied")

  simulation.design @parallel_native {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "parallel.literal"
    simulation.code_unit.decl 2 in 0 function hierarchy "parallel.packed_0"
    simulation.code_unit.decl 3 in 0 function hierarchy "parallel.packed_1"
    simulation.code_unit.decl 4 in 0 function hierarchy "parallel.packed_2"
    simulation.code_unit.decl 5 in 0 function hierarchy "parallel.packed_3"
    simulation.code_unit.decl 6 in 0 function hierarchy "parallel.packed_4"
    simulation.code_unit.decl 7 in 0 function hierarchy "parallel.packed_5"
    simulation.code_unit.decl 8 in 0 function hierarchy "parallel.packed_6"
    simulation.code_unit.decl 9 in 0 function hierarchy "parallel.packed_7"
    simulation.code_unit.decl 10 in 0 function hierarchy "parallel.packed_8"
    simulation.code_unit.decl 11 in 0 function hierarchy "parallel.packed_9"
    simulation.code_unit.decl 12 in 0 function hierarchy "parallel.packed_10"
    simulation.code_unit.decl 13 in 0 function hierarchy "parallel.packed_11"
    simulation.code_unit.decl 14 in 0 function hierarchy "parallel.packed_12"
    simulation.code_unit.decl 15 in 0 function hierarchy "parallel.packed_13"
    simulation.code_unit.decl 16 in 0 function hierarchy "parallel.packed_14"
    simulation.code_unit.decl 17 in 0 function hierarchy "parallel.packed_15"
    simulation.code_unit.decl 18 in 0 function hierarchy "parallel.packed_16"
    simulation.code_unit.decl 19 in 0 function hierarchy "parallel.packed_17"
    simulation.code_unit.decl 20 in 0 function hierarchy "parallel.packed_18"
    simulation.code_unit.decl 21 in 0 function hierarchy "parallel.packed_19"
    simulation.code_unit.decl 22 in 0 function hierarchy "parallel.packed_20"
    simulation.code_unit.decl 23 in 0 function hierarchy "parallel.packed_21"
    simulation.code_unit.decl 24 in 0 function hierarchy "parallel.packed_22"
    simulation.code_unit.decl 25 in 0 function hierarchy "parallel.packed_23"
    simulation.code_unit.decl 26 in 0 function hierarchy "parallel.packed_24"
    simulation.code_unit.decl 27 in 0 function hierarchy "parallel.packed_25"
    simulation.code_unit.decl 28 in 0 function hierarchy "parallel.packed_26"
    simulation.code_unit.decl 29 in 0 function hierarchy "parallel.packed_27"
    simulation.code_unit.decl 30 in 0 function hierarchy "parallel.packed_28"
    simulation.code_unit.decl 31 in 0 function hierarchy "parallel.packed_29"
    simulation.code_unit.decl 32 in 0 function hierarchy "parallel.packed_30"
    simulation.code_unit.decl 33 in 0 function hierarchy "parallel.packed_31"
    simulation.code_unit.decl 34 in 0 function hierarchy "parallel.packed_32"
    simulation.code_unit.decl 35 in 0 function hierarchy "parallel.packed_33"
    simulation.code_unit.decl 36 in 0 function hierarchy "parallel.packed_34"
    simulation.code_unit.decl 37 in 0 function hierarchy "parallel.packed_35"
    simulation.code_unit.decl 38 in 0 function hierarchy "parallel.packed_36"
    simulation.code_unit.decl 39 in 0 function hierarchy "parallel.packed_37"
    simulation.code_unit.decl 40 in 0 function hierarchy "parallel.packed_38"
    simulation.code_unit.decl 41 in 0 function hierarchy "parallel.packed_39"
    simulation.code_unit.decl 42 in 0 function hierarchy "parallel.packed_40"
    simulation.code_unit.decl 43 in 0 function hierarchy "parallel.packed_41"
    simulation.code_unit.decl 44 in 0 function hierarchy "parallel.packed_42"
    simulation.code_unit.decl 45 in 0 function hierarchy "parallel.packed_43"
    simulation.code_unit.decl 46 in 0 function hierarchy "parallel.packed_44"
    simulation.code_unit.decl 47 in 0 function hierarchy "parallel.packed_45"
    simulation.code_unit.decl 48 in 0 function hierarchy "parallel.packed_46"
    simulation.code_unit.decl 49 in 0 function hierarchy "parallel.packed_47"
    simulation.code_unit.decl 50 in 0 function hierarchy "parallel.packed_48"
    simulation.code_unit.decl 51 in 0 function hierarchy "parallel.packed_49"
    simulation.code_unit.decl 52 in 0 function hierarchy "parallel.packed_50"
    simulation.code_unit.decl 53 in 0 function hierarchy "parallel.packed_51"
    simulation.code_unit.decl 54 in 0 function hierarchy "parallel.packed_52"
    simulation.code_unit.decl 55 in 0 function hierarchy "parallel.packed_53"
    simulation.code_unit.decl 56 in 0 function hierarchy "parallel.packed_54"
    simulation.code_unit.decl 57 in 0 function hierarchy "parallel.packed_55"
    simulation.code_unit.decl 58 in 0 function hierarchy "parallel.packed_56"
    simulation.code_unit.decl 59 in 0 function hierarchy "parallel.packed_57"
    simulation.code_unit.decl 60 in 0 function hierarchy "parallel.packed_58"
    simulation.code_unit.decl 61 in 0 function hierarchy "parallel.packed_59"
    simulation.code_unit.decl 62 in 0 function hierarchy "parallel.packed_60"
    simulation.code_unit.decl 63 in 0 function hierarchy "parallel.packed_61"
    simulation.code_unit.decl 64 in 0 function hierarchy "parallel.packed_62"
    simulation.code_unit.decl 65 in 0 function hierarchy "parallel.packed_63"
    simulation.code_unit.decl 66 in 0 function hierarchy "parallel.packed_64"
    simulation.func @literal(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = simulation.string.literal "hello"
      simulation.return
    }

    simulation.func private @packed_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_2(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_3(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_4(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_5(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 8 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_7(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 9 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_8(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_9(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_10(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_11(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 13 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_12(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 14 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_13(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 15 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_14(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 16 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_15(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_16(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 18 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_17(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 19 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_18(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 20 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_19(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 21 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_20(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 22 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_21(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 23 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_22(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 24 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_23(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 25 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_24(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 26 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_25(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 27 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_26(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 28 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_27(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 29 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_28(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 30 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_29(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 31 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_30(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 32 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_31(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 33 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_32(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 34 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_33(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 35 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_34(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 36 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_35(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 37 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_36(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 38 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_37(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 39 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_38(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 40 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_39(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 41 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_40(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 42 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_41(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 43 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_42(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 44 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_43(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 45 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_44(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_45(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 47 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_46(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 48 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_47(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 49 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_48(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 50 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_49(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 51 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_50(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 52 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_51(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 53 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_52(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 54 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_53(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 55 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_54(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 56 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_55(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 57 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_56(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 58 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_57(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 59 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_58(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 60 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_59(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 61 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_60(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 62 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_61(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 63 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_62(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 64 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_63(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 65 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

    simulation.func private @packed_64(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 66 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return
    }

  }

  // Resolve a forward callee from more than one 64-function conversion chunk.
  // The final wrapper conversion replaces these func.func symbols only after
  // all body workers have finished using the shared lookup index.
  func.func private @external_helper()

  func.func private @worker_0() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 0 : i32
    return
  }

  func.func private @worker_1() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 1 : i32
    return
  }

  func.func private @worker_2() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 2 : i32
    return
  }

  func.func private @worker_3() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 3 : i32
    return
  }

  func.func private @worker_4() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 4 : i32
    return
  }

  func.func private @worker_5() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 5 : i32
    return
  }

  func.func private @worker_6() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 6 : i32
    return
  }

  func.func private @worker_7() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 7 : i32
    return
  }

  func.func private @worker_8() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 8 : i32
    return
  }

  func.func private @worker_9() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 9 : i32
    return
  }

  func.func private @worker_10() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 10 : i32
    return
  }

  func.func private @worker_11() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 11 : i32
    return
  }

  func.func private @worker_12() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 12 : i32
    return
  }

  func.func private @worker_13() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 13 : i32
    return
  }

  func.func private @worker_14() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 14 : i32
    return
  }

  func.func private @worker_15() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 15 : i32
    return
  }

  func.func private @worker_16() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 16 : i32
    return
  }

  func.func private @worker_17() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 17 : i32
    return
  }

  func.func private @worker_18() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 18 : i32
    return
  }

  func.func private @worker_19() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 19 : i32
    return
  }

  func.func private @worker_20() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 20 : i32
    return
  }

  func.func private @worker_21() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 21 : i32
    return
  }

  func.func private @worker_22() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 22 : i32
    return
  }

  func.func private @worker_23() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 23 : i32
    return
  }

  func.func private @worker_24() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 24 : i32
    return
  }

  func.func private @worker_25() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 25 : i32
    return
  }

  func.func private @worker_26() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 26 : i32
    return
  }

  func.func private @worker_27() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 27 : i32
    return
  }

  func.func private @worker_28() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 28 : i32
    return
  }

  func.func private @worker_29() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 29 : i32
    return
  }

  func.func private @worker_30() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 30 : i32
    return
  }

  func.func private @worker_31() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 31 : i32
    return
  }

  func.func private @worker_32() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 32 : i32
    return
  }

  func.func private @worker_33() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 33 : i32
    return
  }

  func.func private @worker_34() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 34 : i32
    return
  }

  func.func private @worker_35() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 35 : i32
    return
  }

  func.func private @worker_36() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 36 : i32
    return
  }

  func.func private @worker_37() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 37 : i32
    return
  }

  func.func private @worker_38() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 38 : i32
    return
  }

  func.func private @worker_39() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 39 : i32
    return
  }

  func.func private @worker_40() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 40 : i32
    return
  }

  func.func private @worker_41() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 41 : i32
    return
  }

  func.func private @worker_42() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 42 : i32
    return
  }

  func.func private @worker_43() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 43 : i32
    return
  }

  func.func private @worker_44() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 44 : i32
    return
  }

  func.func private @worker_45() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 45 : i32
    return
  }

  func.func private @worker_46() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 46 : i32
    return
  }

  func.func private @worker_47() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 47 : i32
    return
  }

  func.func private @worker_48() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 48 : i32
    return
  }

  func.func private @worker_49() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 49 : i32
    return
  }

  func.func private @worker_50() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 50 : i32
    return
  }

  func.func private @worker_51() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 51 : i32
    return
  }

  func.func private @worker_52() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 52 : i32
    return
  }

  func.func private @worker_53() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 53 : i32
    return
  }

  func.func private @worker_54() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 54 : i32
    return
  }

  func.func private @worker_55() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 55 : i32
    return
  }

  func.func private @worker_56() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 56 : i32
    return
  }

  func.func private @worker_57() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 57 : i32
    return
  }

  func.func private @worker_58() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 58 : i32
    return
  }

  func.func private @worker_59() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 59 : i32
    return
  }

  func.func private @worker_60() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 60 : i32
    return
  }

  func.func private @worker_61() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 61 : i32
    return
  }

  func.func private @worker_62() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 62 : i32
    return
  }

  func.func private @worker_63() {
    func.call @worker_64() : () -> ()
    %c = arith.constant 63 : i32
    return
  }

  func.func private @worker_64() {
    func.call @external_helper() : () -> ()
    %c = arith.constant 64 : i32
    return
  }
}

// CHECK: llvm.mlir.global internal constant @__obelisk_string_literal.1("hello")
// CHECK: llvm.mlir.global internal constant @__obelisk_string_literal.0("occupied")
// CHECK-COUNT-65: llvm.func @packed_
// CHECK-COUNT-65: llvm.func @worker_
// CALLS-COUNT-64: llvm.call @worker_64()
// CALLS: llvm.call @external_helper()
