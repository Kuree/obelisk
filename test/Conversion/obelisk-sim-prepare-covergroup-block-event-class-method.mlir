// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir \
// RUN:   '--obelisk-sim-prepare=prune-unused-coverage=false' \
// RUN:   | FileCheck %s

// Keep this as a preparation-pass test.  The source fragment is used only to
// obtain the frontend's complete class inventory without duplicating its
// compiler-generated method family in hand-written semantic IR.

// The unqualified class-method references resolve to the exact executable
// subroutine and begin/end retain one common v1 target identity.
// CHECK-DAG: obelisk.sv.symbol.subroutine attributes {{.*}}hierarchical_name = "C::observed"{{.*}}simulation.coverage_block_event_target_id = [[METHOD:[4-9][0-9]{18}]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}referenced_path = "C::observed"{{.*}}simulation.coverage_block_event_target_id = [[METHOD]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}referenced_path = "C::observed"{{.*}}simulation.coverage_block_event_target_id = [[METHOD]] : i64
// CHECK-DAG: simulation.func private @{{[^ ]+}}{{.*}}simulation.coverage_block_event_target_id = [[METHOD]] : i64{{.*}}simulation.hierarchical_name = "C::observed"

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
