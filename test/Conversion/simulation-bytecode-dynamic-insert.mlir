// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// CHECK: 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @dynamic_insert {
    obelisk_sim.scope.decl 0 hierarchy "dynamic_insert"
    obelisk_sim.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "dynamic_insert.root"
    obelisk_sim.code_unit.decl 9910001 in 0 initial
        hierarchy "dynamic_insert.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %base = obelisk_sim.logic.constant 21 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %replacement = obelisk_sim.logic.constant 6 : i3, 0 : i3 : !obelisk_sim.logic<3>
      %negative = obelisk_sim.logic.constant -1 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %high = obelisk_sim.logic.constant 4 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %outside = obelisk_sim.logic.constant 6 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %unknown = obelisk_sim.logic.constant 0 : i5, 1 : i5 : !obelisk_sim.logic<5>

      %logic_low = obelisk_sim.logic.dyn_insert %replacement into %base at %negative : (!obelisk_sim.logic<5>, !obelisk_sim.logic<3>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
      %logic_high = obelisk_sim.logic.dyn_insert %replacement into %base at %high : (!obelisk_sim.logic<5>, !obelisk_sim.logic<3>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
      %logic_outside = obelisk_sim.logic.dyn_insert %replacement into %base at %outside : (!obelisk_sim.logic<5>, !obelisk_sim.logic<3>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
      %logic_unknown = obelisk_sim.logic.dyn_insert %replacement into %base at %unknown : (!obelisk_sim.logic<5>, !obelisk_sim.logic<3>, !obelisk_sim.logic<5>) -> !obelisk_sim.logic<5>
      %expected_low = obelisk_sim.logic.constant 23 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %expected_high = obelisk_sim.logic.constant 5 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %ok_logic_low = obelisk_sim.logic.compare case_eq %logic_low, %expected_low : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %ok_logic_high = obelisk_sim.logic.compare case_eq %logic_high, %expected_high : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %ok_logic_outside = obelisk_sim.logic.compare case_eq %logic_outside, %base : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1
      %ok_logic_unknown = obelisk_sim.logic.compare case_eq %logic_unknown, %base : (!obelisk_sim.logic<5>, !obelisk_sim.logic<5>) -> i1

      %bits_base = arith.constant 21 : i5
      %bits_replacement = arith.constant 6 : i3
      %bits_low = obelisk_sim.bits.dyn_insert %bits_replacement into %bits_base at %negative : (i5, i3, !obelisk_sim.logic<5>) -> i5
      %bits_high = obelisk_sim.bits.dyn_insert %bits_replacement into %bits_base at %high : (i5, i3, !obelisk_sim.logic<5>) -> i5
      %bits_outside = obelisk_sim.bits.dyn_insert %bits_replacement into %bits_base at %outside : (i5, i3, !obelisk_sim.logic<5>) -> i5
      %bits_unknown = obelisk_sim.bits.dyn_insert %bits_replacement into %bits_base at %unknown : (i5, i3, !obelisk_sim.logic<5>) -> i5
      %bits_expected_low = arith.constant 23 : i5
      %bits_expected_high = arith.constant 5 : i5
      %ok_bits_low = arith.cmpi eq, %bits_low, %bits_expected_low : i5
      %ok_bits_high = arith.cmpi eq, %bits_high, %bits_expected_high : i5
      %ok_bits_outside = arith.cmpi eq, %bits_outside, %bits_base : i5
      %ok_bits_unknown = arith.cmpi eq, %bits_unknown, %bits_base : i5

      %ok0 = arith.andi %ok_logic_low, %ok_logic_high : i1
      %ok1 = arith.andi %ok_logic_outside, %ok_logic_unknown : i1
      %ok2 = arith.andi %ok_bits_low, %ok_bits_high : i1
      %ok3 = arith.andi %ok_bits_outside, %ok_bits_unknown : i1
      %ok4 = arith.andi %ok0, %ok1 : i1
      %ok5 = arith.andi %ok2, %ok3 : i1
      %ok = arith.andi %ok4, %ok5 : i1
      %format = obelisk_sim.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %ok)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, i1
      obelisk_sim.return
    }
  }
}
