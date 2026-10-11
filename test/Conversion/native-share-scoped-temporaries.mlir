// RUN: obelisk-opt %s --pass-pipeline='builtin.module(llvm.func(obelisk-native-share-scoped-temporaries))' | FileCheck %s

llvm.func @consume(!llvm.ptr, !llvm.ptr)

// Temporaries whose uses lie between one lifetime start and end in a block
// share an entry slot per element type and alignment. Overlapping scopes keep
// distinct slots; an allocation without lifetime markers is left alone.
// CHECK-LABEL: llvm.func @scoped
// CHECK-DAG: %[[ONE:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK-DAG: %[[A:.*]] = llvm.alloca %[[ONE]] x i32
// CHECK-DAG: %[[B:.*]] = llvm.alloca %[[ONE]] x i32
// CHECK-DAG: %[[W:.*]] = llvm.alloca %[[ONE]] x i64
// CHECK-DAG: %[[KEPT:.*]] = llvm.alloca %{{.*}} x i8
// CHECK-NOT: llvm.alloca
// CHECK: llvm.intr.lifetime.start %[[A]]
// CHECK: llvm.intr.lifetime.start %[[B]]
// CHECK: llvm.call @consume(%[[A]], %[[B]])
// CHECK: ^bb1:
// CHECK: llvm.intr.lifetime.start %[[A]]
// CHECK: llvm.intr.lifetime.start %[[W]]
// CHECK: llvm.call @consume(%[[A]], %[[W]])
// CHECK: llvm.call @consume(%[[KEPT]], %[[KEPT]])
llvm.func @scoped(%value: i32, %wide: i64) {
  %one = llvm.mlir.constant(1 : i64) : i64
  %first = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
  %second = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
  %third = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
  %fourth = llvm.alloca %one x i64 : (i64) -> !llvm.ptr
  %kept = llvm.alloca %one x i8 : (i64) -> !llvm.ptr
  llvm.intr.lifetime.start %first : !llvm.ptr
  llvm.store %value, %first : i32, !llvm.ptr
  llvm.intr.lifetime.start %second : !llvm.ptr
  llvm.store %value, %second : i32, !llvm.ptr
  llvm.call @consume(%first, %second) : (!llvm.ptr, !llvm.ptr) -> ()
  llvm.intr.lifetime.end %first : !llvm.ptr
  llvm.intr.lifetime.end %second : !llvm.ptr
  llvm.br ^bb1
^bb1:
  llvm.intr.lifetime.start %third : !llvm.ptr
  llvm.store %value, %third : i32, !llvm.ptr
  llvm.intr.lifetime.start %fourth : !llvm.ptr
  llvm.store %wide, %fourth : i64, !llvm.ptr
  llvm.call @consume(%third, %fourth) : (!llvm.ptr, !llvm.ptr) -> ()
  llvm.intr.lifetime.end %third : !llvm.ptr
  llvm.intr.lifetime.end %fourth : !llvm.ptr
  llvm.call @consume(%kept, %kept) : (!llvm.ptr, !llvm.ptr) -> ()
  llvm.return
}
