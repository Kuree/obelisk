// RUN: %target_clang %target_pic -c %S/Inputs/two_state_dpi.c -I%resource_dir/include -o %t.o
// RUN: obelisk --two-state -O3 %s %t.o -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk --two-state -O3 --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk --two-state --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h
// RUN: obelisk --emit-dpi-header %s | diff -u %t.h -

// Two-state execution must preserve the declared C representation, including
// svLogicVecVal's interleaved aval/bval words, and project foreign X/Z to zero.
// HEADER: int32_t two_state_dpi(const svLogicVecVal *arg0, const svLogicVecVal *arg1, svLogicVecVal *arg2, svLogicVecVal *arg3, svLogicVecVal *arg4);
// CHECK: two-state DPI passed
module top;
  typedef struct packed { logic [31:0] high; bit [31:0] low; } packet_t;
  typedef struct { logic [31:0] vector; logic flag; } record_t;
  import "DPI-C" function int two_state_dpi(
      input logic [64:0] source, input time timestamp,
      output logic [64:0] destination, output integer number,
      inout packet_t packet);
  import "DPI-C" function logic two_state_scalar(input logic source,
                                                 output logic destination);
  import "DPI-C" function int two_state_aggregate(input record_t source,
                                                  output record_t destination);
  import "DPI-C" context function int two_state_export_check();
  import "DPI-C" function int two_state_open(inout logic [64:0] values[]);
  export "DPI-C" function two_state_export;
  function logic two_state_export(input logic flag, input logic [64:0] source,
                                  output logic [64:0] destination);
    destination = source + 1;
    return flag;
  endfunction
  logic [64:0] source, destination;
  integer number;
  time timestamp;
  packet_t packet;
  logic scalar;
  record_t record_in, record_out;
  logic [64:0] open_values[2];
  logic [64:0] dynamic_values[];
  initial begin
    source = 65'h0_12345678_abcdef01;
    timestamp = 64'h123456789abcdef0;
    packet = 64'h9876543212345678;
    if (two_state_dpi(source, timestamp, destination, number, packet) != 1)
      $fatal(1, "two-state C input layout");
    if (destination !== 65'h10000000000000001 || number !== 32'h12340000 ||
        packet !== 64'h55555555aaaaaaaa)
      $fatal(1, "two-state C output projection");
    if (two_state_scalar(1'bx, scalar) !== 0 || scalar !== 0)
      $fatal(1, "scalar DPI projection");
    record_in = '{32'h12345678, 1'bz};
    if (two_state_aggregate(record_in, record_out) != 1 ||
        record_out.vector !== 32'h12340000 || record_out.flag !== 0)
      $fatal(1, "aggregate DPI projection");
    if (two_state_export_check() != 1)
      $fatal(1, "export DPI projection");
    open_values = '{65'd1, 65'd2};
    dynamic_values = new[2];
    dynamic_values[0] = 1;
    dynamic_values[1] = 2;
    if (two_state_open(open_values) != 1 ||
        open_values[0] !== 65'h1_00000000_00000007 ||
        two_state_open(dynamic_values) != 1 ||
        dynamic_values[0] !== 65'h1_00000000_00000007)
      $fatal(1, "open-array DPI layout or projection");
    $display("two-state DPI passed");
    $finish;
  end
endmodule
