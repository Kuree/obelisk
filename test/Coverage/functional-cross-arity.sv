// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=OUT
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.6: a cross enumerates every combination of its targets'
// bins in declaration order. This 64-target cross has only four combinations
// (two bins in each of the first two targets), so enumeration must stay
// proportional to the product size, not to the target count.
module top;
  bit sampled;
`define CP(n, b) cp``n: coverpoint sampled { b }
`define ONE(n) `CP(n, bins a = {0};)
`define ONE10(p) `ONE(p``0) `ONE(p``1) `ONE(p``2) `ONE(p``3) `ONE(p``4) `ONE(p``5) `ONE(p``6) `ONE(p``7) `ONE(p``8) `ONE(p``9)
`define NM(p) cp``p``0, cp``p``1, cp``p``2, cp``p``3, cp``p``4, cp``p``5, cp``p``6, cp``p``7, cp``p``8, cp``p``9
  covergroup cg;
    `CP(0, bins a = {0}; bins b = {0};)
    `CP(1, bins a = {0}; bins b = {0};)
    `ONE(2) `ONE(3) `ONE(4) `ONE(5) `ONE(6) `ONE(7) `ONE(8) `ONE(9)
    `ONE10(1) `ONE10(2) `ONE10(3) `ONE10(4) `ONE10(5)
    `ONE(60) `ONE(61) `ONE(62) `ONE(63)
    product: cross cp0, cp1, cp2, cp3, cp4, cp5, cp6, cp7, cp8, cp9,
      `NM(1), `NM(2), `NM(3), `NM(4), `NM(5), cp60, cp61, cp62, cp63;
  endgroup
  cg value;
  initial begin
    value = new;
    value.sample();
    $display("PRODUCT %0.2f", value.product.get_inst_coverage());
    $finish;
  end
endmodule

// OUT: PRODUCT 100.00
// REPORT: coverpoint cp0: 2/2 (100.00%)
// REPORT: cross product: 4/4 (100.00%)
// REPORT-NEXT: automatic: 4/4
// REPORT-DAG: auto bin <b,a,{{[a,]+}}>: 1 [covered, at_least 1]
// REPORT-DAG: auto bin <b,b,{{[a,]+}}>: 1 [covered, at_least 1]
// REPORT-DAG: auto bin <a,a,{{[a,]+}}>: 1 [covered, at_least 1]
// REPORT-DAG: auto bin <a,b,{{[a,]+}}>: 1 [covered, at_least 1]
