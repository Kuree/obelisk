// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir | opt -S -passes=verify \
// RUN:   | FileCheck %s --check-prefix=LLVM

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0 hierarchy "coverage"
    obelisk_sim.covergroup.decl @cg id 9 bins [2, 3] debug "cg"
    obelisk_sim.code_unit.decl 90 in 0 function
      hierarchy "coverage.exercise"

    obelisk_sim.func private @exercise(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %incoming: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 90 : i64, entry_kind = 8 : i32} {
      %null = obelisk_sim.covergroup.null
        : !obelisk_sim.covergroup_handle<@cg>
      %type_percentage, %type_covered, %type_total =
        obelisk_sim.covergroup.type_query %ctx from @cg
        : !obelisk_sim.context -> (f64, i32, i32)
      %created = obelisk_sim.covergroup.create %ctx from @cg
        : !obelisk_sim.context -> !obelisk_sim.covergroup_handle<@cg>
      %enabled = obelisk_sim.covergroup.sample_enabled %ctx, %incoming
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> i1
      obelisk_sim.covergroup.bin_hit %ctx, %created[1, 2]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.sample %ctx, %created[
          %enabled, %enabled, %enabled, %enabled, %enabled]
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.stop %ctx, %null
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      obelisk_sim.covergroup.start %ctx, %created
        : !obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>
      %percentage, %covered, %total =
        obelisk_sim.covergroup.instance_query %ctx, %created
        : (!obelisk_sim.context,
           !obelisk_sim.covergroup_handle<@cg>) -> (f64, i32, i32)
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_create
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_sample_enabled
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_bin_hit
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_sample
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_set_enabled
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_instance_query
// CHECK-DAG: llvm.func @obelisk_rt_v1_covergroup_type_query
// CHECK-LABEL: llvm.func @exercise(
// CHECK-SAME: %[[CTX:.*]]: !llvm.ptr, %[[INCOMING:.*]]: i64) -> i32
// CHECK-DAG: llvm.mlir.zero : i64
// CHECK-DAG: llvm.mlir.constant(9 : i64)
// CHECK-DAG: llvm.mlir.constant(2 : i64)
// CHECK-DAG: llvm.mlir.constant(3 : i64)
// CHECK: llvm.call @obelisk_rt_v1_covergroup_type_query
// CHECK: llvm.call @obelisk_rt_v1_covergroup_create
// CHECK: llvm.call @obelisk_rt_v1_covergroup_sample_enabled
// CHECK: llvm.call @obelisk_rt_v1_covergroup_bin_hit
// CHECK: llvm.call @obelisk_rt_v1_covergroup_sample
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_covergroup_set_enabled
// CHECK: llvm.call @obelisk_rt_v1_covergroup_instance_query
// CHECK: llvm.return
// CHECK-NOT: obelisk_sim.covergroup
// CHECK-NOT: unrealized_conversion_cast

// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_create
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_sample_enabled
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_bin_hit
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_sample
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_set_enabled
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_instance_query
// LLVM-DAG: declare i32 @obelisk_rt_v1_covergroup_type_query
