// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
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
  simulation.design @dynamic_insert {
    simulation.scope.decl 0 hierarchy "dynamic_insert"
    simulation.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "dynamic_insert.root"
    simulation.code_unit.decl 9910001 in 0 initial
        hierarchy "dynamic_insert.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %base = simulation.logic.constant 21 : i5, 0 : i5 : !simulation.logic<5>
      %replacement = simulation.logic.constant 6 : i3, 0 : i3 : !simulation.logic<3>
      %negative = simulation.logic.constant -1 : i5, 0 : i5 : !simulation.logic<5>
      %high = simulation.logic.constant 4 : i5, 0 : i5 : !simulation.logic<5>
      %outside = simulation.logic.constant 6 : i5, 0 : i5 : !simulation.logic<5>
      %unknown = simulation.logic.constant 0 : i5, 1 : i5 : !simulation.logic<5>

      %logic_low = simulation.logic.dyn_insert %replacement into %base at %negative : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
      %logic_high = simulation.logic.dyn_insert %replacement into %base at %high : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
      %logic_outside = simulation.logic.dyn_insert %replacement into %base at %outside : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
      %logic_unknown = simulation.logic.dyn_insert %replacement into %base at %unknown : (!simulation.logic<5>, !simulation.logic<3>, !simulation.logic<5>) -> !simulation.logic<5>
      %expected_low = simulation.logic.constant 23 : i5, 0 : i5 : !simulation.logic<5>
      %expected_high = simulation.logic.constant 5 : i5, 0 : i5 : !simulation.logic<5>
      %ok_logic_low = simulation.logic.compare case_eq %logic_low, %expected_low : (!simulation.logic<5>, !simulation.logic<5>) -> i1
      %ok_logic_high = simulation.logic.compare case_eq %logic_high, %expected_high : (!simulation.logic<5>, !simulation.logic<5>) -> i1
      %ok_logic_outside = simulation.logic.compare case_eq %logic_outside, %base : (!simulation.logic<5>, !simulation.logic<5>) -> i1
      %ok_logic_unknown = simulation.logic.compare case_eq %logic_unknown, %base : (!simulation.logic<5>, !simulation.logic<5>) -> i1

      %bits_base = arith.constant 21 : i5
      %bits_replacement = arith.constant 6 : i3
      %bits_low = simulation.bits.dyn_insert %bits_replacement into %bits_base at %negative : (i5, i3, !simulation.logic<5>) -> i5
      %bits_high = simulation.bits.dyn_insert %bits_replacement into %bits_base at %high : (i5, i3, !simulation.logic<5>) -> i5
      %bits_outside = simulation.bits.dyn_insert %bits_replacement into %bits_base at %outside : (i5, i3, !simulation.logic<5>) -> i5
      %bits_unknown = simulation.bits.dyn_insert %bits_replacement into %bits_base at %unknown : (i5, i3, !simulation.logic<5>) -> i5
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
      %format = simulation.bytes.constant "%0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %ok)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, i1
      simulation.return
    }
  }
}
