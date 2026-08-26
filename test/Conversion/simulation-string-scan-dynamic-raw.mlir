// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

!pair = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "first", type = !obelisk_sim.logic<8>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "second", type = !obelisk_sim.logic<4>, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dynamic_raw {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.scan"
    obelisk_sim.func private @scan(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.string {obelisk_sim.capture_kind = 2 : i32},
        %format: !obelisk_sim.string {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %field, %cursor, %plan, %kind, %ok =
          "obelisk_sim.string.scan_dynamic"(%input, %zero, %format, %zero, %one)
          {allowed_specifiers = 34603008 : i64, finalize = false,
           raw_four_state_bytes = 16 : i64, raw_two_state_bytes = 8 : i64} :
          (!obelisk_sim.string, i32, !obelisk_sim.string, i32, i32) ->
          (!obelisk_sim.string, i32, i32, i32, i32)
      %first, %after_first, %first_ok = "obelisk_sim.string.scan_raw"(
          %field, %zero) {four_state = false, max_width = 0 : i64,
                          prefix = ""} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.logic<8>, i32, i32)
      %second, %after_second, %second_ok = "obelisk_sim.string.scan_raw"(
          %field, %after_first) {four_state = false, max_width = 0 : i64,
                                 prefix = ""} :
          (!obelisk_sim.string, i32) -> (!obelisk_sim.logic<4>, i32, i32)
      %pair = obelisk_sim.aggregate.construct %first, %second :
          (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !pair
      obelisk_sim.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_string_scan_dynamic
// NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_string_scan_raw
// BYTECODE: intrinsic 0: id=0x00010246 inputs=9 outputs=5 flags=0
// BYTECODE: intrinsic 1: id=0x00010243 inputs=7 outputs=3 flags=0
// BYTECODE-COUNT-2: id=0x00010243 inputs={{\[}}
