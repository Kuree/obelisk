// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/input.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' --test-obelisk-print-coverage-schema -o %t/lowered.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/mixed.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' --test-obelisk-print-coverage-schema -o %t/mixed-lowered.mlir 2>&1 | FileCheck %s --check-prefix=MIXED-SCHEMA
// RUN: FileCheck %s --check-prefix=MIXED < %t/mixed-lowered.mlir

// A typed predicate DAG (19.6.1.2) replaces the with expression: the selector
// has with_expression=0, flags=0 and a predicate_root, and the 130-bit domain
// and 2**64 matching values never become a queue. The DAG is
// (ca == cb) && (ca < limit): kind 2 iterator leaves (65-bit, one per cross
// target), kind 4 equality, kind 8 less-than whose right leaf is kind 1, a
// constructor operand (role 18, 65 bits, phase 1), and kind 13 conjunction as
// the root.
// SCHEMA-DAG: cross_target cross={{[0-9]+}} target=[[CA:[1-9][0-9]*]] ordinal=0
// SCHEMA-DAG: cross_target cross={{[0-9]+}} target=[[CB:[1-9][0-9]*]] ordinal=1
// SCHEMA-DAG: cross_selector id=[[ROOT:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_policy=2 matches_count=18446744073709551615 flags=0 predicate_root=[[AND:[1-9][0-9]*]]
// SCHEMA-DAG: cross_predicate id=[[LEAF_A:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} expression=0 target=[[CA]] kind=2 width=65 {{.*}} ordinal=0
// SCHEMA-DAG: cross_predicate id=[[LEAF_B:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} expression=0 target=[[CB]] kind=2 width=65 {{.*}} ordinal=1
// SCHEMA-DAG: cross_predicate id=[[EQ:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} operand_count=2 operands=[[LEAF_A]],[[LEAF_B]] expression=0 target=0 kind=4 width=1 {{.*}} ordinal=2
// SCHEMA-DAG: cross_predicate id=[[LEAF_A2:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} expression=0 target=[[CA]] kind=2 width=65 {{.*}} ordinal=3
// SCHEMA-DAG: cross_predicate id=[[LIMIT_LEAF:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} expression=[[LIMIT:[1-9][0-9]*]] target=0 kind=1 width=65 {{.*}} ordinal=[[ORD:[0-9]+]]
// SCHEMA-DAG: cross_predicate id=[[LT:[1-9][0-9]*]] selector=[[ROOT]] {{.*}} operand_count=2 operands=[[LEAF_A2]],[[LIMIT_LEAF]] expression=0 target=0 kind=8 width=1 {{.*}} ordinal=5
// SCHEMA-DAG: cross_predicate id=[[AND]] selector=[[ROOT]] {{.*}} operand_count=2 operands=[[EQ]],[[LT]] expression=0 target=0 kind=13 width=1 {{.*}} ordinal=6
// SCHEMA-DAG: functional_expression id=[[LIMIT]] owner=[[ROOT]] owner_kind=4 role=18 result_kind=2 width=65 {{.*}} owner_ordinal=[[ORD]] owner_subordinal=0 phase=1
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

// A symbolic child ((ca == 0), a predicate DAG of kind 4 over an extended
// iterator and a constant) is resolved before the queue-based parent, whose
// opaque function-call predicate keeps a role 11 expression (result kind 7)
// and needs prepare/candidates/bind_with exactly once.
// MIXED-SCHEMA-DAG: cross_selector id=[[CHILD:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} flags=0 predicate_root=[[EQ:[1-9][0-9]*]]
// MIXED-SCHEMA-DAG: cross_selector id=[[PARENT:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} operand_count=1 with_expression=[[CALL:[1-9][0-9]*]] {{.*}} flags=0 predicate_root=0
// MIXED-SCHEMA-DAG: cross_predicate id=[[EQ]] selector=[[CHILD]] {{.*}} kind=4 width=1
// MIXED-SCHEMA-DAG: cross_selector_operand node=[[PARENT]] operand=[[CHILD]] ordinal=0
// MIXED-SCHEMA-DAG: functional_expression id=[[CALL]] owner=[[PARENT]] owner_kind=4 role=11 result_kind=7
// MIXED-SCHEMA-DAG: functional_expression {{.*}} owner=[[CHILD]] owner_kind=4 role=18 result_kind=2 width=32
// MIXED: simulation.covergroup.prepare
// MIXED: simulation.covergroup.candidates
// MIXED: simulation.covergroup.bind_with
// MIXED-NOT: simulation.covergroup.candidates
// MIXED-NOT: simulation.covergroup.bind_with
// MIXED: simulation.covergroup.release_preparation
// MIXED: simulation.covergroup.create

//--- input.sv
module symbolic;
  bit [64:0] a, b;
  covergroup cg(bit [64:0] limit);
    ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    cb: coverpoint b { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    product: cross ca, cb {
      bins selected = product with (ca == cb && ca < limit)
          matches 64'hffff_ffff_ffff_ffff;
    }
  endgroup
  cg cov = new(65'h1_0000_0000_0000_0000);
endmodule

//--- mixed.sv
module mixed;
  bit [1:0] a;
  bit b;
  covergroup cg;
    ca: coverpoint a { bins all = {[0:3]}; }
    cb: coverpoint b { bins zero = {0}; }
    product: cross ca, cb {
      bins selected = (product with (ca == 0)) with (ca[0] == 1);
    }
  endgroup
  cg cov = new;
endmodule
