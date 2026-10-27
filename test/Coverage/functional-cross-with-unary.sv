// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 --test-talos-print-coverage-schema -o %t.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// Compiler (11.4.3, 11.4.9, 19.6.1.2): the negation and each reduction
// operator become one typed predicate node whose single operand keeps the
// operand's self-determined 65-bit width, and no value-tuple queue is built.
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[NEG:[0-9]+]] {{.*}} kind=24 width=65
// SCHEMA-DAG: cross_predicate id=[[NEG]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[RAND:[0-9]+]] {{.*}} kind=25 width=1
// SCHEMA-DAG: cross_predicate id=[[RAND]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[RNAND:[0-9]+]] {{.*}} kind=26 width=1
// SCHEMA-DAG: cross_predicate id=[[RNAND]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[ROR:[0-9]+]] {{.*}} kind=27 width=1
// SCHEMA-DAG: cross_predicate id=[[ROR]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[RNOR:[0-9]+]] {{.*}} kind=28 width=1
// SCHEMA-DAG: cross_predicate id=[[RNOR]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[RXOR:[0-9]+]] {{.*}} kind=29 width=1
// SCHEMA-DAG: cross_predicate id=[[RXOR]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[RXNOR:[0-9]+]] {{.*}} kind=30 width=1
// SCHEMA-DAG: cross_predicate id=[[RXNOR]] {{.*}} width=65
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

