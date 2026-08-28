// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module dpi_deprecated_export_unsupported;
  function int legacy_export(input int value);
    return value;
  endfunction
  export "DPI" function legacy_export;
endmodule

// CHECK: legacy SystemVerilog 3.1a `DPI` exports are unsupported; use `DPI-C`
