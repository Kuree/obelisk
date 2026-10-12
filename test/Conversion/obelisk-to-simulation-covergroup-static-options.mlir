// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: FileCheck %s --check-prefix=SOURCE < %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 19.7.1 and 19.10: definitions are elaborated constants and the implicit
// static structures are initialized once, before processes or constructors.
// SOURCE: obelisk.coverage.type_option_constant = "75"
// SOURCE: obelisk.coverage.type_option_constant = "init group"
// SOURCE: obelisk.coverage.type_option_constant = "0.25"
// SIM-LABEL: simulation.func @__obelisk_root(
// SIM: simulation.call @[[GROUP:unit_[0-9]+]]
// SIM-NEXT: simulation.call @[[POINT:unit_[0-9]+]]
// SIM-NEXT: simulation.call @[[DEFAULT_POINT:unit_[0-9]+]]
// SIM-NEXT: simulation.call @[[CROSS:unit_[0-9]+]]
// SIM-NOT: simulation.covergroup.create
// SIM: simulation.spawn @[[PROCESS:unit_[0-9]+]]

// SIM: simulation.func private @[[GROUP]](
// SIM-DAG: %[[GOAL:.*]] = arith.constant 75 : i64
// SIM-DAG: %[[REAL:.*]] = arith.constant 2.500000e-01 : f64
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE:-?[0-9]+]] item 0 option weight
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option goal value %[[GOAL]]
// SIM: simulation.string.literal "init group"
// SIM: simulation.covergroup.set_type_string_option {{.*}} type [[TYPE]] item 0 option comment
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option merge_instances
// SIM: simulation.ref.store %[[REAL]] to {{.*}} : f64, !simulation.ref<f64>
// SIM-NOT: simulation.call
// SIM: simulation.return
// SIM: simulation.func private @[[POINT]](
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item [[POINT_ID:-?[0-9]+]] option weight
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item [[POINT_ID]] option goal
// SIM: simulation.return
// SIM: simulation.func private @[[DEFAULT_POINT]](
// SIM-DAG: %[[DEFAULT_REAL:.*]] = arith.constant 1.000000e+00 : f64
// SIM-DAG: %[[DEFAULT_GOAL:.*]] = arith.constant 100 : i32
// SIM-DAG: %[[DEFAULT_WEIGHT:.*]] = arith.constant 1 : i32
// SIM: simulation.ref.store %[[DEFAULT_WEIGHT]] to {{.*}} : i32, !simulation.ref<i32>
// SIM: simulation.ref.store %[[DEFAULT_GOAL]] to {{.*}} : i32, !simulation.ref<i32>
// SIM: simulation.string.literal ""
// SIM: simulation.ref.store %[[DEFAULT_REAL]] to {{.*}} : f64, !simulation.ref<f64>
// SIM: simulation.return
// SIM: simulation.func private @[[CROSS]](
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item [[CROSS_ID:-?[0-9]+]] option weight
// SIM: simulation.return
// SIM: simulation.func private @[[PROCESS]](
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.type_query {{.*}} item [[POINT_ID]]
// SIM: simulation.covergroup.create
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option weight
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.call
// SIM: simulation.finish

// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_string_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_type_query
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_create
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010450 inputs=29 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010455 inputs=2 outputs=3 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010478 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010479 inputs=4 outputs=0 flags=0

//--- input.sv
module top;
  int value, covered, total;
  real percentage;
  function automatic int initial_goal(); return 75; endfunction
  function automatic string initial_comment(); return {"init", " group"}; endfunction
  function automatic real initial_interval(); return 0.25; endfunction
  covergroup cg(input int limit);
    type_option.weight = 0;
    type_option.goal = initial_goal();
    type_option.comment = initial_comment();
    type_option.merge_instances = 1;
    type_option.real_interval = initial_interval();
    cp: coverpoint value {
      type_option.weight = 0;
      type_option.goal = 50;
      bins values = {[0:limit]};
    }
    other: coverpoint value { bins values = {[0:limit]}; }
    cx: cross cp, other { type_option.weight = 0; }
  endgroup
  cg c;
  initial begin
    percentage = cg::cp::get_coverage(covered, total);
    c = new(1);
    cg::type_option.weight = 3;
    c = new(2);
    $display("%f %0d %0d %0d %0d %s %f", percentage, covered, total,
      cg::type_option.goal, cg::other::type_option.weight,
      cg::type_option.comment, cg::type_option.real_interval);
    $finish;
  end
endmodule