// IEEE 1800-2023 11.4.3, 11.4.9 and 19.6.1.2. Wide reductions and
// fixed-width negation select associated value tuples without enumerating
// the 2^65-value domain. The extra bins test each exact count's upper bound.
module top;
  bit [64:0] value;
  bit marker;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value {
      bins all = {[0:65'h1_ffff_ffff_ffff_ffff]};
    }
    bit cb: coverpoint marker { bins zero = {0}; }
    reduction_and: cross ca, cb {
      bins selected = reduction_and with ((&ca) === 1'b1) matches 1;
      bins too_many = reduction_and with ((&ca) === 1'b1) matches 2;
    }
    reduction_or: cross ca, cb {
      bins selected = reduction_or with ((|ca) === 1'b1)
          matches 128'h1_ffff_ffff_ffff_ffff;
      bins too_many = reduction_or with ((|ca) === 1'b1)
          matches 128'h2_0000_0000_0000_0000;
    }
    reduction_xor: cross ca, cb {
      bins selected = reduction_xor with ((^ca) === 1'b1)
          matches 128'h1_0000_0000_0000_0000;
      bins too_many = reduction_xor with ((^ca) === 1'b1)
          matches 128'h1_0000_0000_0000_0001;
    }
    reduction_nand: cross ca, cb {
      bins selected = reduction_nand with ((~&ca) === 1'b1)
          matches 128'h1_ffff_ffff_ffff_ffff;
      bins too_many = reduction_nand with ((~&ca) === 1'b1)
          matches 128'h2_0000_0000_0000_0000;
    }
    reduction_nor: cross ca, cb {
      bins selected = reduction_nor with ((~|ca) === 1'b1) matches 1;
      bins too_many = reduction_nor with ((~|ca) === 1'b1) matches 2;
    }
    reduction_xnor: cross ca, cb {
      bins selected = reduction_xnor with ((~^ca) === 1'b1)
          matches 128'h1_0000_0000_0000_0000;
      bins too_many = reduction_xnor with ((~^ca) === 1'b1)
          matches 128'h1_0000_0000_0000_0001;
    }
    negation: cross ca, cb {
      bins selected = negation with (-ca == ca) matches 2;
      bins too_many = negation with (-ca == ca) matches 3;
    }
  endgroup
  covergroup unknown_group(logic [64:0] mask);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value {
      bins all = {[0:65'h1_ffff_ffff_ffff_ffff]};
    }
    bit cb: coverpoint marker { bins zero = {0}; }
    known_zero: cross ca, cb {
      bins selected = known_zero with ((&(ca & mask)) === 1'b0) matches $;
    }
    known_one: cross ca, cb {
      bins selected = known_one with ((~&(ca & mask)) === 1'b1) matches $;
    }
    unknown_or: cross ca, cb {
      bins selected = unknown_or with ((|(ca & mask)) === 1'bx)
          matches 128'h1_0000_0000_0000_0000;
    }
    unknown_xor: cross ca, cb {
      bins selected = unknown_xor with ((^(ca & mask)) === 1'bx)
          matches 128'h1_0000_0000_0000_0000;
    }
    unknown_negate: cross ca, cb {
      bins selected = unknown_negate with (-(ca & mask) === 65'bx)
          matches 128'h1_0000_0000_0000_0000;
    }
  endgroup
  cg cov;
  unknown_group unknown_x, unknown_z;
  initial begin
    cov = new;
    unknown_x = new(65'b0x);
    unknown_z = new(65'b0z);
    cov.sample();
    unknown_x.sample();
    unknown_z.sample();
    $display("and %0.0f or %0.0f xor %0.0f",
             cov.reduction_and.get_inst_coverage(),
             cov.reduction_or.get_inst_coverage(), cov.reduction_xor.get_inst_coverage());
    $display("nand %0.0f nor %0.0f xnor %0.0f negate %0.0f",
             cov.reduction_nand.get_inst_coverage(),
             cov.reduction_nor.get_inst_coverage(), cov.reduction_xnor.get_inst_coverage(),
             cov.negation.get_inst_coverage());
    $display("X %0.0f %0.0f %0.0f %0.0f %0.0f",
             unknown_x.known_zero.get_inst_coverage(),
             unknown_x.known_one.get_inst_coverage(), unknown_x.unknown_or.get_inst_coverage(),
             unknown_x.unknown_xor.get_inst_coverage(), unknown_x.unknown_negate.get_inst_coverage());
    $display("Z %0.0f %0.0f %0.0f %0.0f %0.0f",
             unknown_z.known_zero.get_inst_coverage(),
             unknown_z.known_one.get_inst_coverage(), unknown_z.unknown_or.get_inst_coverage(),
             unknown_z.unknown_xor.get_inst_coverage(), unknown_z.unknown_negate.get_inst_coverage());
    $finish;
  end
endmodule
// Each cross has exactly one satisfying-count threshold: the selected bin's
// matches count equals the number of satisfying value tuples of the 2^65
// domain (e.g. |ca| holds for 2^65-1 values), so it is covered; the too_many
// bin asks for one more, selects nothing, and is excluded as empty (19.6.1.2).
// The unknown groups (mask bit 0 is x or z) check the four-state results of
// 11.4.8-11.4.9: a reduction over a zero bit is known, and any x bit makes
// |, ^ and unary minus unknown.
// CHECK: and 100 or 100 xor 100
// CHECK-NEXT: nand 100 nor 100 xnor 100 negate 100
// CHECK-NEXT: X 100 100 100 100 100
// CHECK-NEXT: Z 100 100 100 100 100

// REPORT: functional: 100.00% (23/23)
// REPORT-DAG: cross reduction_and: 1/1 (100.00%)
// REPORT-DAG: cross reduction_or: 1/1 (100.00%)
// REPORT-DAG: cross reduction_xor: 1/1 (100.00%)
// REPORT-DAG: cross reduction_nand: 1/1 (100.00%)
// REPORT-DAG: cross reduction_nor: 1/1 (100.00%)
// REPORT-DAG: cross reduction_xnor: 1/1 (100.00%)
// REPORT-DAG: cross negation: 1/1 (100.00%)
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: cross known_zero: 1/1 (100.00%)
// REPORT-DAG: cross known_one: 1/1 (100.00%)
// REPORT-DAG: cross unknown_or: 1/1 (100.00%)
// REPORT-DAG: cross unknown_xor: 1/1 (100.00%)
// REPORT-DAG: cross unknown_negate: 1/1 (100.00%)

// Every too_many bin carries the empty flag (128), and no value tuple is
// materialized: the selection is resolved symbolically.
// JSON-DAG: "hierarchy":"top.cg.reduction_and.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.reduction_or.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.reduction_xor.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.reduction_nand.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.reduction_nor.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.reduction_xnor.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.negation.too_many","kind":3,"flags":128
// JSON: "resolved_functional_tuples":[]
