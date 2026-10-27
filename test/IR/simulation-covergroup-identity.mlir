// RUN: talos-opt %s --canonicalize | FileCheck %s

module {
  simulation.covergroup.decl @cg schema 1
  simulation.covergroup.decl @other schema 2

  // CHECK-LABEL: func.func @self
  // CHECK: arith.constant true
  // CHECK-NOT: simulation.covergroup.equal
  // CHECK: return
  func.func @self(%group: !simulation.covergroup_handle<@cg>) -> i1 {
    %equal = simulation.covergroup.equal %group, %group :
      !simulation.covergroup_handle<@cg>, !simulation.covergroup_handle<@cg>
    return %equal : i1
  }

  // Equal configurations say nothing about distinct instance identities.
  // CHECK-LABEL: func.func @distinct
  // CHECK: simulation.covergroup.equal
  // CHECK: return
  func.func @distinct(%left: !simulation.covergroup_handle<@cg>,
                      %right: !simulation.covergroup_handle<@cg>) -> i1 {
    %equal = simulation.covergroup.equal %left, %right :
      !simulation.covergroup_handle<@cg>, !simulation.covergroup_handle<@cg>
    return %equal : i1
  }

  // CHECK-LABEL: func.func @nulls
  // CHECK: arith.constant true
  // CHECK-NOT: simulation.covergroup.equal
  // CHECK: return
  func.func @nulls() -> i1 {
    %left = simulation.covergroup.null : !simulation.covergroup_handle<@cg>
    %right = simulation.covergroup.null : !simulation.covergroup_handle<@other>
    %equal = simulation.covergroup.equal %left, %right :
      !simulation.covergroup_handle<@cg>, !simulation.covergroup_handle<@other>
    return %equal : i1
  }
}
