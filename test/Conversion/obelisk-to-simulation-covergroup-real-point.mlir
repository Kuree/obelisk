// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %python -c "p=open(r'%t/input.mlir').read().splitlines(); i=next(i for i,s in enumerate(p) if 'name = \"other\"' in s); p[i]=p[i].replace('is_array = false', 'is_array = true'); open(r'%t/real-default-array.mlir','w').write('\n'.join(p))"
// RUN: not obelisk-opt %t/real-default-array.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REAL-DEFAULT

// IEEE 1800-2023 adds real-valued coverpoints.  Keep samples and option
// values typed as f64 through preparation; the v1 schema represents exact
// values, simple ranges, and the two tolerance forms as real intervals.
// SIM: obelisk_sim.covergroup.create {{.*}} payloads[{{.*}}] argument_count 0 formal_ids [] expression_ids [{{.*}}] : (f64
// SIM: obelisk_sim.covergroup.sample {{.*}} values[{{.*}}] ids [{{.*}}] : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<{{.*}}>, f64, !obelisk_sim.logic<1>) -> ()
// SCHEMA-DAG: functional_expression id=[[SAMPLE:[1-9][0-9]*]] owner=[[ITEM:[1-9][0-9]*]] owner_kind=2 role=2 result_kind=3 width=0 signedness=3 {{.*}} phase=2
// SCHEMA-DAG: functional_value_set id={{[1-9][0-9]*}} item=[[ITEM]] atoms=1 width=64 kind=2 flags=1 signedness=3
// SCHEMA-DAG: functional_value_atom set=[[EXACT_SET:[1-9][0-9]*]] ordinal=0 kind=3 flags=3 lower_expression=[[EXACT:[1-9][0-9]*]] upper_expression=0
// SCHEMA-DAG: functional_expression id=[[EXACT]] owner=[[EXACT_SET]] owner_kind=5 role=14 result_kind=3 width=0 signedness=3
// SCHEMA-DAG: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=3 flags=67 lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=3 flags=71 lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=3 flags=75 lower_expression={{[1-9][0-9]*}} upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=3 flags=83 lower_expression=0 upper_expression={{[1-9][0-9]*}}
// SCHEMA-DAG: functional_value_atom set={{[1-9][0-9]*}} ordinal=0 kind=3 flags=99 lower_expression={{[1-9][0-9]*}} upper_expression=0
// SCHEMA-DAG: functional_bin id=[[OPEN:[1-9][0-9]*]] {{.*}} name=open
// SCHEMA-DAG: functional_bin id=[[FIXED:[1-9][0-9]*]] {{.*}} name=fixed
// SCHEMA-DAG: functional_bin_plan bin=[[OPEN]] {{.*}} cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_bin_plan bin=[[FIXED]] {{.*}} cardinality_expression={{[1-9][0-9]*}} array_cardinality=0 array_mode=3 distribution=3
// SCHEMA-DAG: functional_option_plan owner={{[1-9][0-9]*}} expression={{[1-9][0-9]*}} owner_kind=1 scope=2 option=11 ordinal=11 flags=0
// SCHEMA-DAG: functional_option_plan owner=[[ITEM]] expression={{[1-9][0-9]*}} owner_kind=2 scope=2 option=11 ordinal=11 flags=0
// SCHEMA-DAG: cross_plan item=[[CROSS:[1-9][0-9]*]] first_target=0 target_count=2 {{.*}} tuple_provenance_span=128 tuple_flags=1
// SCHEMA-DAG: cross_target cross=[[CROSS]] target=[[ITEM]] ordinal=0 tuple_bit_offset=0 tuple_bit_width=64 tuple_result_kind=3 tuple_signedness=3 tuple_flags=0
// SCHEMA-DAG: cross_target cross=[[CROSS]] target={{[1-9][0-9]*}} ordinal=1 tuple_bit_offset=64 tuple_bit_width=1 tuple_result_kind=2 tuple_signedness=1 tuple_flags=1
// REAL-DEFAULT: a default bin for a real coverpoint cannot be an array

//--- input.sv
module real_coverage;
  real sampled;
  bit side;
  covergroup cg;
    type_option.real_interval = 0.25;
    cp: coverpoint sampled {
      type_option.real_interval = 0.1;
      bins exact = {1.5};
      bins interval = {[2.0:3.0]};
      bins absolute = {[4.0+/-0.5]};
      bins relative = {[5.0+%-10.0]};
      bins low = {[$:0.75]};
      bins high = {[1.25:$]};
      bins open[] = {[6.0:8.0]};
      bins fixed[5] = {[9.0:11.0], 12.0};
      bins other = default;
    }
    side_cp: coverpoint side;
    product: cross cp, side_cp;
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule
