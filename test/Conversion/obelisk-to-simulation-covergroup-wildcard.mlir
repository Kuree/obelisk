// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: FileCheck %s --check-prefix=OBELISK < %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// Wildcard state and transition bins remain first-class semantic obligations. The
// compiler preserves the literal's original width and signedness so the v1
// runtime can apply the coverpoint-type conversion after expanding wildcard
// bits, as required by IEEE 1800-2023 19.5.4.
// OBELISK-COUNT-3: obelisk.sv.symbol.coverage_bin
// OBELISK-SAME: is_wildcard = true
// SIM: obelisk_sim.covergroup.decl @[[DECL:__obelisk_covergroup_.*]] schema {{[1-9][0-9]*}}
// SIM: obelisk_sim.covergroup.create {{.*}} from @[[DECL]]
// SIM: obelisk_sim.covergroup.sample
// SCHEMA-DAG: functional_bin id=[[BIN:[1-9][0-9]*]] {{.*}} name=values kind=1 flags=16
// SCHEMA-DAG: functional_value_set id=[[SET:[1-9][0-9]*]] {{.*}} atoms=1 width=5 kind=1 flags=1 signedness=2
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[SET]] owner_kind=5 role=14 result_kind=2 width=4 signedness=2
// SCHEMA-DAG: functional_bin_plan bin=[[BIN]] value_set=[[SET]]
// SCHEMA-DAG: functional_bin id=[[SCALAR:[1-9][0-9]*]] {{.*}} name=T0_3 kind=2 flags=16
// SCHEMA-DAG: functional_bin_plan bin=[[SCALAR]] value_set=0 {{.*}} array_mode=1 distribution=1
// SCHEMA-DAG: transition_program bin=[[SCALAR]] {{.*}} alternative_count=1
// SCHEMA-DAG: transition_step bin=[[SCALAR]] value_set=[[SCALAR_FIRST:[1-9][0-9]*]] {{.*}} ordinal=0 repetition=1 flags=0
// SCHEMA-DAG: transition_step bin=[[SCALAR]] value_set=[[SCALAR_LAST:[1-9][0-9]*]] {{.*}} ordinal=1 repetition=1 flags=0
// SCHEMA-DAG: functional_bin id=[[ARRAY:[1-9][0-9]*]] {{.*}} name=T0_3_array kind=2 flags=16
// SCHEMA-DAG: functional_bin_plan bin=[[ARRAY]] value_set=0 {{.*}} array_mode=2 distribution=2
// SCHEMA-DAG: transition_program bin=[[ARRAY]] {{.*}} alternative_count=1
// SCHEMA-DAG: transition_step bin=[[ARRAY]] value_set=[[ARRAY_FIRST:[1-9][0-9]*]] {{.*}} ordinal=0 repetition=1 flags=0
// SCHEMA-DAG: transition_step bin=[[ARRAY]] value_set=[[ARRAY_LAST:[1-9][0-9]*]] {{.*}} ordinal=1 repetition=1 flags=0
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[SCALAR_FIRST]] owner_kind=5 role=14 result_kind=2 width=2 signedness=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[SCALAR_LAST]] owner_kind=5 role=14 result_kind=2 width=2 signedness=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[ARRAY_FIRST]] owner_kind=5 role=14 result_kind=2 width=2 signedness=1
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner=[[ARRAY_LAST]] owner_kind=5 role=14 result_kind=2 width=2 signedness=1

//--- input.sv
module wildcard_state_bin;
  logic signed [4:0] sampled;
  logic [1:0] transition_sampled;

  covergroup cg;
    cp: coverpoint sampled {
      wildcard bins values = {4'sb?001};
    }
    transition_cp: coverpoint transition_sampled {
      wildcard bins T0_3 = (2'b0x => 2'b1x);
      wildcard bins T0_3_array[] = (2'b0x => 2'b1x);
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule
