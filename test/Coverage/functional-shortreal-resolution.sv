// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov > %t.out 2> %t.err
// RUN: FileCheck %s --check-prefix=QUERY < %t.out
// RUN: FileCheck %s --check-prefix=WARNING < %t.err
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT

// IEEE 1800-2023 19.5.7: lossy implicit casts exclude singleton bin values. Explicit
// casts retain their value, and ranges intersect the effective type's domain.
// Bin arrays use real_interval; queue set expressions retain element precision.
module top;
  shortreal actual;
  real wide = 0.1;
  shortreal narrow_values[$] = '{shortreal'(0.1), 0.5};
  real wide_values[$] = '{0.1, 0.5};
  int integer_values[$] = '{16777217, 16777216};
  covergroup cg;
    cp: coverpoint actual {
      bins decimal = {0.1};
      bins variable = {wide};
      bins explicit_cast = {shortreal'(0.1)};
      bins clipped = {[0.1:0.3]};
      bins outside = {[1.0e100:2.0e100]};
      bins no_values = {[1.0000000000000002:1.0000000000000004]};
    }
    integer_point: coverpoint actual {
      bins lossy_positive = {16777217};
      bins lossy_negative = {-16777217};
      bins exact_positive = {16777216};
      bins exact_negative = {-16777216};
      bins clipped_integer = {[16777217:16777219]};
      bins no_integer_values = {[16777217:16777217]};
      bins unknown_value = {32'hx};
    }
    open_point: coverpoint actual {
      type_option.real_interval = 0.25;
      bins values[] = {[0.0:0.5], 0.75};
    }
    fixed_point: coverpoint actual {
      type_option.real_interval = 0.25;
      bins values[3] = {[0.0:0.5], 0.75};
    }
    narrow_set: coverpoint actual { bins values = narrow_values; }
    wide_set: coverpoint actual { bins values = wide_values; }
    integer_set: coverpoint actual { bins values = integer_values; }
  endgroup
  cg value;
  initial begin
    value = new;
    actual = 0.1; value.sample();
    actual = 0.25; value.sample();
    actual = 0.5; value.sample();
    actual = 0.75; value.sample();
    actual = 16777216; value.sample();
    actual = -16777216; value.sample();
    actual = 16777218; value.sample();
    $display("coverage=%0.0f", value.get_inst_coverage());
    $finish;
  end
endmodule

// QUERY: coverage=100

// Lossy conversions, clipped ranges, out-of-domain ranges and X/Z elements each
// raise one diagnostic, in bin declaration order.
// WARNING: warning: functional bin real element changes value when converted to the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real element changes value when converted to the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real range is clipped to the effective coverpoint type
// WARNING-NEXT: warning: functional bin real range is outside the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real range has no values in the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real element changes value when converted to the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real element changes value when converted to the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real range is clipped to the effective coverpoint type
// WARNING-NEXT: warning: functional bin real range has no values in the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin real element contains X or Z and is ignored
// WARNING-NEXT: warning: functional bin set real element changes value when converted to the effective coverpoint type and is ignored
// WARNING-NEXT: warning: functional bin set real element changes value when converted to the effective coverpoint type and is ignored

// Bins with no value in the effective shortreal domain are excluded and empty;
// the others are covered. Coverpoint order inside an instance is not fixed.
// REPORT: functional: 100.00% (14/14)
// REPORT-DAG: bin decimal: 0 [excluded] {empty}
// REPORT-DAG: bin variable: 0 [excluded] {empty}
// REPORT-DAG: bin explicit_cast: 1 [covered]
// REPORT-DAG: bin clipped: 2 [covered]
// REPORT-DAG: bin outside: 0 [excluded] {empty}
// REPORT-DAG: bin no_values: 0 [excluded] {empty}
// REPORT-DAG: bin lossy_positive: 0 [excluded] {empty}
// REPORT-DAG: bin lossy_negative: 0 [excluded] {empty}
// REPORT-DAG: bin exact_positive: 1 [covered]
// REPORT-DAG: bin exact_negative: 1 [covered]
// REPORT-DAG: bin clipped_integer: 1 [covered]
// REPORT-DAG: bin no_integer_values: 0 [excluded] {empty}
// REPORT-DAG: bin unknown_value: 0 [excluded] {empty}
// REPORT-DAG: coverpoint open_point: 3/3
// REPORT-DAG: coverpoint fixed_point: 3/3
// REPORT-DAG: coverpoint narrow_set: 1/1
// REPORT-DAG: coverpoint wide_set: 1/1
// REPORT-DAG: coverpoint integer_set: 1/1
// REPORT-DAG: bin values[0]: 1 [covered]
// REPORT-DAG: bin values[1]: 2 [covered]
// REPORT-DAG: bin values[2]: 1 [covered]
// REPORT-DAG: bin values[0.0:0.25): 1 [covered]
// REPORT-DAG: bin values[0.25:0.5]: 2 [covered]
// REPORT-DAG: bin values[0.75]: 1 [covered]
