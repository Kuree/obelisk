// RUN: env TALOS_TEST_INPUT=%s TALOS_TEST_OUTPUT=%t.dump \
// RUN:   TALOS_TEST_VPI=read %obj_root/test/talos-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

// IEEE 1800-2023 37.10, 37.12: a scope iterates its concurrent assertions
// through vpiAssertion and vpiConcurrentAssertion, and each assertion returns
// its scope through vpiScope.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_assertion_relations {
    simulation.scope.decl 0 hierarchy "\\$root " debug "$root"
    simulation.vpi_definition.decl @top_definition type 32 name "top"
    simulation.scope.decl 1 parent 0 hierarchy "top" debug "top"
      source_definition "top" vpi_kind 32 definition @top_definition
    simulation.vpi_object.anchor @top_anchor id 0 type 32 in 1 ordinal 0
      hierarchy "top" debug "top" {
        backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_object.anchor @check_anchor id 1 type 686 in 1
      parent @top_anchor ordinal 0 hierarchy "top.check_one"
      debug "check_one"
    simulation.vpi_object.anchor @cover_anchor id 2 type 688 in 1
      parent @top_anchor ordinal 1 hierarchy "top.cov" debug "cov"

    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK-DAG: static_object name=top.check_one vpi_kind=686
// CHECK-DAG: static_object name=top.cov vpi_kind=688
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=740 ordinal=0 {{.*}} source_name=top target_name=top.check_one
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=740 ordinal=1 {{.*}} source_name=top target_name=top.cov
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=744 ordinal=0 {{.*}} source_name=top target_name=top.check_one
// CHECK-DAG: relation {{.*}} source_type=32 mode=iterate selector=744 ordinal=1 {{.*}} source_name=top target_name=top.cov
// CHECK-DAG: relation {{.*}} source_type=686 mode=handle selector=84 {{.*}} source_name=top.check_one target_name=top
