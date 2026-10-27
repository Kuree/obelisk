// RUN: %split-file %s %t
// RUN: %talos --std=1800-2023 -emit-talos %t/input.sv -o %t/input.mlir
// RUN: talos-opt %t/input.mlir \
// RUN:   '--talos-sim-prepare=prune-unused-coverage=false' \
// RUN:   | FileCheck %s

// Keep this as a preparation-pass test.  The source fragment is used only to
// obtain the frontend's complete class inventory without duplicating its
// compiler-generated method family in hand-written semantic IR.

// The unqualified class-method references resolve to the exact executable
// subroutine and begin/end retain one common v1 target identity.
// CHECK-DAG: talos.sv.symbol.subroutine @{{[^ ]+}} attributes {{.*}}coverage.block_event_target_id = [[METHOD:[4-9][0-9]{18}]] : i64{{.*}}hierarchical_name = "C::observed"
// CHECK-DAG: talos.sv.expression.arbitrary_symbol attributes {{.*}}coverage.block_event_target_id = [[METHOD]] : i64{{.*}}referenced_path = "C::observed"
// CHECK-DAG: talos.sv.expression.arbitrary_symbol attributes {{.*}}coverage.block_event_target_id = [[METHOD]] : i64{{.*}}referenced_path = "C::observed"
// CHECK-DAG: simulation.func private @{{[^ ]+}}{{.*}}coverage.block_event_target_id = [[METHOD]] : i64{{.*}}simulation.hierarchical_name = "C::observed"

//--- input.sv
class C;
  function void observed;
  endfunction

  covergroup cg @@ (begin observed or end observed);
    point: coverpoint 1;
  endgroup
endclass

module top;
  C object = new;
endmodule
