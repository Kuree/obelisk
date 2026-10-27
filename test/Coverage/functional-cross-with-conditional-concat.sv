// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 --test-talos-print-coverage-schema -o %t.mlir 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: FileCheck %s --check-prefix=SIM < %t.mlir
// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON

// Compiler (11.4.11, 11.4.12): a conditional is one variadic node holding its
// condition(s) then both arms, so several &&& conditions do not duplicate the
// arms; a concatenation is one node whose operands keep source order and their
// self-determined widths (33+32+1 and 4+1+4+1+4+57).
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=3 operands=[[C1:[0-9]+]],[[C2:[0-9]+]],[[C3:[0-9]+]] {{.*}} kind=31 width=65
// SCHEMA-DAG: cross_predicate id=[[C1]] {{.*}} width=1
// SCHEMA-DAG: cross_predicate id=[[C2]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id=[[C3]] {{.*}} width=65
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=3 operands=[[D1:[0-9]+]],[[D2:[0-9]+]],[[D3:[0-9]+]] {{.*}} kind=31 width=4
// SCHEMA-DAG: cross_predicate id=[[D1]] {{.*}} width=2
// SCHEMA-DAG: cross_predicate id=[[D2]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id=[[D3]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=4 operands=[[M1:[0-9]+]],[[M2:[0-9]+]],[[M3:[0-9]+]],[[M4:[0-9]+]] {{.*}} kind=31 width=4
// SCHEMA-DAG: cross_predicate id=[[M1]] {{.*}} width=2
// SCHEMA-DAG: cross_predicate id=[[M2]] {{.*}} width=1
// SCHEMA-DAG: cross_predicate id=[[M3]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id=[[M4]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=3 operands=[[K1:[0-9]+]],[[K2:[0-9]+]],[[K3:[0-9]+]] {{.*}} kind=32 width=66
// SCHEMA-DAG: cross_predicate id=[[K1]] {{.*}} width=33
// SCHEMA-DAG: cross_predicate id=[[K2]] {{.*}} width=32
// SCHEMA-DAG: cross_predicate id=[[K3]] {{.*}} width=1
// SCHEMA-DAG: cross_predicate id={{[0-9]+}} {{.*}} operand_count=6 operands=[[W1:[0-9]+]],[[W2:[0-9]+]],[[W3:[0-9]+]],[[W4:[0-9]+]],[[W5:[0-9]+]],[[W6:[0-9]+]] {{.*}} kind=32 width=71
// SCHEMA-DAG: cross_predicate id=[[W1]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id=[[W2]] {{.*}} width=1
// SCHEMA-DAG: cross_predicate id=[[W3]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id=[[W4]] {{.*}} width=1
// SCHEMA-DAG: cross_predicate id=[[W5]] {{.*}} width=4
// SCHEMA-DAG: cross_predicate id=[[W6]] {{.*}} width=57
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with
// SIM: simulation.covergroup.create
// SIM-NOT: simulation.covergroup.prepare
// SIM-NOT: simulation.covergroup.candidates
// SIM-NOT: simulation.covergroup.bind_with

