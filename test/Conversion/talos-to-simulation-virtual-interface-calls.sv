// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--test-talos-edit-attributes=op=talos.sv.symbol.procedural_block match={procedure_kind = 0 : i32} set={procedure_kind = 3 : i32}' '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=SENS
// RUN: not talos-opt %t.talos.mlir '--test-talos-edit-attributes=has=virtual_interface_call_import remove=virtual_interface_call_import' '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=MODPORT

// CHECK-DAG: simulation.scope.decl [[A:[0-9]+]] parent 1 hierarchy "top.a"
// CHECK-DAG: simulation.scope.decl [[B:[0-9]+]] parent 1 hierarchy "top.b"
// Per IEEE 1800-2017 13.5, the outlined task copies its hidden output
// temporary back through the caller-provided output reference. Its unused
// copy-in value is not part of the lowered boundary. That reference is the
// caller's own formal cell, which reaches the actual only on return.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: [[OUT_STORAGE:%.*]] = simulation.context.storage %arg0[4]
// CHECK: [[OUT:%.*]] = simulation.ref.load [[OUT_STORAGE]]
// CHECK: simulation.ref.store [[OUT]] to %arg1
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK: simulation.virtual_interface.scope
// CHECK: arith.cmpi eq
// CHECK: simulation.call @unit_0
// CHECK: arith.cmpi eq
// CHECK: simulation.call @unit_2
// CHECK: virtual interface call used a null or invalid handle.
// CHECK: simulation.virtual_interface.scope
// CHECK: [[FORMAL:%.*]] = simulation.ref.alloc
// CHECK: arith.cmpi eq
// CHECK: ^bb{{[0-9]+}}([[RETURNED_REF:%.*]]: !simulation.ref<i32>):
// CHECK: [[RETURNED:%.*]] = simulation.ref.load [[RETURNED_REF]]
// CHECK: simulation.ref.store [[RETURNED]] to %arg2
// CHECK: simulation.task.call @unit_1(%arg0, [[FORMAL]], {{.*}} to [[CONT:\^bb[0-9]+]]
// CHECK: arith.cmpi eq
// CHECK: simulation.task.call @unit_3(%arg0, [[FORMAL]], {{.*}} to [[CONT]]
// CHECK: virtual interface task call used a null or invalid handle.
// CHECK-NOT: talos.sv.
// Every possible instance capture read by a dynamically selected function is
// conservatively included in always_comb sensitivity; a task body adds none
// (IEEE 1800-2023 9.2.2.2.1).
// SENS: simulation.suspend.any {{.*}} edges [0, 0]
// MODPORT: virtual-interface method is not imported by the selected modport

interface bus_if;
  int x;
  function int get(int add);
    return x + add;
  endfunction
  task put(int v, output int old);
    old = x;
    x = v;
  endtask
  modport master(import get, import put);
endinterface
module top;
  bus_if a();
  bus_if b();
  virtual bus_if.master vif;
  int y;
  initial begin
    vif = a;
    y = vif.get(3);
    vif.put(4, y);
  end
endmodule
