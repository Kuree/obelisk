// RUN: cd %S && obelisk -emit-slang %s -o %t.slang.mlir 2>%t.err
// RUN: FileCheck %s --check-prefix=SLANG < %t.slang.mlir
// RUN: FileCheck %s --check-prefix=NO-SDF < %t.slang.mlir
// RUN: FileCheck %s --check-prefix=WARN < %t.err

module sdf_count_1(input wire source, output wire destination);
  timeunit 1ns;
  timeprecision 100fs;
  assign destination = source;
  specify (source => destination) = 99; endspecify
endmodule

module sdf_count_2(input wire source, output wire destination);
  timeunit 1ns;
  timeprecision 100fs;
  assign destination = source;
  specify (posedge source => destination) = 99; endspecify
endmodule

module sdf_count_3(input wire source, output wire destination);
  timeunit 1ns;
  timeprecision 100fs;
  assign destination = source;
  specify (source => destination) = 99; endspecify
endmodule

module sdf_count_6(input wire source, output wire destination);
  timeunit 1ns;
  timeprecision 100fs;
  assign destination = source;
  specify (source => destination) = 99; endspecify
endmodule

module sdf_count_12(input wire [3:0] source,
                    output wire [3:0] destination);
  timeunit 1ns;
  timeprecision 100fs;
  assign destination = source;
  specify (source[2] => destination[1]) = 99; endspecify
endmodule

module sdf_value_counts;
  timeunit 1ns;
  timeprecision 100fs;
  logic source;
  wire destination;
  logic [3:0] source_bus;
  wire [3:0] destination_bus;
  sdf_count_1 one(source, destination);
  sdf_count_2 two(source, destination);
  sdf_count_3 three(source, destination);
  sdf_count_6 six(source, destination);
  sdf_count_12 twelve(source_bus, destination_bus);

  initial begin
    // IEEE 1800-2017 32.9: with no module argument, relative INSTANCE names
    // are resolved below the scope containing the system task call.
    $sdf_annotate("Inputs/sdf-value-counts.sdf");
  end
endmodule

// SLANG-DAG: timing_delay_count = 1 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 1300>
// SLANG-DAG: timing_delay_count = 2 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 2000, 3000>
// SLANG-DAG: timing_delay_count = 3 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 4000, 5000, 6000>
// SLANG-DAG: timing_delay_count = 6 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 7000, 8000, 9000, 10000, 11000, 12000>
// SLANG-DAG: timing_delay_count = 12 : i64
// SLANG-DAG: timing_delay_fs = array<i64: 13000, 14000, 15000, 16000, 17000, 18000, 19000, 20000, 21000, 22000, 23000, 24000>
// NO-SDF: module{{.*}} {
// NO-SDF-NOT: slang.symbol.sdf
// NO-SDF-NOT: obelisk.sdf.table
// WARN: Inputs/sdf-value-counts.sdf:36:3: warning: SDF CELL did not match an elaborated instance
