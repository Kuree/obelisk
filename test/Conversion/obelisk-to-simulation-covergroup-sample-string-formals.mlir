// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA

// IEEE 1800-2023 19.8.1 does not restrict overridden sample formals to
// integral values. String input and ref formals are transient call values:
// their derived coverpoint and iff results enter the sample batch, while the
// managed strings themselves do not become long-lived runtime coverage state.
// SIM: obelisk_sim.covergroup.create
// SIM: obelisk_sim.string.literal "abc"
// SIM: obelisk_sim.argument_ref.from_ref
// SIM: obelisk_sim.argument_ref.load
// SIM: obelisk_sim.string.compare
// SIM: obelisk_sim.covergroup.sample {{.*}} values[{{%[^,]+}}, {{%[^,]+}}, {{%[^]]+}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// SCHEMA-DAG: functional_formal id={{[1-9][0-9]*}} {{.*}} name=tag kind=2 direction=1 result_kind=6 {{.*}} ordinal=0
// SCHEMA-DAG: functional_formal id={{[1-9][0-9]*}} {{.*}} name=alias_arg kind=2 direction=2 result_kind=6 {{.*}} ordinal=1
// SCHEMA-DAG: functional_formal id={{[1-9][0-9]*}} {{.*}} name=fallback kind=2 direction=1 result_kind=6 flags=1 {{.*}} ordinal=2 default_expression=[[DEFAULT:[1-9][0-9]*]]
// SCHEMA-DAG: functional_expression id=[[DEFAULT]] owner={{[1-9][0-9]*}} owner_kind=6 role=17 result_kind=6 {{.*}} phase=2
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=2 role=2 result_kind=2
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=2 role=3 result_kind=1

//--- input.sv
module sample_string_formals;
  string alias_value;

  covergroup cg with function sample(input string tag,
                                     ref string alias_arg,
                                     input string fallback = "ready");
    cp: coverpoint tag.len()
        iff (alias_arg != "skip" && fallback == "ready") {
      bins three = {3};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    alias_value = "go";
    cov.sample("abc", alias_value);
  end
endmodule
