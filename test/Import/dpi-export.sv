// RUN: talos -emit-slang %s | FileCheck %s --check-prefix=SLANG

module dpi_export_leaf;
  function int exported(input int value);
    return value;
  endfunction
  export "DPI-C" exported_c = function exported;
endmodule

// IEEE 1800-2023 35.5.3: another scope may export a function with an
// equivalent signature under the same C identifier.
module dpi_export_other;
  function int other(input int value);
    return value + 1;
  endfunction
  export "DPI-C" exported_c = function other;
endmodule

module dpi_export;
  dpi_export_leaf left();
  dpi_export_leaf right();
  dpi_export_other another();
endmodule

// SLANG-DAG: dpi_export_c_identifier = "exported_c"{{.*}}hierarchical_name = "dpi_export.left.exported"
// SLANG-DAG: dpi_export_c_identifier = "exported_c"{{.*}}hierarchical_name = "dpi_export.right.exported"
// SLANG-DAG: dpi_export_c_identifier = "exported_c"{{.*}}hierarchical_name = "dpi_export.another.other"
