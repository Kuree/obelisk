// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 19.5.1.1, 19.7.1: the definition is published before processes, while
// later writes are ordered side effects on the same typed group owner. CSE
// must preserve writes on either side of construction.
// SIM-LABEL: simulation.func @__obelisk_root(
// SIM: simulation.call @[[INITIALIZER:unit_[0-9]+]]
// SIM: simulation.spawn @[[PROCESS:unit_[0-9]+]]
// SIM: simulation.func private @[[INITIALIZER]](
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE:-?[0-9]+]] item 0 option distribute_first
// SIM: simulation.return
// SIM: simulation.func private @[[PROCESS]](
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option distribute_first
// SIM: simulation.covergroup.create
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option distribute_first
// SIM: simulation.covergroup.create
// SIM: simulation.finish

// NATIVE-DAG: llvm.mlir.constant(7 : i32)
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_create
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010478 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: imm=7

//--- input.sv
module top;
  int value;
  covergroup cg;
    type_option.distribute_first = 1;
    cp: coverpoint value { bins selected[2] = {[0:5]} with (item >= 2); }
  endgroup
  cg filter_first, distribute_first;
  initial begin
    cg::type_option.distribute_first = 0;
    filter_first = new;
    cg::type_option.distribute_first = 1;
    distribute_first = new;
    $finish;
  end
endmodule
