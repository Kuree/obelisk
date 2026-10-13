// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk -emit-obelisk %t/too-many.sv -o %t/too-many.mlir
// RUN: obelisk-opt %t/too-many.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=WIDE
// RUN: obelisk -emit-obelisk %t/signed.sv -o %t/signed.mlir
// RUN: obelisk-opt %t/signed.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=SIGNED
// RUN: obelisk -emit-obelisk %t/four-state.sv -o %t/four-state.mlir
// RUN: obelisk-opt %t/four-state.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=FOUR-STATE
// RUN: obelisk -emit-obelisk %t/nested.sv -o %t/nested.mlir
// RUN: obelisk-opt %t/nested.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=NESTED

// IEEE 1800-2023 19.6.1.2 selects associated values from subordinate bins.
// Integral iterator operations, including bit selects, use a typed DAG. A
// function predicate still uses one constructor queue of associated values.
// PREPARE: simulation.coverage.functional.cross_with_target_paths = ["cross_with.cg.x.ca", "cross_with.cg.x.cb"]
// SIGNED: simulation.coverage.functional.cross_with_target_paths = ["signed_cross_with.cg.x.ca", "signed_cross_with.cg.x.cb"]
// FOUR-STATE: simulation.coverage.functional.cross_with_target_paths = ["four_state_cross_with.cg.x.ca", "four_state_cross_with.cg.x.cb"]

// Omitted `matches` is Count 1, a folded positive count is retained directly,
// `$` is All, and a constructor-dependent count has one typed expression.
// SCHEMA-DAG: cross_selector id=[[ALL:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[ALL_WITH:[1-9][0-9]*]] {{.*}} matches_expression=0 matches_policy=2 matches_count=1 flags=0 predicate_root=0
// SCHEMA-DAG: cross_selector id=[[BIT:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=0 matches_policy=2 matches_count=1 flags=0 predicate_root=[[BIT_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_selector id=[[LEAF:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=0 matches_policy=2 matches_count=2 flags=0 predicate_root=[[LEAF_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_selector id=[[COMPOUND:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=0 matches_policy=3 matches_count=0 flags=0 predicate_root=[[COMPOUND_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_selector id=[[DYNAMIC:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=[[MATCHES:[1-9][0-9]*]] matches_policy=2 matches_count=0 flags=0 predicate_root=[[DYNAMIC_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_selector id=[[HUGE:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=[[HUGE_MATCHES:[1-9][0-9]*]] matches_policy=2 matches_count=0 flags=0 predicate_root=[[HUGE_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: functional_expression id=[[ALL_WITH]] owner=[[ALL]] owner_kind=4 role=11 result_kind=7 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression {{.*}} owner=[[LEAF]] owner_kind=4 role=18 result_kind=2 width=32 {{.*}} owner_ordinal=2 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[MATCHES]] owner=[[DYNAMIC]] owner_kind=4 role=12 result_kind=2 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: functional_expression id=[[HUGE_MATCHES]] owner=[[HUGE]] owner_kind=4 role=12 result_kind=2 width=128 {{.*}} owner_ordinal=0 owner_subordinal=0 phase=1
// SCHEMA-DAG: cross_predicate id=[[BIT_ROOT]] selector=[[BIT]] {{.*}} kind=4 width=1
// SCHEMA-DAG: cross_predicate {{.*}} selector=[[BIT]] {{.*}} kind=22 width=1 {{.*}} element_width=1 index_base=0
// SCHEMA-DAG: cross_predicate id=[[LEAF_ROOT]] selector=[[LEAF]] {{.*}} kind=5 width=1
// SCHEMA-DAG: cross_predicate id=[[COMPOUND_ROOT]] selector=[[COMPOUND]] {{.*}} kind=9 width=1
// SCHEMA-DAG: cross_predicate id=[[DYNAMIC_ROOT]] selector=[[DYNAMIC]] {{.*}} kind=4 width=1
// SCHEMA-DAG: cross_predicate id=[[HUGE_ROOT]] selector=[[HUGE]] {{.*}} kind=4 width=1

// Constructor candidates are queried once, then filtered in an ordinary CFG.
// SIM: simulation.covergroup.prepare
// SIM: simulation.covergroup.candidates
// SIM: simulation.container.read
// SIM: simulation.aggregate.extract
// SIM: simulation.container.write
// SIM: simulation.covergroup.bind_with
// SIM: simulation.covergroup.release_preparation
// SIM: simulation.covergroup.create
// SIM-SAME: argument_count 1
// SIM-SAME: expression_ids [

// WIDE: simulation.coverage.functional.cross_with_target_paths = ["too_many_cross_with.cg.x.ca", "too_many_cross_with.cg.x.cb"]
// NESTED-COUNT-2: simulation.coverage.functional.cross_with_target_paths

//--- input.sv
module cross_with;
  bit [1:0] a;
  bit b;
  function automatic logic low_bit(logic [1:0] value);
    return value[0];
  endfunction

  covergroup cg(int need);
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins all = x with (low_bit(ca) == cb);
      bins bit_select = x with (ca[0] == cb);
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
