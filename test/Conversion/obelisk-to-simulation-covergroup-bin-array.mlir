// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: FileCheck %s --check-prefix=OBELISK < %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk -emit-obelisk %t/sized-default.sv -o %t/sized-default.mlir
// RUN: %python -c "p=open(r'%t/sized-default.mlir').read().splitlines(); i=next(i for i,s in enumerate(p) if 'name = \"bad\"' in s); p[i]=p[i].replace('child_roles = array<i64>', 'child_roles = array<i64: 1>').replace('has_number_of_bins = false', 'has_number_of_bins = true'); p.insert(i+1, '                obelisk.sv.expression.integer_literal attributes {constant_value = \"2\", is_declared_unsized = true, is_signed = true, node_id = 999999 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}'); open(r'%t/sized-default-invalid.mlir','w').write('\n'.join(p))"
// RUN: not obelisk-opt %t/sized-default-invalid.mlir \
// RUN:   '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SIZED-DEFAULT

// IEEE 1800-2017 19.5.1 distinguishes a per-value unsized array from a
// fixed-cardinality array.  The latter count belongs to the constructor batch
// because it may depend on covergroup constructor formals.
// OBELISK-DAG: obelisk.sv.symbol.coverage_bin attributes {{.*}}has_number_of_bins = true{{.*}}is_array = true{{.*}}name = "fixed"
// OBELISK-DAG: obelisk.sv.symbol.coverage_bin attributes {{.*}}has_number_of_bins = false{{.*}}is_array = true{{.*}}name = "per_value"
// OBELISK-DAG: obelisk.sv.symbol.coverage_bin attributes {{.*}}has_number_of_bins = false{{.*}}is_array = true{{.*}}is_default = true{{.*}}name = "others"
// SIM: obelisk_sim.covergroup.create {{.*}} payloads[{{.*}}] argument_count 0 formal_ids [] expression_ids [{{[1-9][0-9]*}}
// SCHEMA-DAG: functional_bin id=[[FIXED:[1-9][0-9]*]] {{.*}} name=fixed
// SCHEMA-DAG: functional_bin id=[[UNSIZED:[1-9][0-9]*]] {{.*}} name=per_value
// SCHEMA-DAG: functional_bin id=[[DEFAULT:[1-9][0-9]*]] {{.*}} name=others {{.*}} flags=1
// SCHEMA-DAG: functional_expression id=[[COUNT:[1-9][0-9]*]] owner=[[FIXED]] owner_kind=3 role=8 result_kind=2
// SCHEMA-DAG: functional_bin_plan bin=[[FIXED]] {{.*}} cardinality_expression=[[COUNT]] array_cardinality=0 array_mode=3 distribution=3
// SCHEMA-DAG: functional_bin_plan bin=[[UNSIZED]] {{.*}} cardinality_expression=0 array_cardinality=0 array_mode=2 distribution=2
// SCHEMA-DAG: functional_bin_plan bin=[[DEFAULT]] value_set=0 {{.*}}array_mode=2 distribution=2
// SIZED-DEFAULT: a default coverage bin array must be unsized

//--- input.sv
module state_bin_arrays;
  bit [3:0] sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins fixed[2] = {[0:3]};
      bins per_value[] = {4, 5};
      bins others[] = default;
    }
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample();
  end
endmodule

//--- sized-default.sv
module sized_default;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins bad[] = default;
      bins ordinary = {0};
    }
  endgroup
endmodule
