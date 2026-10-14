// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   --test-obelisk-print-bytecode -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @covergroup_identity {
    simulation.scope.decl 0 hierarchy "top"
    simulation.covergroup.decl @cg schema 1
    simulation.covergroup.decl @other schema 2
    simulation.code_unit.decl 1 in 0 function hierarchy "top.compare"
    simulation.func @compare(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %left: !simulation.covergroup_handle<@cg> {simulation.capture_kind = 1 : i32},
        %right: !simulation.covergroup_handle<@other> {simulation.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %equal = simulation.covergroup.equal %left, %right :
        !simulation.covergroup_handle<@cg>, !simulation.covergroup_handle<@other>
      simulation.return %equal : i1
    }
  }
}

// ROUNDTRIP: simulation.covergroup.equal
// NATIVE-LABEL: llvm.func @compare
// NATIVE: llvm.icmp "eq" {{.*}} : i64
// NATIVE-NOT: simulation.covergroup.equal
// BYTECODE: opcode=17 flags=0 {{.*}}src0=1 src1=2
