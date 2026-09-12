// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2017 Tables 19-1 and 19-3 couple the instance Boolean
// get_inst_coverage option to the static Boolean merge_instances option. Keep
// their distinct scopes in the typed v1 plan and constructor option batch.
// SIM: obelisk_sim.covergroup.create
// SIM-SAME: payloads[{{.*}}]
// SIM-SAME: : (i1, {{.*}}, i1, i1) -> !obelisk_sim.covergroup_handle
// SCHEMA: functional_type id=[[TYPE:[1-9][0-9]*]] name={{.*}} language={{2017|2023}} hierarchy=merge_instances_plan.cg
// SCHEMA-DAG: functional_expression id=[[MERGE:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=1 width=0 signedness=3 owner_ordinal=8 owner_subordinal=2 phase=4 result_ordinal={{[0-9]+}}
// SCHEMA-DAG: functional_expression id=[[TRACK:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=1 width=0 signedness=3 owner_ordinal=9 owner_subordinal=1 phase=4 result_ordinal={{[0-9]+}}
// SCHEMA-DAG: functional_expression id=[[TYPE_WEIGHT:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=2 owner_subordinal=2 phase=4 result_ordinal={{[0-9]+}}
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[MERGE]] owner_kind=1 scope=2 option=8 ordinal=8 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[TRACK]] owner_kind=1 scope=1 option=9 ordinal=9 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[TYPE]] expression=[[TYPE_WEIGHT]] owner_kind=1 scope=2 option=2 ordinal=2 flags=0

//--- input.sv
module merge_instances_plan;
  bit sampled;
  covergroup cg(input bit track_instance);
    type_option.weight = 3;
    type_option.merge_instances = 1;
    option.get_inst_coverage = track_instance;
    cp: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  initial begin
    cov = new(1);
    cov.sample();
  end
endmodule
