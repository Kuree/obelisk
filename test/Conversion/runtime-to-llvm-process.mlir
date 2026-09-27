// RUN: obelisk-opt %s --convert-obelisk-runtime-to-llvm | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8"
} {
  func.func @process_api(%descriptor: !runtime.process_descriptor,
                         %context: !runtime.context,
                         %tier: i32) -> !runtime.status {
    %create_status, %instance = runtime.process.instance.create %descriptor :
        (!runtime.process_descriptor) ->
        (!runtime.status, !runtime.process_instance)
    %frame_status, %frame = runtime.process.instance.frame %instance :
        (!runtime.process_instance) ->
        (!runtime.status, !runtime.mut_bytes)
    %execute_status, %action = runtime.process.instance.execute
        %instance, %context, %tier :
        (!runtime.process_instance, !runtime.context, i32) ->
        (!runtime.status, !runtime.action)
    %destroy_status = runtime.process.instance.destroy %instance :
        (!runtime.process_instance) -> !runtime.status
    return %destroy_status : !runtime.status
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_create(!llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_frame(!llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_execute(!llvm.ptr, !llvm.ptr, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_process_instance_destroy(!llvm.ptr) -> i32
// CHECK-LABEL: func.func @process_api(
// CHECK-SAME: %[[DESCRIPTOR:.*]]: !llvm.ptr, %[[CONTEXT:.*]]: !llvm.ptr, %[[TIER:.*]]: i32) -> i32
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.ptr {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x i64 {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.struct<(i32, i32, i32, i32, i64, i64)> {alignment = 8 : i64}
// CHECK: llvm.call @obelisk_rt_v1_process_instance_create(%[[DESCRIPTOR]], {{.*}})
// CHECK: llvm.call @obelisk_rt_v1_process_instance_frame
// CHECK: llvm.call @obelisk_rt_v1_process_instance_execute({{.*}}, %[[CONTEXT]], %[[TIER]], {{.*}})
// CHECK: llvm.call @obelisk_rt_v1_process_instance_destroy
// CHECK: return {{.*}} : i32
// CHECK-NOT: runtime.
