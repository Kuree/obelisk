// RUN: obelisk-opt --convert-obelisk-runtime-to-llvm %S/../IR/runtime.mlir | FileCheck %s

// CHECK-DAG: llvm.func @obelisk_rt_v1_context_create(!llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_context_destroy(!llvm.ptr)
// CHECK-DAG: llvm.func @obelisk_rt_v1_status_string(i32) -> !llvm.ptr
// CHECK-DAG: llvm.func @obelisk_rt_v1_buffer_release(!llvm.ptr)
// CHECK-DAG: llvm.func @obelisk_rt_v1_last_error(!llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_finish(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_stop(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_fatal(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_termination_requested(!llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_format(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_display(!llvm.ptr, i32, i32, i32, !llvm.ptr, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_open_mcd(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_open(!llvm.ptr, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_close(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_flush(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_write(!llvm.ptr, i32, !llvm.ptr, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_read(!llvm.ptr, i32, !llvm.ptr, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_readmem_token(!llvm.ptr, i32, i32, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_getc(!llvm.ptr, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_ungetc(!llvm.ptr, i32, i8) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_getline(!llvm.ptr, i32, i64, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_eof(!llvm.ptr, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_error(!llvm.ptr, i32, !llvm.ptr, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_seek(!llvm.ptr, i32, i64, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_tell(!llvm.ptr, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_rewind(!llvm.ptr, i32) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_fragment_execute(!llvm.ptr, !llvm.ptr, !llvm.ptr, i64, i32, !llvm.ptr) -> i32
// CHECK-DAG: llvm.func @obelisk_rt_v1_bytecode_execute_bounded(!llvm.ptr, !llvm.ptr, !llvm.ptr, i64, i32, i64, !llvm.ptr) -> i32

// CHECK: func.func private @abi_records(!llvm.struct<(i32, i32, i64)>, !llvm.struct<(i32, i32, i32, i32, i64, i64)>, !llvm.ptr, !llvm.ptr, !llvm.struct<(i32, i32)>, !llvm.struct<(i32, i32)>, !llvm.struct<(i8, i8, i8, i8, i32, i64, i64, i64)>, !llvm.struct<(i32, i32, i16, i16, i32)>, i8, i8, i8, i8, i8, i32, !llvm.struct<(i32, i32, i64, ptr, ptr)>, !llvm.ptr)
// CHECK: func.func private @identity(!llvm.ptr) -> !llvm.ptr
// CHECK: func.func private @tuple_identity(tuple<!llvm.ptr>) -> tuple<!llvm.ptr>
// CHECK-LABEL: func.func @call_indirect
// CHECK: %[[CALLEE:.*]] = constant @identity : (!llvm.ptr) -> !llvm.ptr
// CHECK: %[[RESULT:.*]] = call_indirect %[[CALLEE]]({{.*}}) : (!llvm.ptr) -> !llvm.ptr
// CHECK: return %[[RESULT]] : !llvm.ptr
// CHECK-LABEL: func.func @call_and_branch
// CHECK: call @identity({{.*}}) : (!llvm.ptr) -> !llvm.ptr
// CHECK: cf.cond_br {{.*}}, ^bb1({{.*}} : !llvm.ptr), ^bb2({{.*}} : !llvm.ptr)
// CHECK-LABEL: func.func @loop_alloca
// CHECK: llvm.alloca {{.*}} x i8 {alignment = 1 : i64}
// CHECK: cf.br
// CHECK: ^bb1:
// CHECK-NOT: llvm.alloca
// CHECK: llvm.call @obelisk_rt_v1_file_getc
// CHECK-LABEL: func.func @runtime_calls
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.ptr {alignment = 8 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x i8 {alignment = 1 : i64}
// CHECK-DAG: llvm.alloca {{.*}} x !llvm.struct<(i32, i32, i32, i32, i64, i64)> {alignment = 8 : i64}
// CHECK: llvm.call @obelisk_rt_v1_context_create
// CHECK: llvm.load {{.*}} {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
// CHECK: llvm.call @obelisk_rt_v1_scheduler_finish
// CHECK: llvm.call @obelisk_rt_v1_scheduler_stop
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fatal
// CHECK: %[[TERMINATION_REQUESTED_I32:.*]] = llvm.call @obelisk_rt_v1_scheduler_termination_requested
// CHECK: llvm.trunc %[[TERMINATION_REQUESTED_I32]] : i32 to i1
// CHECK: llvm.zext {{.*}} : i1 to i32
// CHECK: llvm.mlir.constant(16 : i32) : i32
// CHECK: llvm.call @obelisk_rt_v1_display
// CHECK: llvm.call @obelisk_rt_v1_file_readmem_token
// CHECK: llvm.call @obelisk_rt_v1_file_getc
// CHECK: llvm.call @obelisk_rt_v1_file_seek
// CHECK: llvm.call @obelisk_rt_v1_fragment_execute
// CHECK: llvm.load {{.*}} {alignment = 8 : i64} : !llvm.ptr -> !llvm.struct<(i32, i32, i32, i32, i64, i64)>
