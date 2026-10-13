// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir

// Selects retain the original self-determined index and declared range. A
// part-select with a constructor index has a fixed window per configuration.
// Neither wide values nor the variable-index value tuples become a queue.
// SCHEMA-DAG: cross_predicate {{.*}} kind=22 width=1 {{.*}} element_width=1 index_base=0
// SCHEMA-DAG: cross_predicate {{.*}} kind=22 width=1 {{.*}} flags=2 element_width=1 index_base=64
// SCHEMA-DAG: cross_predicate {{.*}} kind=22 width=1 {{.*}} element_width=1 index_base=-58
// SCHEMA-DAG: cross_predicate {{.*}} kind=22 width=4 {{.*}} element_width=4 index_base=0
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=2 {{.*}} flags=0 element_width=1 index_base=0
// SCHEMA-DAG: cross_predicate {{.*}} kind=23 width=2 {{.*}} flags=4 element_width=1 index_base=0
// SCHEMA-DAG: functional_expression {{.*}} role=18 result_kind=2 width=32 signedness=2
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

module selects;
  bit [64:0] a;
  bit [0:64] ascending;
  bit [6:-58] negative;
  bit [1:0][3:0] packed_value;
  bit [6:0] position;
  bit b;
  covergroup cg(int index);
    logic [64:0] ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [0:64] cu: coverpoint ascending { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [6:-58] cn: coverpoint negative { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [1:0][3:0] cp: coverpoint packed_value { bins all = {[0:255]}; }
    bit [6:0] ci: coverpoint position { bins all = {[0:64]}; }
    cb: coverpoint b { bins zero = {0}; }
    product: cross ca, cb {
      bins fixed_bit = product with (ca[0] == 0);
      bins constructor_bit = product with (ca[index] == 0);
      bins fixed_slice = product with (ca[64:63] == 0);
      bins indexed_up = product with (ca[index +: 2] == 0);
      bins indexed_down = product with (ca[index -: 2] == 0);
    }
    upward: cross cu, cb { bins selected = upward with (cu[index] == 0); }
    below_zero: cross cn, cb { bins selected = below_zero with (cn[-58] == 0); }
    packed_elements: cross cp, cb { bins selected = packed_elements with (cp[index] == 0); }
    variable: cross ca, ci { bins selected = variable with (ca[ci] == 1); }
  endgroup
  cg cov = new(0);
endmodule
