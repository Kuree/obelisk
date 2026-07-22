// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  func.func @process_api(%descriptor: !obelisk_rt.process_descriptor,
                         %context: !obelisk_rt.context,
                         %tier: i32) -> !obelisk_rt.status {
    %create_status, %instance = obelisk_rt.process.instance.create %descriptor :
        (!obelisk_rt.process_descriptor) ->
        (!obelisk_rt.status, !obelisk_rt.process_instance)
    %frame_status, %frame = obelisk_rt.process.instance.frame %instance :
        (!obelisk_rt.process_instance) ->
        (!obelisk_rt.status, !obelisk_rt.mut_bytes)
    %execute_status, %action = obelisk_rt.process.instance.execute
        %instance, %context, %tier :
        (!obelisk_rt.process_instance, !obelisk_rt.context, i32) ->
        (!obelisk_rt.status, !obelisk_rt.action)
    %destroy_status = obelisk_rt.process.instance.destroy %instance :
        (!obelisk_rt.process_instance) -> !obelisk_rt.status
    return %destroy_status : !obelisk_rt.status
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_create(!llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_frame(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_execute(!llvm.ptr, !llvm.ptr, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_destroy(!llvm.ptr) -> i32
// CHECK-LABEL: llvm.func @process_api(
// CHECK-SAME: %[[DESCRIPTOR:.*]]: !llvm.ptr, %[[CONTEXT:.*]]: !llvm.ptr, %[[TIER:.*]]: i32) -> i32
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.ptr {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x i64 {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.struct<(i32, i32, i32, i32, i64, i64)> {alignment = 8 : i64}
// CHECK: llvm.call @obelisk_rt_v1_process_instance_create(%[[DESCRIPTOR]], {{.*}})
// CHECK: llvm.call @obelisk_rt_v1_process_instance_frame
// CHECK: llvm.call @obelisk_rt_v1_process_instance_execute({{.*}}, %[[CONTEXT]], %[[TIER]], {{.*}})
// CHECK: llvm.call @obelisk_rt_v1_process_instance_destroy
// CHECK: llvm.return {{.*}} : i32
// CHECK-NOT: obelisk_rt.
// CHECK-NOT: unrealized_conversion_cast
// CHECK-NOT: func.
