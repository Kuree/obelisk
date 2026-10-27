// RUN: %split-file %s %t
// RUN: talos --std=1800-2023 -emit-talos %t/scalar.sv | talos-opt '--lower-talos-to-sim=opt-level=0' --test-talos-print-coverage-schema -o %t/lowered.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: talos --std=1800-2023 -emit-talos %t/mixed.sv | talos-opt '--lower-talos-to-sim=opt-level=0' --test-talos-print-coverage-schema -o %t/mixed-lowered.mlir 2>&1 | FileCheck %s --check-prefix=MIXED-SCHEMA
// RUN: FileCheck %s --check-prefix=MIXED < %t/mixed-lowered.mlir

// IEEE 1800-2023 19.6.1.2: an iterator-independent predicate has one Boolean
// constructor value (role 11, result kind 1). The with selector refers to the
// automatic-bins selector as its only operand, is flagged scalar (flags=1),
// carries the matches count 2**64-1 in 64 bits, and has no predicate DAG.
// SCHEMA: cross_selector id=[[ALL:[1-9][0-9]*]] {{.*}} kind=6 {{.*}} with_expression=0 {{.*}} flags=0 predicate_root=0
// SCHEMA: cross_selector id=[[ROOT:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} operand_count=1 with_expression=[[PREDICATE:[1-9][0-9]*]] {{.*}} matches_policy=2 matches_count=18446744073709551615 flags=1 predicate_root=0
// SCHEMA: cross_selector_operand node=[[ROOT]] operand=[[ALL]] ordinal=0
// SCHEMA-DAG: functional_expression id=[[PREDICATE]] owner=[[ROOT]] owner_kind=4 role=11 result_kind=1 width=0 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-NOT: cross_predicate
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

// A scalar child (flags=1, role 11 / result kind 1) is resolved before the
// outer iterator-dependent predicate (role 11 / result kind 7) queries its
// candidates; only the outer selector needs a queue and is not scalar.
// MIXED-SCHEMA-DAG: cross_selector id=[[CHILD:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} operand_count=1 with_expression=[[CHILD_PRED:[1-9][0-9]*]] {{.*}} matches_count=1 flags=1
// MIXED-SCHEMA-DAG: cross_selector id=[[OUTER:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} operand_count=1 with_expression=[[OUTER_PRED:[1-9][0-9]*]] {{.*}} matches_count=1 flags=0
// MIXED-SCHEMA-DAG: cross_selector_operand node=[[OUTER]] operand=[[CHILD]] ordinal=0
// MIXED-SCHEMA-DAG: functional_expression id=[[CHILD_PRED]] owner=[[CHILD]] owner_kind=4 role=11 result_kind=1
// MIXED-SCHEMA-DAG: functional_expression id=[[OUTER_PRED]] owner=[[OUTER]] owner_kind=4 role=11 result_kind=7
// MIXED: simulation.covergroup.prepare
// MIXED: simulation.covergroup.candidates
// MIXED: simulation.covergroup.bind_with
// MIXED-NOT: simulation.covergroup.candidates
// MIXED-NOT: simulation.covergroup.bind_with
// MIXED: simulation.covergroup.release_preparation
// MIXED: simulation.covergroup.create

//--- scalar.sv
module scalar;
  bit [31:0] a, b;
  covergroup cg(bit enabled);
    ca: coverpoint a;
    cb: coverpoint b;
    product: cross ca, cb {
      bins selected = product with (enabled) matches 64'hffff_ffff_ffff_ffff;
    }
  endgroup
  cg cov = new(1);
endmodule

//--- mixed.sv
module mixed;
  bit [2:0] a;
  bit b;
  function automatic logic low_bit(logic [2:0] value);
    return value[0];
  endfunction
  covergroup cg(bit enabled);
    ca: coverpoint a { bins all = {[0:7]}; }
    cb: coverpoint b { bins zero = {0}; }
    product: cross ca, cb {
      bins selected = (product with (enabled)) with (low_bit(ca) == 0);
    }
  endgroup
  cg cov = new(1);
endmodule
