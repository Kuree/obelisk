// RUN: not obelisk -c --vpi=off %s -o %t.o 2>&1 | FileCheck %s

module dpi_signature_conflict_a;
  import "DPI-C" shared_name = function int first(input int value);
endmodule

module dpi_signature_conflict_b;
  import "DPI-C" shared_name = function longint second(input longint value);
endmodule

// CHECK: more than one DPI subroutine with C identifier 'shared_name' declared with mismatching type signatures
