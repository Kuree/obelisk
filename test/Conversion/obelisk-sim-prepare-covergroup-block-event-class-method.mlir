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
// CHECK-DAG: obelisk.sv.symbol.subroutine attributes {{.*}}hierarchical_name = "C::observed"{{.*}}obelisk_sim.coverage_block_event_target_id = [[METHOD:[4-9][0-9]{18}]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}obelisk_sim.coverage_block_event_target_id = [[METHOD]] : i64{{.*}}referenced_path = "C::observed"
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}obelisk_sim.coverage_block_event_target_id = [[METHOD]] : i64{{.*}}referenced_path = "C::observed"
// CHECK-DAG: obelisk_sim.func private @{{[^ ]+}}{{.*}}obelisk_sim.coverage_block_event_target_id = [[METHOD]] : i64{{.*}}obelisk_sim.hierarchical_name = "C::observed"

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
