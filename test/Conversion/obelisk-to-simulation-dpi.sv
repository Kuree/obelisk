// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-sim %s | FileCheck %s --check-prefix=SIM

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

// SIM: obelisk_sim.dpi.call "c_add" id {{-?[0-9]+}} scope 1
// SIM-SAME: context
// SIM-SAME: kind = int
// SIM-SAME: direction = result
// SIM-SAME: is_pure = true
// SIM-SAME: source_file = "{{.*}}obelisk-to-simulation-dpi.sv"
// SIM-NEXT: obelisk_sim.status.check
// SIM: obelisk_sim.dpi.call "update" id {{-?[0-9]+}} scope 1
// SIM-SAME: kind = logic_vector
// SIM-SAME: width = 65
// SIM-SAME: kind = bit_vector
// SIM-SAME: direction = inout
// SIM-SAME: width = 33
// SIM-SAME: is_context = true
// SIM-SAME: is_task = true
// SIM-NEXT: obelisk_sim.status.check
