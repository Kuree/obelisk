// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk -emit-obelisk %t/too-many.sv -o %t/too-many.mlir
// RUN: not obelisk-opt %t/too-many.mlir --obelisk-sim-prepare 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TOO-MANY
// RUN: obelisk -emit-obelisk %t/signed.sv -o %t/signed.mlir
// RUN: obelisk-opt %t/signed.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=SIGNED
// RUN: obelisk -emit-obelisk %t/four-state.sv -o %t/four-state.mlir
// RUN: obelisk-opt %t/four-state.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=FOUR-STATE
// RUN: obelisk -emit-obelisk %t/nested.sv -o %t/nested.mlir
// RUN: obelisk-opt %t/nested.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=NESTED

// IEEE 1800-2023 19.6.1.2 evaluates a selector `with` predicate over the
// complete cross-target value Cartesian product at construction. Targets use
// declared cross order, mathematical value order, and the final target varies
// fastest. The exact tuple-major plan is transient compiler metadata.
// PREPARE: simulation.coverage.functional.cross_with_candidate_values = [0 : i2, false, 0 : i2, true, 1 : i2, false, 1 : i2, true, -2 : i2, false, -2 : i2, true, -1 : i2, false, -1 : i2, true]
// PREPARE-SAME: simulation.coverage.functional.cross_with_target_paths = ["cross_with.cg.x.ca", "cross_with.cg.x.cb"]
// SIGNED: simulation.coverage.functional.cross_with_candidate_values = [-2 : i2, false, -2 : i2, true, -1 : i2, false, -1 : i2, true, 0 : i2, false, 0 : i2, true, 1 : i2, false, 1 : i2, true]
// Four-state candidates encode the exact A plane in the low half and B plane
// in the high half, so the bounded domain includes 0, 1, X, and Z per bit.
// FOUR-STATE: simulation.coverage.functional.cross_with_candidate_values = [0 : i4, false
// FOUR-STATE-SAME: -1 : i4, true]

// Omitted `matches` is Count 1, a folded positive count is retained directly,
// `$` is All, and a constructor-dependent count has one typed expression.
// SCHEMA-DAG: cross_selector id=[[ALL:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[ALL_WITH:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=1
// SCHEMA-DAG: cross_selector id=[[LEAF:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[LEAF_WITH:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=2
// SCHEMA-DAG: cross_selector id=[[COMPOUND:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[COMPOUND_WITH:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=3 matches_count=0
// SCHEMA-DAG: cross_selector id=[[DYNAMIC:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[DYNAMIC_WITH:[1-9][0-9]*]] {{.*}} matches_expression=[[MATCHES:[1-9][0-9]*]] matches_policy=2 matches_count=0
// SCHEMA-DAG: cross_selector id=[[HUGE:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[HUGE_WITH:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=4097
// SCHEMA-DAG: functional_expression id=[[ALL_WITH]] owner=[[ALL]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[ALL]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=7 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[LEAF_WITH]] owner=[[LEAF]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[COMPOUND_WITH]] owner=[[COMPOUND]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[DYNAMIC_WITH]] owner=[[DYNAMIC]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[MATCHES]] owner=[[DYNAMIC]] owner_kind=4 role=12 result_kind=2 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[HUGE_WITH]] owner=[[HUGE]] owner_kind=4 role=11 result_kind=1 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1

// Every predicate result and the constructor-dependent matches count travels
// through the unchanged v1 covergroup-create expression batch.
// SIM: simulation.covergroup.create
// SIM-SAME: argument_count 1
// SIM-SAME: expression_ids [

// TOO-MANY: cross selector with candidate count exceeds the v1 limit of 4096
// NESTED-COUNT-2: simulation.coverage.functional.cross_with_candidate_values

//--- input.sv
module cross_with;
  bit [1:0] a;
  bit b;

  covergroup cg(int need);
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins all = x with (ca[0] == cb);
      bins leaf = binsof(ca) with (ca != 0) matches 2;
      bins compound = (binsof(ca) && binsof(cb)) with (ca <= cb) matches $;
      bins dynamic = binsof(cb) with (ca == cb) matches need;
      bins huge = binsof(cb) with (ca == cb)
          matches 128'h1_0000_0000_0000_0000;
    }
  endgroup

  cg cov = new(2);
endmodule

//--- too-many.sv
module too_many_cross_with;
  bit [6:0] a;
  bit [5:0] b;
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = x with (ca == cb);
    }
  endgroup
endmodule

//--- signed.sv
module signed_cross_with;
  bit signed [1:0] a;
  bit b;
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = x with (ca <= cb);
    }
  endgroup
endmodule

//--- four-state.sv
module four_state_cross_with;
  logic [1:0] a;
  bit b;
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = x with (ca == cb);
    }
  endgroup
endmodule

//--- nested.sv
module nested_cross_with;
  bit a;
  bit b;
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = (x with (ca == cb)) with (ca != cb);
    }
  endgroup
endmodule
