// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --convert-obelisk-sim-processes-to-llvm-coroutines > %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: FileCheck %s < %t.threaded

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // Reserve the first managed-literal symbol to exercise collision-free
  // serial name inventory before parallel body conversion.
  llvm.mlir.global internal constant @__obelisk_string_literal.0("occupied")

  obelisk_sim.design @parallel_native {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "parallel.literal"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "parallel.packed_0"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "parallel.packed_1"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "parallel.packed_2"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "parallel.packed_3"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "parallel.packed_4"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "parallel.packed_5"
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "parallel.packed_6"
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "parallel.packed_7"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "parallel.packed_8"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "parallel.packed_9"
    obelisk_sim.code_unit.decl 12 in 0 function hierarchy "parallel.packed_10"
    obelisk_sim.code_unit.decl 13 in 0 function hierarchy "parallel.packed_11"
    obelisk_sim.code_unit.decl 14 in 0 function hierarchy "parallel.packed_12"
    obelisk_sim.code_unit.decl 15 in 0 function hierarchy "parallel.packed_13"
    obelisk_sim.code_unit.decl 16 in 0 function hierarchy "parallel.packed_14"
    obelisk_sim.code_unit.decl 17 in 0 function hierarchy "parallel.packed_15"
    obelisk_sim.code_unit.decl 18 in 0 function hierarchy "parallel.packed_16"
    obelisk_sim.code_unit.decl 19 in 0 function hierarchy "parallel.packed_17"
    obelisk_sim.code_unit.decl 20 in 0 function hierarchy "parallel.packed_18"
    obelisk_sim.code_unit.decl 21 in 0 function hierarchy "parallel.packed_19"
    obelisk_sim.code_unit.decl 22 in 0 function hierarchy "parallel.packed_20"
    obelisk_sim.code_unit.decl 23 in 0 function hierarchy "parallel.packed_21"
    obelisk_sim.code_unit.decl 24 in 0 function hierarchy "parallel.packed_22"
    obelisk_sim.code_unit.decl 25 in 0 function hierarchy "parallel.packed_23"
    obelisk_sim.code_unit.decl 26 in 0 function hierarchy "parallel.packed_24"
    obelisk_sim.code_unit.decl 27 in 0 function hierarchy "parallel.packed_25"
    obelisk_sim.code_unit.decl 28 in 0 function hierarchy "parallel.packed_26"
    obelisk_sim.code_unit.decl 29 in 0 function hierarchy "parallel.packed_27"
    obelisk_sim.code_unit.decl 30 in 0 function hierarchy "parallel.packed_28"
    obelisk_sim.code_unit.decl 31 in 0 function hierarchy "parallel.packed_29"
    obelisk_sim.code_unit.decl 32 in 0 function hierarchy "parallel.packed_30"
    obelisk_sim.code_unit.decl 33 in 0 function hierarchy "parallel.packed_31"
    obelisk_sim.code_unit.decl 34 in 0 function hierarchy "parallel.packed_32"
    obelisk_sim.code_unit.decl 35 in 0 function hierarchy "parallel.packed_33"
    obelisk_sim.code_unit.decl 36 in 0 function hierarchy "parallel.packed_34"
    obelisk_sim.code_unit.decl 37 in 0 function hierarchy "parallel.packed_35"
    obelisk_sim.code_unit.decl 38 in 0 function hierarchy "parallel.packed_36"
    obelisk_sim.code_unit.decl 39 in 0 function hierarchy "parallel.packed_37"
    obelisk_sim.code_unit.decl 40 in 0 function hierarchy "parallel.packed_38"
    obelisk_sim.code_unit.decl 41 in 0 function hierarchy "parallel.packed_39"
    obelisk_sim.code_unit.decl 42 in 0 function hierarchy "parallel.packed_40"
    obelisk_sim.code_unit.decl 43 in 0 function hierarchy "parallel.packed_41"
    obelisk_sim.code_unit.decl 44 in 0 function hierarchy "parallel.packed_42"
    obelisk_sim.code_unit.decl 45 in 0 function hierarchy "parallel.packed_43"
    obelisk_sim.code_unit.decl 46 in 0 function hierarchy "parallel.packed_44"
    obelisk_sim.code_unit.decl 47 in 0 function hierarchy "parallel.packed_45"
    obelisk_sim.code_unit.decl 48 in 0 function hierarchy "parallel.packed_46"
    obelisk_sim.code_unit.decl 49 in 0 function hierarchy "parallel.packed_47"
    obelisk_sim.code_unit.decl 50 in 0 function hierarchy "parallel.packed_48"
    obelisk_sim.code_unit.decl 51 in 0 function hierarchy "parallel.packed_49"
    obelisk_sim.code_unit.decl 52 in 0 function hierarchy "parallel.packed_50"
    obelisk_sim.code_unit.decl 53 in 0 function hierarchy "parallel.packed_51"
    obelisk_sim.code_unit.decl 54 in 0 function hierarchy "parallel.packed_52"
    obelisk_sim.code_unit.decl 55 in 0 function hierarchy "parallel.packed_53"
    obelisk_sim.code_unit.decl 56 in 0 function hierarchy "parallel.packed_54"
    obelisk_sim.code_unit.decl 57 in 0 function hierarchy "parallel.packed_55"
    obelisk_sim.code_unit.decl 58 in 0 function hierarchy "parallel.packed_56"
    obelisk_sim.code_unit.decl 59 in 0 function hierarchy "parallel.packed_57"
    obelisk_sim.code_unit.decl 60 in 0 function hierarchy "parallel.packed_58"
    obelisk_sim.code_unit.decl 61 in 0 function hierarchy "parallel.packed_59"
    obelisk_sim.code_unit.decl 62 in 0 function hierarchy "parallel.packed_60"
    obelisk_sim.code_unit.decl 63 in 0 function hierarchy "parallel.packed_61"
    obelisk_sim.code_unit.decl 64 in 0 function hierarchy "parallel.packed_62"
    obelisk_sim.code_unit.decl 65 in 0 function hierarchy "parallel.packed_63"
    obelisk_sim.code_unit.decl 66 in 0 function hierarchy "parallel.packed_64"
    obelisk_sim.func @literal(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %value = obelisk_sim.string.literal "hello"
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_2(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_3(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_4(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_5(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_6(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 8 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_7(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 9 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_8(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_9(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_10(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_11(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 13 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_12(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 14 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_13(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 15 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_14(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 16 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_15(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_16(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 18 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_17(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 19 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_18(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 20 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_19(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 21 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_20(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 22 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_21(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 23 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_22(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 24 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_23(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 25 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_24(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 26 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_25(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 27 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_26(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 28 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_27(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 29 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_28(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 30 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_29(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 31 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_30(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 32 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_31(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 33 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_32(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 34 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_33(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 35 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_34(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 36 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_35(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 37 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_36(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 38 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_37(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 39 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_38(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 40 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_39(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 41 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_40(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 42 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_41(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 43 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_42(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 44 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_43(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 45 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_44(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 46 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_45(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 47 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_46(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 48 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_47(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 49 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_48(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 50 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_49(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 51 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_50(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 52 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_51(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 53 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_52(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 54 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_53(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 55 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_54(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 56 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_55(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 57 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_56(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 58 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_57(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 59 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_58(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 60 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_59(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 61 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_60(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 62 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_61(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 63 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_62(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 64 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_63(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 65 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

    obelisk_sim.func private @packed_64(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 66 : i64, entry_kind = 8 : i32} {
      %bits = arith.constant 0 : i32
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return
    }

  }

  func.func private @worker_0() {
    %c = arith.constant 0 : i32
    return
  }

  func.func private @worker_1() {
    %c = arith.constant 1 : i32
    return
  }

  func.func private @worker_2() {
    %c = arith.constant 2 : i32
    return
  }

  func.func private @worker_3() {
    %c = arith.constant 3 : i32
    return
  }

  func.func private @worker_4() {
    %c = arith.constant 4 : i32
    return
  }

  func.func private @worker_5() {
    %c = arith.constant 5 : i32
    return
  }

  func.func private @worker_6() {
    %c = arith.constant 6 : i32
    return
  }

  func.func private @worker_7() {
    %c = arith.constant 7 : i32
    return
  }

  func.func private @worker_8() {
    %c = arith.constant 8 : i32
    return
  }

  func.func private @worker_9() {
    %c = arith.constant 9 : i32
    return
  }

  func.func private @worker_10() {
    %c = arith.constant 10 : i32
    return
  }

  func.func private @worker_11() {
    %c = arith.constant 11 : i32
    return
  }

  func.func private @worker_12() {
    %c = arith.constant 12 : i32
    return
  }

  func.func private @worker_13() {
    %c = arith.constant 13 : i32
    return
  }

  func.func private @worker_14() {
    %c = arith.constant 14 : i32
    return
  }

  func.func private @worker_15() {
    %c = arith.constant 15 : i32
    return
  }

  func.func private @worker_16() {
    %c = arith.constant 16 : i32
    return
  }

  func.func private @worker_17() {
    %c = arith.constant 17 : i32
    return
  }

  func.func private @worker_18() {
    %c = arith.constant 18 : i32
    return
  }

  func.func private @worker_19() {
    %c = arith.constant 19 : i32
    return
  }

  func.func private @worker_20() {
    %c = arith.constant 20 : i32
    return
  }

  func.func private @worker_21() {
    %c = arith.constant 21 : i32
    return
  }

  func.func private @worker_22() {
    %c = arith.constant 22 : i32
    return
  }

  func.func private @worker_23() {
    %c = arith.constant 23 : i32
    return
  }

  func.func private @worker_24() {
    %c = arith.constant 24 : i32
    return
  }

  func.func private @worker_25() {
    %c = arith.constant 25 : i32
    return
  }

  func.func private @worker_26() {
    %c = arith.constant 26 : i32
    return
  }

  func.func private @worker_27() {
    %c = arith.constant 27 : i32
    return
  }

  func.func private @worker_28() {
    %c = arith.constant 28 : i32
    return
  }

  func.func private @worker_29() {
    %c = arith.constant 29 : i32
    return
  }

  func.func private @worker_30() {
    %c = arith.constant 30 : i32
    return
  }

  func.func private @worker_31() {
    %c = arith.constant 31 : i32
    return
  }

  func.func private @worker_32() {
    %c = arith.constant 32 : i32
    return
  }

  func.func private @worker_33() {
    %c = arith.constant 33 : i32
    return
  }

  func.func private @worker_34() {
    %c = arith.constant 34 : i32
    return
  }

  func.func private @worker_35() {
    %c = arith.constant 35 : i32
    return
  }

  func.func private @worker_36() {
    %c = arith.constant 36 : i32
    return
  }

  func.func private @worker_37() {
    %c = arith.constant 37 : i32
    return
  }

  func.func private @worker_38() {
    %c = arith.constant 38 : i32
    return
  }

  func.func private @worker_39() {
    %c = arith.constant 39 : i32
    return
  }

  func.func private @worker_40() {
    %c = arith.constant 40 : i32
    return
  }

  func.func private @worker_41() {
    %c = arith.constant 41 : i32
    return
  }

  func.func private @worker_42() {
    %c = arith.constant 42 : i32
    return
  }

  func.func private @worker_43() {
    %c = arith.constant 43 : i32
    return
  }

  func.func private @worker_44() {
    %c = arith.constant 44 : i32
    return
  }

  func.func private @worker_45() {
    %c = arith.constant 45 : i32
    return
  }

  func.func private @worker_46() {
    %c = arith.constant 46 : i32
    return
  }

  func.func private @worker_47() {
    %c = arith.constant 47 : i32
    return
  }

  func.func private @worker_48() {
    %c = arith.constant 48 : i32
    return
  }

  func.func private @worker_49() {
    %c = arith.constant 49 : i32
    return
  }

  func.func private @worker_50() {
    %c = arith.constant 50 : i32
    return
  }

  func.func private @worker_51() {
    %c = arith.constant 51 : i32
    return
  }

  func.func private @worker_52() {
    %c = arith.constant 52 : i32
    return
  }

  func.func private @worker_53() {
    %c = arith.constant 53 : i32
    return
  }

  func.func private @worker_54() {
    %c = arith.constant 54 : i32
    return
  }

  func.func private @worker_55() {
    %c = arith.constant 55 : i32
    return
  }

  func.func private @worker_56() {
    %c = arith.constant 56 : i32
    return
  }

  func.func private @worker_57() {
    %c = arith.constant 57 : i32
    return
  }

  func.func private @worker_58() {
    %c = arith.constant 58 : i32
    return
  }

  func.func private @worker_59() {
    %c = arith.constant 59 : i32
    return
  }

  func.func private @worker_60() {
    %c = arith.constant 60 : i32
    return
  }

  func.func private @worker_61() {
    %c = arith.constant 61 : i32
    return
  }

  func.func private @worker_62() {
    %c = arith.constant 62 : i32
    return
  }

  func.func private @worker_63() {
    %c = arith.constant 63 : i32
    return
  }

  func.func private @worker_64() {
    %c = arith.constant 64 : i32
    return
  }
}

// CHECK: llvm.mlir.global internal constant @__obelisk_string_literal.1("hello")
// CHECK: llvm.mlir.global internal constant @__obelisk_string_literal.0("occupied")
// CHECK-COUNT-65: llvm.func @packed_
// CHECK-COUNT-65: llvm.func @worker_
