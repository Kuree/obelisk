// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2017 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=PREPARE
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2017 6.20.2.1, 19.5.1, 19.5.2, and 19.6.1 permit `$` at either
// end of a covergroup value range (the parameter rules moved to 6.20.7 in
// 1800-2023). Preparation retains the open end in the v1 definition schema
// without inventing a constructor expression. An unbounded parameter alias
// chain is semantically identical to a direct `$` endpoint.
// PREPARE-DAG: simulation.coverage.functional.with_candidate_values = [0 : i4, 1 : i4, 2 : i4, 3 : i4]
// PREPARE-DAG: simulation.coverage.functional.with_candidate_values = [-8 : i4, -7 : i4, -6 : i4]
// SCHEMA-DAG: functional_bin id=[[TRANSITION:[1-9][0-9]*]] {{.*}} name=crossing kind=2
// SCHEMA-DAG: functional_bin id=[[PARAMETER:[1-9][0-9]*]] {{.*}} name=parameter_low kind=1
// SCHEMA-DAG: transition_program bin=[[TRANSITION]]
// SCHEMA-DAG: transition_step bin=[[TRANSITION]] value_set=[[TRANSITION_LOW:[1-9][0-9]*]] {{.*}} alternative_ordinal=0 ordinal=0
// SCHEMA-DAG: transition_step bin=[[TRANSITION]] value_set=[[TRANSITION_HIGH:[1-9][0-9]*]] {{.*}} alternative_ordinal=0 ordinal=1
// SCHEMA-DAG: cross_selector id={{[1-9][0-9]*}} cross={{[1-9][0-9]*}} target={{[1-9][0-9]*}} bin=0 value_set=[[SELECTOR_LOW:[1-9][0-9]*]] kind=1
// SCHEMA-DAG: cross_selector id={{[1-9][0-9]*}} cross={{[1-9][0-9]*}} target={{[1-9][0-9]*}} bin=0 value_set=[[SELECTOR_HIGH:[1-9][0-9]*]] kind=1
// SCHEMA-DAG: functional_bin_plan bin=[[PARAMETER]] value_set=[[PARAMETER_SET:[1-9][0-9]*]]
// SCHEMA-DAG: functional_value_atom set=[[TRANSITION_LOW]] ordinal=0 kind=2 flags=19 lower_expression=0 upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set=[[TRANSITION_HIGH]] ordinal=0 kind=2 flags=35 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_value_atom set=[[SELECTOR_LOW]] ordinal=0 kind=2 flags=19 lower_expression=0 upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set=[[SELECTOR_HIGH]] ordinal=0 kind=2 flags=35 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_value_atom set=[[PARAMETER_SET]] ordinal=0 kind=2 flags=19 lower_expression=0 upper_expression={{[1-9][0-9]*}}

//--- input.sv
module integral_open_ranges;
  parameter int OPEN_BASE = $;
  parameter int OPEN = OPEN_BASE;
  bit [3:0] u;
  logic signed [3:0] s;

  covergroup cg;
    unsigned_cp: coverpoint u {
      bins low = {[$:2]};
      bins high = {[13:$]};
      bins parameter_low = {[OPEN:1]};
      bins low_array[] = {[$:1]};
      bins high_array[2] = {[14:$]};
      bins selected = {[$:3]} with (!item[0]);
    }
    signed_cp: coverpoint s {
      bins low = {[$:-6]};
      bins high = {[6:$]};
      bins selected = {[$:-6]} with (item < -5);
    }
    transition_cp: coverpoint u {
      bins crossing = ([$:2] => [13:$]);
      bins expanded[] = ([$:1] => [14:$]);
    }
    pair: cross unsigned_cp, signed_cp {
      bins corners = binsof(unsigned_cp) intersect {[$:2]} &&
                     binsof(signed_cp) intersect {[6:$]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule
