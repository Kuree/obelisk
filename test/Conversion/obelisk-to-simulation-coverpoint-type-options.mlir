// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2017 Tables 19-3 and 19-4 define weight and goal as static
// coverpoint options. Preserve their item ownership and type scope in v1.
// SCHEMA: functional_type id=[[TYPE:[1-9][0-9]*]] name={{.*}} language={{2017|2023}} hierarchy=coverpoint_type_options.cg
// SCHEMA: functional_item id=[[POINT:[1-9][0-9]*]] type=[[TYPE]] name=cp kind=1 ordinal=0 hierarchy=coverpoint_type_options.cg.cp
// SCHEMA-DAG: functional_expression id=[[GOAL:[1-9][0-9]*]] owner=[[POINT]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=1 owner_subordinal=2 phase=4 result_ordinal={{[0-9]+}}
// SCHEMA-DAG: functional_expression id=[[WEIGHT:[1-9][0-9]*]] owner=[[POINT]] owner_kind=2 role=13 result_kind=2 width=32 signedness=2 owner_ordinal=2 owner_subordinal=2 phase=4 result_ordinal={{[0-9]+}}
// SCHEMA-DAG: functional_option_plan owner=[[POINT]] expression=[[GOAL]] owner_kind=2 scope=2 option=1 ordinal=1 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[POINT]] expression=[[WEIGHT]] owner_kind=2 scope=2 option=2 ordinal=2 flags=0

//--- input.sv
module coverpoint_type_options;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled {
      type_option.weight = 3;
      type_option.goal = 50;
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule
