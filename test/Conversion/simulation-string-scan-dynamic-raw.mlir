// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

!pair = !simulation.unpacked_struct<[
  #simulation.field<name = "first", type = !simulation.logic<8>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "second", type = !simulation.logic<4>, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @dynamic_raw {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.scan"
    simulation.func private @scan(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.string {simulation.capture_kind = 2 : i32},
        %format: !simulation.string {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %field, %cursor, %plan, %kind, %ok =
          simulation.string.scan_dynamic %input, %zero, %format, %zero, %one
          {allowed_specifiers = 34603008 : i64, finalize = false,
           raw_four_state_bytes = 16 : i64, raw_two_state_bytes = 8 : i64} :
          (!simulation.string, i32, !simulation.string, i32, i32) ->
          (!simulation.string, i32, i32, i32, i32)
      %first, %after_first, %first_ok = simulation.string.scan_raw
          %field, %zero {four_state = false, max_width = 0 : i64,
                          prefix = ""} :
          (!simulation.string, i32) -> (!simulation.logic<8>, i32, i32)
      %second, %after_second, %second_ok = simulation.string.scan_raw
          %field, %after_first {four_state = false, max_width = 0 : i64,
                                 prefix = ""} :
          (!simulation.string, i32) -> (!simulation.logic<4>, i32, i32)
      %pair = simulation.aggregate.construct %first, %second :
          (!simulation.logic<8>, !simulation.logic<4>) -> !pair
      simulation.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_string_scan_dynamic
// NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_string_scan_raw
// BYTECODE: intrinsic 0: id=0x00010246 inputs=9 outputs=5 flags=0
// BYTECODE: intrinsic 1: id=0x00010243 inputs=7 outputs=3 flags=0
// BYTECODE-COUNT-2: id=0x00010243 inputs={{\[}}
