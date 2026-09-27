// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2017 Table 19-1 makes per_instance a Boolean covergroup-instance
// option evaluated when the covergroup is constructed. Keep that contract in
// the typed v1 plan instead of treating its one-bit result as an integer.
// SIM: simulation.covergroup.create
// SIM-SAME: payloads[{{.*}}]
// SIM-SAME: : (i1, {{.*}}, i1) -> !simulation.covergroup_handle
// SCHEMA: functional_type id=[[TYPE:[1-9][0-9]*]] name={{.*}} language={{2017|2023}} hierarchy=per_instance_plan.cg
// SCHEMA: functional_expression id=[[PER_INSTANCE:[1-9][0-9]*]] owner=[[TYPE]] owner_kind=1 role=13 result_kind=1 width=0 signedness=3 owner_ordinal=7 owner_subordinal=1 phase=4 result_ordinal=0
// SCHEMA: functional_option_plan owner=[[TYPE]] expression=[[PER_INSTANCE]] owner_kind=1 scope=1 option=7 ordinal=7 flags=0

//--- input.sv
module per_instance_plan;
  bit sampled;
  covergroup cg(input bit retain_instance);
    option.per_instance = retain_instance;
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
