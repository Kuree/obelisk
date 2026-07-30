// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG

module dpi_imports;
  import "DPI-C" pure c_add = function int sv_add(input int value);
  import "DPI-C" context task update(input logic [64:0] source,
                                     inout bit [32:0] destination);

  int result;
  logic [64:0] source;
  bit [32:0] destination;

  initial begin
    result = sv_add(7);
    update(source, destination);
  end
endmodule

// SLANG: slang.symbol.subroutine
// SLANG-SAME: dpi_c_identifier = "c_add"
// SLANG-SAME: is_dpi_import
// SLANG-SAME: is_pure
// SLANG: slang.symbol.subroutine
// SLANG-SAME: dpi_c_identifier = "update"
// SLANG-SAME: is_dpi_context
// SLANG-SAME: is_dpi_import
// SLANG-SAME: subroutine_kind = 1
