// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 19.3: input values are captured at construction; refs stay live.
// Reads retain their types and allocation effects through CSE. Constructor
// bytecode carries reference widths rather than looking up each formal.
// SIM: simulation.spawn @[[PROCESS:unit_[0-9]+]]
// SIM: simulation.func private @[[PROCESS]](
// SIM: simulation.call @[[INPUT:unit_[0-9]+]]
// SIM-NOT: simulation.call @[[INPUT]]
// SIM: simulation.argument_ref.from_ref {{.*}}!simulation.ref<!simulation.string> -> !simulation.argument_ref<!simulation.string>
// SIM: simulation.argument_ref.from_ref {{.*}}!simulation.ref<f64> -> !simulation.argument_ref<f64>
// SIM: %[[HANDLE:[0-9]+]] = simulation.covergroup.create {{.*}} formal_ids {{\[}}[[FROZEN:[1-9][0-9]*]], [[LIVE:[1-9][0-9]*]], [[REAL:[1-9][0-9]*]]{{\]}}
// SIM: simulation.covergroup.sample_enabled {{.*}}, %[[SAMPLE_HANDLE:[0-9]+]]
// SIM: %[[FROZEN_VALUE:[0-9]+]] = simulation.covergroup.formal_read {{.*}}, %[[SAMPLE_HANDLE]]{{\[}}[[FROZEN]]{{\]}} {{.*}} -> !simulation.string
// SIM: %[[LIVE_VALUE:[0-9]+]] = simulation.covergroup.formal_read {{.*}}, %[[SAMPLE_HANDLE]]{{\[}}[[LIVE]]{{\]}} {{.*}} -> !simulation.string
// SIM: simulation.covergroup.formal_read {{.*}}, %[[SAMPLE_HANDLE]]{{\[}}[[REAL]]{{\]}} {{.*}} -> f64
// SIM: simulation.string.length %[[FROZEN_VALUE]]
// SIM: simulation.string.length %[[LIVE_VALUE]]
// SIM: simulation.covergroup.sample
// SIM: simulation.covergroup.formal_read {{.*}}{{\[}}[[FROZEN]]{{\]}} {{.*}} -> !simulation.string
// SIM: simulation.covergroup.formal_read {{.*}}{{\[}}[[LIVE]]{{\]}} {{.*}} -> !simulation.string
// SIM: simulation.covergroup.sample
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create
// NATIVE: %[[STRING_KIND:[0-9]+]] = llvm.mlir.constant(5 : i32) : i32
// NATIVE-NEXT: %[[STRING_WIDTH:[0-9]+]] = llvm.mlir.constant(64 : i64) : i64
// NATIVE-NEXT: %[[STRING_SIZE:[0-9]+]] = llvm.mlir.constant(8 : i64) : i64
// NATIVE-NEXT: %[[STRING_STATE:[0-9]+]] = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
// NATIVE-NEXT: %[[STRING_UNKNOWN:[0-9]+]] = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
// NATIVE-NEXT: %[[STRING_COUNT:[0-9]+]] = llvm.mlir.constant({{[1-9][0-9]*}} : i64) : i64
// NATIVE-NEXT: llvm.call @obelisk_rt_v1_covergroup_formal_read({{.*}}, %[[STRING_KIND]], %[[STRING_WIDTH]], %[[STRING_SIZE]], %[[STRING_STATE]], %[[STRING_UNKNOWN]], %[[STRING_COUNT]], {{.*}}) : (!llvm.ptr, i64, i64, i32, i64, i64, !llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32
// NATIVE: %[[REAL_KIND:[0-9]+]] = llvm.mlir.constant(3 : i32) : i32
// NATIVE-NEXT: %[[REAL_WIDTH:[0-9]+]] = llvm.mlir.constant(64 : i64) : i64
// NATIVE-NEXT: %[[REAL_SIZE:[0-9]+]] = llvm.mlir.constant(8 : i64) : i64
// NATIVE-NEXT: %[[REAL_STATE:[0-9]+]] = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
// NATIVE-NEXT: %[[REAL_UNKNOWN:[0-9]+]] = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
// NATIVE-NEXT: %[[REAL_COUNT:[0-9]+]] = llvm.mlir.constant({{[1-9][0-9]*}} : i64) : i64
// NATIVE-NEXT: llvm.call @obelisk_rt_v1_covergroup_formal_read({{.*}}, %[[REAL_KIND]], %[[REAL_WIDTH]], %[[REAL_SIZE]], %[[REAL_STATE]], %[[REAL_UNKNOWN]], %[[REAL_COUNT]], {{.*}}) : (!llvm.ptr, i64, i64, i32, i64, i64, !llvm.ptr, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010450 inputs=18 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010453 inputs=2 outputs=1 flags=0

//--- input.sv
module top;
  string source_text = "captured string";
  real source_real = 1.0;
  int calls;
  function automatic string input_once();
    calls++;
    return source_text;
  endfunction
  covergroup cg(input string frozen, ref string live, ref real real_value);
    cp: coverpoint frozen.len() { bins value = {15}; }
    other: coverpoint live.len() iff (live != "skip") { bins value = {15}; }
    real_point: coverpoint real_value { bins value = {1.0}; }
  endgroup
  cg cov;
  initial begin
    cov = new(input_once(), source_text, source_real);
    cov.sample();
    source_text = "changed string";
    cov.sample();
    $finish;
  end
endmodule
