// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir

`timescale 1ns / 1ns

module sdf_mtm_cell(input wire source, output wire destination);
  assign destination = source;
  specify (source => destination) = 100; endspecify
endmodule

module sdf_mtm_container(input wire source, output wire destination);
  sdf_mtm_cell dut(source, destination);
endmodule

module sdf_mtm_selection;
  logic source;
  wire minimum_destination;
  wire typical_destination;
  wire maximum_destination;
  wire tool_destination;
  wire default_destination;
  sdf_mtm_container minimum(source, minimum_destination);
  sdf_mtm_container typical(source, typical_destination);
  sdf_mtm_container maximum(source, maximum_destination);
  sdf_mtm_container tool(source, tool_destination);
  sdf_mtm_container default_selection(source, default_destination);

  initial begin
    // IEEE 1800-2017 32.9, Table 32-5: selection is per annotation call,
    // including repeated uses of one cached SDF file at disjoint roots.
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", minimum,
                  "", "", "MINIMUM");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", typical,
                  "", "", "TYPICAL");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", maximum,
                  "", "", "MAXIMUM");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", tool,
                  "", "", "TOOL_CONTROL");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", default_selection);
  end
endmodule

// CHECK-DAG: hierarchical_name = "sdf_mtm_selection.minimum.dut"{{.*}}timing_delay_count = 1 : i64, timing_delay_fs = array<i64: 1000000>
// CHECK-DAG: hierarchical_name = "sdf_mtm_selection.typical.dut"{{.*}}timing_delay_count = 1 : i64, timing_delay_fs = array<i64: 2000000>
// CHECK-DAG: hierarchical_name = "sdf_mtm_selection.maximum.dut"{{.*}}timing_delay_count = 1 : i64, timing_delay_fs = array<i64: 3000000>
// CHECK-DAG: hierarchical_name = "sdf_mtm_selection.tool.dut"{{.*}}timing_delay_count = 1 : i64, timing_delay_fs = array<i64: 2000000>
// CHECK-DAG: hierarchical_name = "sdf_mtm_selection.default_selection.dut"{{.*}}timing_delay_count = 1 : i64, timing_delay_fs = array<i64: 2000000>
// CHECK-NOT: obelisk_sdf.
// CHECK-NOT: obelisk.sdf.table
