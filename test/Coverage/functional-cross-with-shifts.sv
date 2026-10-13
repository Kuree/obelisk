// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 --test-obelisk-print-coverage-schema -o %t.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir
// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// Compiler (11.4.10): each shift is one binary node (kinds 33-36 for <<, >>,
// <<<, >>>) over the left operand's context width, so the 65-bit result is
// kept; the count operand keeps its own self-determined width (8, 129, or the
// 2-bit select) and is never widened or truncated to the result. The
// arithmetic right shift records whether its context is signed.
// SCHEMA-DAG: cross_predicate id=[[W129:[0-9]+]] {{.*}} kind=2 width=129
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands={{[0-9]+}},[[W129]] {{.*}} kind=34 width=65
// SCHEMA-DAG: cross_predicate id=[[SEL2:[0-9]+]] {{.*}} kind=23 width=2
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands={{[0-9]+}},[[SEL2]] {{.*}} kind=34 width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=2 operands={{[0-9]+}},[[SHL:[0-9]+]] {{.*}} kind=33 width=65
// SCHEMA-DAG: cross_predicate id=[[SHL]] {{.*}} kind=2 width=8
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=2 operands={{[0-9]+}},{{[0-9]+}} {{.*}} kind=35 width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=2 operands={{[0-9]+}},{{[0-9]+}} {{.*}} kind=36 width=65 signedness=1
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=2 operands=[[SGN:[0-9]+]],{{[0-9]+}} {{.*}} kind=36 width=65 signedness=2
// SCHEMA-DAG: cross_predicate id=[[SGN]] {{.*}} kind=2 width=65 signedness=2
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operands=[[P5:[0-9]+]],[[PC:[0-9]+]] {{.*}} kind=34 width=5
// SCHEMA-DAG: cross_predicate id=[[P5]] {{.*}} width=5
// SCHEMA-DAG: cross_predicate id=[[PC]] {{.*}} width=8
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} kind=36 width=5 signedness=1
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} kind=36 width=5 signedness=2
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

