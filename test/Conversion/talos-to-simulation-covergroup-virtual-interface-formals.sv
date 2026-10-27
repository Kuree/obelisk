// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 1800-2023 25.9, 19.3 and 19.8.1: virtual-interface formals keep the
// interface type through the constructor payloads, the live ref, the formal
// reads and the sample default. The sample formal declared as bus.view carries
// the same interface with the "view" modport; the member-less interface keeps
// its own interface type.
// CHECK: simulation.virtual_interface.bind {{[0-9]+}} : !simulation.virtual_interface<[[IF:"[^"]*"]], "">
// CHECK: simulation.covergroup.create {{.*}} argument_count 2 {{.*}} : (!simulation.virtual_interface<[[IF]], "">, !simulation.argument_ref<!simulation.virtual_interface<[[IF]], "">>,
// CHECK: simulation.covergroup.formal_read {{.*}} -> !simulation.virtual_interface<[[IF]], "">{{$}}
// CHECK: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.virtual_interface<[[IF]], "">>
// CHECK: simulation.argument_ref.load {{.*}} -> !simulation.virtual_interface<[[IF]], "">
// CHECK: simulation.virtual_interface.equal {{.*}} : !simulation.virtual_interface<[[IF]], "">, !simulation.virtual_interface<[[IF]], "">
// CHECK: simulation.covergroup.sample {{.*}} !simulation.virtual_interface<[[IF]], "view">, !simulation.logic<1>

// CHECK: simulation.covergroup.create {{.*}} argument_count 1 {{.*}} : (!simulation.virtual_interface<[[EIF:"[^"]*empty_bus"]], "">,
// CHECK: simulation.covergroup.formal_read {{.*}} -> !simulation.virtual_interface<[[EIF]], "">
// CHECK: simulation.virtual_interface.equal {{.*}} : !simulation.virtual_interface<[[EIF]], "">, !simulation.virtual_interface<[[EIF]], "">
// CHECK: simulation.covergroup.sample {{.*}} !simulation.virtual_interface<[[EIF]], "">, !simulation.logic<1>

// The schema records all five formals as result_kind 15 with width 0. Each
// carries the shared type identity of its virtual-interface type; the bus.view
// sample formal has its own identity but the same interface_type as bus. Two
// tables follow: three shared types, and the scope bindings of the two bus
// instances (the unused-specialization instance is not bound).
// SCHEMA-DAG: functional_formal id={{[0-9]+}} type=[[CG:[0-9]+]] name=live kind=1 direction=2 result_kind=15 flags=0 width=0 signedness=3 ordinal=1 default_expression=0 value_type=[[BUS:[0-9]+]]
// SCHEMA-DAG: functional_formal id={{[0-9]+}} type=[[CG]] name=frozen kind=1 direction=1 result_kind=15 flags=1 width=0 signedness=3 ordinal=0 default_expression={{[1-9][0-9]*}} value_type=[[BUS]]
// SCHEMA-DAG: functional_formal id={{[0-9]+}} type=[[CG]] name=current kind=2 direction=1 result_kind=15 flags=1 width=0 signedness=3 ordinal=0 default_expression={{[1-9][0-9]*}} value_type=[[VIEW:[0-9]+]]
// SCHEMA-DAG: functional_formal id={{[0-9]+}} type=[[ECG:[0-9]+]] name=frozen kind=1 direction=1 result_kind=15 flags=1 width=0 signedness=3 ordinal=0 default_expression={{[1-9][0-9]*}} value_type=[[EMPTY:[0-9]+]]
// SCHEMA-DAG: functional_formal id={{[0-9]+}} type=[[ECG]] name=current kind=2 direction=1 result_kind=15 flags=1 width=0 signedness=3 ordinal=0 default_expression={{[1-9][0-9]*}} value_type=[[EMPTY]]
// SCHEMA: functional_virtual_interface_type id=[[EMPTY]] interface_type=[[EMPTY]]
// SCHEMA-NEXT: functional_virtual_interface_type id=[[VIEW]] interface_type=[[BUS]]
// SCHEMA-NEXT: functional_virtual_interface_type id=[[BUS]] interface_type=[[BUS]]
// SCHEMA-NEXT: functional_virtual_interface_scope id=2 interface_type=[[BUS]]
// SCHEMA-NEXT: functional_virtual_interface_scope id=3 interface_type=[[BUS]]
// SCHEMA-NOT: functional_virtual_interface_
// SCHEMA-COUNT-4: functional_expression {{.*}} role=17 result_kind=15 width=0

interface bus #(int WIDTH = 8);
  int value;
  modport view(input value);
endinterface
interface empty_bus;
endinterface
module top;
  bus first();
  bus unused();
  bus #(4) other();
  virtual bus actual;
  covergroup cg(input virtual bus frozen = null, ref virtual bus live)
      with function sample(input virtual bus.view current = null);
    cp: coverpoint (frozen == live && current == null) { bins nil = {1}; }
  endgroup
  covergroup empty_cg(input virtual empty_bus frozen = null)
      with function sample(input virtual empty_bus current = null);
    cp: coverpoint (frozen == null && current == null) { bins nil = {1}; }
  endgroup
  cg group_value;
  empty_cg empty_value;
  initial begin
    actual = first;
    group_value = new(actual, actual);
    group_value.sample();
    empty_value = new;
    empty_value.sample();
    $finish;
  end
endmodule
