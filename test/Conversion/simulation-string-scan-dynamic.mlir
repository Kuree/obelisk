// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dynamic_scan {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.scan"
    obelisk_sim.func private @scan(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %descriptor: i32 {obelisk_sim.capture_kind = 2 : i32},
        %input: !obelisk_sim.string {obelisk_sim.capture_kind = 2 : i32},
        %format: !obelisk_sim.string {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %validated = "obelisk_sim.scan_dynamic_validate"(%format, %zero)
          {allowed_specifiers = 262144 : i64, file = false,
           finalize = false} : (!obelisk_sim.string, i32) -> i32
      %field, %cursor, %plan, %kind, %ok =
          "obelisk_sim.string.scan_dynamic"(%input, %zero, %format, %zero, %one)
          {allowed_specifiers = 262144 : i64, finalize = false,
           raw_four_state_bytes = 0 : i64, raw_two_state_bytes = 0 : i64} :
          (!obelisk_sim.string, i32, !obelisk_sim.string, i32, i32) ->
          (!obelisk_sim.string, i32, i32, i32, i32)
      %fileField, %filePlan, %fileKind, %fileOk, %eof =
          "obelisk_sim.file.scan_dynamic"(%ctx, %descriptor, %format, %zero,
                                            %one)
          {allowed_specifiers = 262144 : i64, finalize = false,
           raw_four_state_bytes = 0 : i64, raw_two_state_bytes = 0 : i64} :
          (!obelisk_sim.context, i32, !obelisk_sim.string, i32, i32) ->
          (!obelisk_sim.string, i32, i32, i32, i32)
      obelisk_sim.return
    }
  }
}

// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_scan_dynamic_validate
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_string_scan_dynamic
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_file_scan_dynamic
// BYTECODE-COUNT-1: intrinsic 0: id=0x00010248 inputs=5 outputs=1 flags=0
// BYTECODE-COUNT-1: intrinsic 1: id=0x00010246 inputs=9 outputs=5 flags=0
// BYTECODE-COUNT-1: intrinsic 2: id=0x00010247 inputs=8 outputs=5 flags=0