// IEEE 1800-2023 11.4.11, 11.4.12 and 19.6.1.2. Operands keep source
// order, exact four-state planes, and their self-determined bit widths.
// Selection counts cover the 2^66-value domain without tuple enumeration.
module top;
  bit [64:0] value;
  bit select;
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    bit cb: coverpoint select { bins all = {[0:1]}; }
    conditional: cross ca, cb {
      bins selected = conditional with ((cb ? ca : ~ca) == 65'b0) matches 2;
      bins too_many = conditional with ((cb ? ca : ~ca) == 65'b0) matches 3;
    }
    concatenation: cross ca, cb {
      bins selected = concatenation with ({ca[64:32], ca[31:0], cb} === 66'b0) matches 1;
      bins too_many = concatenation with ({ca[64:32], ca[31:0], cb} === 66'b0) matches 2;
    }
    many_operands: cross ca, cb {
      bins selected = many_operands with (
          {ca[3:0], cb, ca[7:4], cb, 4'bzxx0, ca[64:8]} ===
          {4'b0, 1'b0, 4'b0, 1'b0, 4'bzxx0, 57'b0}) matches 1;
      bins too_many = many_operands with (
          {ca[3:0], cb, ca[7:4], cb, 4'bzxx0, ca[64:8]} ===
          {4'b0, 1'b0, 4'b0, 1'b0, 4'bzxx0, 57'b0}) matches 2;
    }
  endgroup
  covergroup unknown_group(logic [1:0] mask);
    option.cross_retain_auto_bins = 0;
    logic [64:0] ca: coverpoint value { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    bit cb: coverpoint select { bins all = {[0:1]}; }
    merged: cross ca, cb {
      bins selected = merged with (
          (mask & 2'(cb) ? 4'bz1x0 : 4'bz110) === 4'bx1x0)
          matches 128'h2_0000_0000_0000_0000;
    }
    false_arm: cross ca, cb {
      bins selected = false_arm with (
          (mask & 2'(cb) ? 4'bz1x0 : 4'bz110) === 4'bz110)
          matches 128'h2_0000_0000_0000_0000;
    }
    true_arm: cross ca, cb {
      bins selected = true_arm with (
          (mask & {cb, cb} ? 4'bz1x0 : 4'bz110) === 4'bz1x0)
          matches 128'h2_0000_0000_0000_0000;
    }
    z_merge: cross ca, cb {
      bins selected = z_merge with (
          (mask & {cb, cb} ? 4'bzzzz : 4'bzzzz) === 4'bxxxx)
          matches 128'h2_0000_0000_0000_0000;
    }
    multiple_conditions: cross ca, cb {
      bins selected = multiple_conditions with (
          (mask & 2'(cb) &&& ca[0] ? 4'bz1x0 : 4'bz110) === 4'bz110)
          matches 128'h3_0000_0000_0000_0000;
    }
  endgroup
  cg cov;
  unknown_group unknown_x, unknown_z, known_one;
  initial begin
    cov = new;
    unknown_x = new(2'b0x);
    unknown_z = new(2'b0z);
    known_one = new(2'b1x);
    cov.sample();
    unknown_x.sample();
    unknown_z.sample();
    known_one.sample();
    $display("conditional %0.0f concatenation %0.0f many %0.0f",
             cov.conditional.get_inst_coverage(), cov.concatenation.get_inst_coverage(),
             cov.many_operands.get_inst_coverage());
    $display("merged %0.0f %0.0f", unknown_x.merged.get_inst_coverage(),
             unknown_z.merged.get_inst_coverage());
    $display("false %0.0f %0.0f %0.0f", unknown_x.false_arm.get_inst_coverage(),
             unknown_z.false_arm.get_inst_coverage(), known_one.false_arm.get_inst_coverage());
    $display("true %0.0f %0.0f %0.0f", unknown_x.true_arm.get_inst_coverage(),
             unknown_z.true_arm.get_inst_coverage(), known_one.true_arm.get_inst_coverage());
    $display("Z merge %0.0f %0.0f %0.0f", unknown_x.z_merge.get_inst_coverage(),
             unknown_z.z_merge.get_inst_coverage(), known_one.z_merge.get_inst_coverage());
    $display("conditions %0.0f %0.0f %0.0f", unknown_x.multiple_conditions.get_inst_coverage(),
             unknown_z.multiple_conditions.get_inst_coverage(), known_one.multiple_conditions.get_inst_coverage());
    $finish;
  end
endmodule
// Counts over the 2^66-value tuple domain (ca x cb in {0,1}). cb ? ca : ~ca
// is zero for (ca=0,cb=1) and (ca=all ones,cb=0): 2 tuples. Each concatenation
// is zero only for ca=0,cb=0: 1 tuple. The too_many bins ask for one more.
// Unknown groups, 11.4.11 and Table 11-20: with mask bit 0 = x or z, cond is
// ambiguous when cb=1 and the arms 4'bz1x0 / 4'bz110 merge to 4'bx1x0 (z/z
// also merges to x); cb=0 selects the false arm. A condition with a known 1
// bit (mask 2'b1x, {cb,cb}) is true, so the true arm is returned. With &&&
// a false clause (ca[0]==0) makes the predicate false even if the first
// clause is ambiguous: 2^65 + 2^64 tuples return the false arm.
// CHECK: conditional 100 concatenation 100 many 100
// CHECK-NEXT: merged 100 100
// CHECK-NEXT: false 100 100 100
// CHECK-NEXT: true 0 0 100
// CHECK-NEXT: Z merge 100 100 0
// CHECK-NEXT: conditions 100 100 100

// A cross whose with expression selects no tuple has an empty selected bin.
// REPORT: functional: 100.00% (23/23)
// REPORT-DAG: cross conditional: 1/1 (100.00%)
// REPORT-DAG: cross concatenation: 1/1 (100.00%)
// REPORT-DAG: cross many_operands: 1/1 (100.00%)
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: bin too_many: 0 [excluded] {empty}
// REPORT-DAG: cross true_arm: 0/0 (0.00%)
// REPORT-DAG: bin selected: 0 [excluded] {empty}
// REPORT-DAG: cross z_merge: 0/0 (0.00%)
// REPORT-DAG: bin selected: 0 [excluded] {empty}

// JSON-DAG: "hierarchy":"top.cg.conditional.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.concatenation.too_many","kind":3,"flags":128
// JSON-DAG: "hierarchy":"top.cg.many_operands.too_many","kind":3,"flags":128
// JSON: "resolved_functional_tuples":[]
