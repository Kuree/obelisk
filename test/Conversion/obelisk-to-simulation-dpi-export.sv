// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s --check-prefix=ERROR

module dpi_export;
  function int exported(input int value);
    return value;
  endfunction
  export "DPI-C" exported_c = function exported;
endmodule

// SLANG: slang.symbol.subroutine
// SLANG-SAME: dpi_export_c_identifier = "exported_c"
// ERROR: DPI export 'exported_c' is not supported by simulation lowering
