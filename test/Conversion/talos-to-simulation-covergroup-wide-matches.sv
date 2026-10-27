// RUN: talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' --test-talos-print-coverage-schema -o %t.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir

// IEEE 1800-2023 19.6.1.2: any positive integral matches count is allowed.
// Constructor-dependent and wide counts are typed operands (role 12, result
// kind 2, original width, phase 1) referenced by matches_expression with
// matches_count=0, not truncated into the 64-bit matches_count. Symbolic
// (equality DAG, flags=0, predicate_root) and scalar (flags=1, role 11
// Boolean) selectors keep their own widths: 512 for `need`, 128 for the
// folded 2**65 literal. Only a count that fits (5000) stays in matches_count
// with matches_expression=0.
// SCHEMA-DAG: cross_selector id=[[DYNAMIC:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=[[NEED:[1-9][0-9]*]] matches_policy=2 matches_count=0 flags=0 predicate_root=[[DYNAMIC_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_predicate id=[[DYNAMIC_ROOT]] selector=[[DYNAMIC]] {{.*}} kind=4 width=1
// SCHEMA-DAG: functional_expression id=[[NEED]] owner=[[DYNAMIC]] owner_kind=4 role=12 result_kind=2 width=512 {{.*}} phase=1
// SCHEMA-DAG: cross_selector id=[[SCALAR:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=[[SCALAR_PRED:[1-9][0-9]*]] {{.*}} matches_expression=[[SCALAR_NEED:[1-9][0-9]*]] matches_policy=2 matches_count=0 flags=1 predicate_root=0
// SCHEMA-DAG: functional_expression id=[[SCALAR_PRED]] owner=[[SCALAR]] owner_kind=4 role=11 result_kind=1
// SCHEMA-DAG: functional_expression id=[[SCALAR_NEED]] owner=[[SCALAR]] owner_kind=4 role=12 result_kind=2 width=512 {{.*}} phase=1
// SCHEMA-DAG: cross_selector id=[[LITERAL:[1-9][0-9]*]] {{.*}} kind=7 {{.*}} with_expression=0 {{.*}} matches_expression=[[LITERAL_NEED:[1-9][0-9]*]] matches_policy=2 matches_count=0 flags=0 predicate_root=[[LITERAL_ROOT:[1-9][0-9]*]]
// SCHEMA-DAG: cross_predicate id=[[LITERAL_ROOT]] selector=[[LITERAL]] {{.*}} kind=4 width=1
// SCHEMA-DAG: functional_expression id=[[LITERAL_NEED]] owner=[[LITERAL]] owner_kind=4 role=12 result_kind=2 width=128 {{.*}} phase=1
// SCHEMA-DAG: cross_selector {{.*}} kind=7 {{.*}} with_expression={{[1-9][0-9]*}} {{.*}} matches_expression=0 matches_policy=2 matches_count=5000 flags=1 predicate_root=0

// None of the predicates needs a candidate queue.
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

module wide_matches;
  bit [64:0] a, b;
  covergroup cg(bit [511:0] need);
    ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    cb: coverpoint b { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    product: cross ca, cb {
      bins dynamic_count = product with (ca == cb) matches need;
      bins scalar_count = product with (1) matches need;
      bins literal_count = product with (ca == cb)
          matches 128'h2_0000_0000_0000_0000;
      bins compact_count = product with (1) matches 5000;
    }
  endgroup
  cg cov = new(512'h2_0000_0000_0000_0000);
endmodule
