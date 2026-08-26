// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG

module dpi_export_leaf;
  function int exported(input int value);
    return value;
  endfunction
  export "DPI-C" exported_c = function exported;
endmodule

module dpi_export;
  dpi_export_leaf left();
  dpi_export_leaf right();
endmodule

// SLANG-DAG: dpi_export_c_identifier = "exported_c"{{.*}}hierarchical_name = "dpi_export.left.exported"
// SLANG-DAG: dpi_export_c_identifier = "exported_c"{{.*}}hierarchical_name = "dpi_export.right.exported"
