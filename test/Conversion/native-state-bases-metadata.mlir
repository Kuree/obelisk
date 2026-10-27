// RUN: talos-opt %s --address-native-state-through-bases -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %s --address-native-state-through-bases --address-native-state-through-bases | diff %t.mlir -
// RUN: talos-opt %s --mlir-disable-threading --address-native-state-through-bases | diff %t.mlir -

// Address rewriting preserves existing memory metadata verbatim.
// CHECK-DAG: #[[VALUE_SCOPE:[a-zA-Z0-9_]+]] = #llvm.alias_scope<{{.*}}description = "existing value">
// CHECK-DAG: #[[MASK_SCOPE:[a-zA-Z0-9_]+]] = #llvm.alias_scope<{{.*}}description = "existing mask">
// CHECK-DAG: #[[TAG:[a-zA-Z0-9_]+]] = #llvm.tbaa_tag<
#domain = #llvm.alias_scope_domain<id = distinct[0]<>>
#value_scope = #llvm.alias_scope<id = distinct[1]<>, domain = #domain, description = "existing value">
#mask_scope = #llvm.alias_scope<id = distinct[2]<>, domain = #domain, description = "existing mask">
#root = #llvm.tbaa_root<id = "existing metadata">
#type = #llvm.tbaa_type_desc<id = "existing type", members = {<#root, 0>}>
#tag = #llvm.tbaa_tag<base_type = #type, access_type = #type, offset = 0>
module {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  // CHECK-LABEL: llvm.func @keep_metadata(
  // CHECK: llvm.mlir.addressof @__talos_state_value_base
  // CHECK: llvm.intr.assume
  // CHECK: llvm.load {{.*}} {alias_scopes = [#[[VALUE_SCOPE]]], alignment = 1 : i64, noalias_scopes = [#[[MASK_SCOPE]]], tbaa = [#[[TAG]]]} : !llvm.ptr -> i8
  // CHECK: llvm.store {{.*}} {alias_scopes = [#[[MASK_SCOPE]]], alignment = 1 : i64, noalias_scopes = [#[[VALUE_SCOPE]]]} : i8, !llvm.ptr
  // CHECK: llvm.return
  llvm.func @keep_metadata(%c: i1, %x: i8) -> i8 {
    %vp = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %xp = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %value = llvm.load %vp {alias_scopes = [#value_scope], noalias_scopes = [#mask_scope], tbaa = [#tag], alignment = 1 : i64} : !llvm.ptr -> i8
    llvm.cond_br %c, ^update, ^done
  ^update:
    llvm.store %x, %xp {alias_scopes = [#mask_scope], noalias_scopes = [#value_scope], alignment = 1 : i64} : i8, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return %value : i8
  }
}
