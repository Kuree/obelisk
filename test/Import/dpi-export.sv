// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG

module dpi_export;
  function int exported(input int value);
    return value;
  endfunction
  export "DPI-C" exported_c = function exported;
endmodule

// SLANG: slang.symbol.subroutine
// SLANG-SAME: dpi_export_c_identifier = "exported_c"