// IEEE 1800-2023 11.4.10 and 19.6.1.2. Counts are unsigned and retain
// their self-determined width; result context controls arithmetic right-fill.
// Broad 65-bit domains resolve symbolically, including oversized counts.
module top;
  bit [64:0] value;
  bit signed [64:0] signed_value;
  bit signed [7:0] position;
  bit [128:0] wide_position;
  bit [1:0] unknown_position;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic signed [7:0] ci: coverpoint position { bins one = {1}; bins huge = {65, -1}; }
    logical_left: cross ca, ci {
      bins selected = binsof(ci.one) && logical_left with ((ca << ci) === 65'b0) matches 2;
      bins too_many = binsof(ci.one) && logical_left with ((ca << ci) === 65'b0) matches 3;
      bins overflow = binsof(ci.huge) && logical_left with ((ca << ci) === 65'b0) matches $;
    }
    logical_right: cross ca, ci {
      bins selected = binsof(ci.one) && logical_right with ((ca >> ci) === 65'b0) matches 2;
      bins too_many = binsof(ci.one) && logical_right with ((ca >> ci) === 65'b0) matches 3;
      bins overflow = binsof(ci.huge) && logical_right with ((ca >> ci) === 65'b0) matches $;
    }
    arithmetic_left: cross ca, ci {
      bins selected = binsof(ci.one) && arithmetic_left with ((ca <<< ci) === 65'b0) matches 2;
      bins too_many = binsof(ci.one) && arithmetic_left with ((ca <<< ci) === 65'b0) matches 3;
      bins overflow = binsof(ci.huge) && arithmetic_left with ((ca <<< ci) === 65'b0) matches $;
    }
    unsigned_arithmetic_right: cross ca, ci {
      bins selected = binsof(ci.one) && unsigned_arithmetic_right with ((ca >>> ci) === 65'b0) matches 2;
      bins too_many = binsof(ci.one) && unsigned_arithmetic_right with ((ca >>> ci) === 65'b0) matches 3;
      bins overflow = binsof(ci.huge) && unsigned_arithmetic_right with ((ca >>> ci) === 65'b0) matches $;
    }
  endgroup
  covergroup signed_group;
    option.cross_retain_auto_bins = 0;
    logic signed [64:0] cs: coverpoint signed_value {
      bins negative = {[65'sh1_0000_0000_0000_0000:-65'sb1]};
    }
    logic signed [7:0] ci: coverpoint position { bins huge = {65, -1}; }
    sign_fill: cross cs, ci {
      bins selected = sign_fill with ((cs >>> ci) === -65'sb1) matches $;
    }
    zero_fill: cross cs, ci {
      bins selected = zero_fill with ((cs >> ci) === 65'b0) matches $;
    }
    signed_context: cross cs, ci {
      bins selected = signed_context with (((cs >>> ci) + 65'sb0) === -65'sb1) matches $;
    }
    unsigned_context: cross cs, ci {
      bins selected = unsigned_context with (((cs >>> ci) + 65'b0) === 65'b0) matches $;
    }
  endgroup
  covergroup wide_group;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [128:0] ci: coverpoint wide_position {
      bins huge = {129'h1_0000_0000_0000_0000_0000_0000_0000_0000};
    }
    product: cross ca, ci {
      bins selected = product with ((ca >> ci) === 65'b0) matches $;
    }
  endgroup
  covergroup unknown_group;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic [1:0] ci: coverpoint unknown_position { bins nonzero = {[1:3]}; bins zero = {0}; }
    product: cross ca, ci {
      bins selected = product with ((ca >> ci[ci +: 2]) === 65'bx) matches $;
    }
  endgroup
  covergroup planes_group(logic [3:0] mask);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    logic signed [7:0] ci: coverpoint position { bins one = {1}; }
    logical: cross ca, ci {
      bins selected = logical with (({mask, ca[0]} >> ci) === 5'b0z1x0) matches $;
    }
    arithmetic_unsigned: cross ca, ci {
      bins selected = arithmetic_unsigned with (({mask, ca[0]} >>> ci) === 5'b0z1x0) matches $;
    }
    arithmetic_signed: cross ca, ci {
      bins selected = arithmetic_signed with ((signed'({mask, ca[0]}) >>> ci) === 5'sbzz1x0) matches $;
    }
  endgroup
  cg cov;
  signed_group negative;
  wide_group wide;
  unknown_group unknown;
  planes_group planes;
  initial begin
    cov = new;
    negative = new;
    wide = new;
    unknown = new;
    planes = new(4'bz1x0);
    position = 1;
    cov.sample();
    planes.sample();
    position = 65;
    signed_value = -65'sb1;
    cov.sample();
    negative.sample();
    wide_position = 129'h1_0000_0000_0000_0000_0000_0000_0000_0000;
    wide.sample();
    unknown_position = 1;
    unknown.sample();
    $display("logical %0.0f %0.0f arithmetic %0.0f %0.0f",
             cov.logical_left.get_inst_coverage(), cov.logical_right.get_inst_coverage(),
             cov.arithmetic_left.get_inst_coverage(), cov.unsigned_arithmetic_right.get_inst_coverage());
    $display("signed %0.0f zero %0.0f contexts %0.0f %0.0f",
             negative.sign_fill.get_inst_coverage(), negative.zero_fill.get_inst_coverage(),
             negative.signed_context.get_inst_coverage(), negative.unsigned_context.get_inst_coverage());
    $display("wide %0.0f unknown %0.0f", wide.product.get_inst_coverage(), unknown.product.get_inst_coverage());
    $display("planes %0.0f %0.0f %0.0f", planes.logical.get_inst_coverage(),
             planes.arithmetic_unsigned.get_inst_coverage(), planes.arithmetic_signed.get_inst_coverage());
    $finish;
  end
endmodule
// Counts are unsigned (11.4.10): ci = 1 satisfies (ca shifted by 1) === 0 for
// two ca values (0 and 2^64 for left shifts, 0 and 1 for right shifts), and
// ci = 65 or -1 (255) shifts everything out, so $ selects all tuples. A signed
// operand in a signed context fills with the sign bit (-1 stays -1); in an
// unsigned context, or with >>, it fills with zeros. A count of 2^128, or a
// count with x bits (ci[ci +: 2] runs past the select), gives zero / all x.
// Four-state operand planes shift as-is: z1x0 >> 1 = 0z1x, and an arithmetic
// shift of a signed value with sign bit z fills with z.
// CHECK: logical 100 100 arithmetic 100 100
// CHECK-NEXT: signed 100 zero 100 contexts 100 100
// CHECK-NEXT: wide 100 unknown 100
// CHECK-NEXT: planes 100 100 100

// REPORT: functional: 96.67% (28/29)
// REPORT-DAG: cross logical_left: 2/2 (100.00%)
// REPORT-DAG: cross logical_right: 2/2 (100.00%)
// REPORT-DAG: cross arithmetic_left: 2/2 (100.00%)
// REPORT-DAG: cross unsigned_arithmetic_right: 2/2 (100.00%)
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: cross sign_fill: 1/1 (100.00%)
// REPORT-DAG: cross zero_fill: 1/1 (100.00%)
// REPORT-DAG: cross signed_context: 1/1 (100.00%)
// REPORT-DAG: cross unsigned_context: 1/1 (100.00%)
// REPORT-DAG: cross product: 1/1 (100.00%)
// REPORT-DAG: cross product: 1/1 (100.00%)
// REPORT-DAG: cross logical: 1/1 (100.00%)
// REPORT-DAG: cross arithmetic_unsigned: 1/1 (100.00%)
// REPORT-DAG: cross arithmetic_signed: 1/1 (100.00%)
// REPORT-DAG: coverpoint ci: 1/2 (50.00%)
// REPORT-DAG: bin zero: 0 [uncovered]

// JSON-DAG: "hierarchy":"top.cg.logical_left.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.logical_right.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.arithmetic_left.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.unsigned_arithmetic_right.too_many","kind":3,"flags":128
// JSON: "resolved_functional_tuples":[]
