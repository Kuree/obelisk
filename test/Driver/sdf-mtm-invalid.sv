// RUN: cd %S && not obelisk -emit-slang %s -o %t.mlir 2>&1 | FileCheck %s

module sdf_mtm_dummy;
endmodule

module sdf_mtm_invalid;
  sdf_mtm_dummy dut();
  initial begin
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", dut, "", "", "FASTEST");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", dut, "config.sdfcfg");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", dut,
                  "", "", "TOOL_CONTROL", "2:2:2");
    $sdf_annotate("Inputs/sdf-mtm-selection.sdf", dut, "", "", "");
  end
endmodule

// CHECK-DAG: error: invalid $sdf_annotate mtm_spec; expected MINIMUM, TYPICAL, MAXIMUM, or TOOL_CONTROL
// CHECK-DAG: error: invalid $sdf_annotate mtm_spec; expected MINIMUM, TYPICAL, MAXIMUM, or TOOL_CONTROL
// CHECK-DAG: error: static $sdf_annotate does not support nonempty config_file or log_file arguments
// CHECK-DAG: error: static $sdf_annotate does not support scale_factors or scale_type arguments
