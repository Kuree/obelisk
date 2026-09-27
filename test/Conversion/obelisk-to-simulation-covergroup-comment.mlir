// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// Tables 19-1 and 19-3 define distinct instance and static type comments for
// covergroups and coverpoints. All four String expressions are evaluated once
// in the option batch and retain their typed v1 owner and scope.
// SIM: simulation.covergroup.create
// SIM-SAME: payloads[{{.*}}]
// SIM-SAME: : (!simulation.string, !simulation.string, {{.*}}, !simulation.string, !simulation.string, !simulation.string, !simulation.string) -> !simulation.covergroup_handle
// SCHEMA: functional_type id=[[TYPE:[1-9][0-9]*]] name={{.*}} language={{2017|2023}} hierarchy=comment_plan.cg
// SCHEMA: functional_item id=[[POINT:[1-9][0-9]*]] type=[[TYPE]] name=cp kind=1 ordinal=0 hierarchy=comment_plan.cg.cp
// SCHEMA-DAG: functional_expression id=[[GROUP_COMMENT:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=6 width=0 signedness=3 owner_ordinal=14 owner_subordinal=1 phase=4 result_ordinal=0
// SCHEMA-DAG: functional_expression id=[[GROUP_TYPE_COMMENT:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=6 width=0 signedness=3 owner_ordinal=14 owner_subordinal=2 phase=4 result_ordinal=1
// SCHEMA-DAG: functional_expression id=[[POINT_COMMENT:[1-9][0-9]*]] owner=[[POINT]] owner_kind=2 role=13 result_kind=6 width=0 signedness=3 owner_ordinal=14 owner_subordinal=1 phase=4 result_ordinal=2
// SCHEMA-DAG: functional_expression id=[[POINT_TYPE_COMMENT:[1-9][0-9]*]] owner=[[POINT]] owner_kind=2 role=13 result_kind=6 width=0 signedness=3 owner_ordinal=14 owner_subordinal=2 phase=4 result_ordinal=3
// SCHEMA: functional_option_plan owner=[[TYPE]] expression=[[GROUP_COMMENT]] owner_kind=1 scope=1 option=14 ordinal=14 flags=0
// SCHEMA: functional_option_plan owner=[[TYPE]] expression=[[GROUP_TYPE_COMMENT]] owner_kind=1 scope=2 option=14 ordinal=14 flags=0
// SCHEMA: functional_option_plan owner=[[POINT]] expression=[[POINT_COMMENT]] owner_kind=2 scope=1 option=14 ordinal=14 flags=0
// SCHEMA: functional_option_plan owner=[[POINT]] expression=[[POINT_TYPE_COMMENT]] owner_kind=2 scope=2 option=14 ordinal=14 flags=0

//--- input.sv
module comment_plan;
  bit sampled;
  covergroup cg(input string group_comment, input string point_comment);
    option.comment = group_comment;
    type_option.comment = "group type comment";
    cp: coverpoint sampled {
      option.comment = point_comment;
      type_option.comment = "point type comment";
      bins zero = {0};
    }
  endgroup

  cg cov;
  initial begin
    cov = new("group constructor comment", "point constructor comment");
    cov.sample();
  end
endmodule
