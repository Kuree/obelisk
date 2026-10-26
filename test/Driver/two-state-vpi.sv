// REQUIRES: native-elf
// RUN: %target_clang -fPIC -shared -nostdlib %S/Inputs/two_state_vpi.c -I%resource_dir/include -o %t.so
// RUN: obelisk --two-state --vpi=full -O0 --native-scheduler=generic %s %t.so -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk --two-state --vpi=full -O3 %s %t.so -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk --two-state --vpi=full --execution-tier=bytecode -O3 %s %t.so -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk --two-state --vpi=read -emit-sim %s | FileCheck %s --check-prefix=TYPE

// VPI keeps source identities, including time and frozen parameter types,
// while native and bytecode execution use only the value plane.
// CHECK: two-state VPI passed
// TYPE-DAG: hierarchy "top.legacy" {{.*}}kind = integer, isSigned = true, isFourState = true
// TYPE-DAG: hierarchy "top.timestamp" {{.*}}kind = time, isSigned = false, isFourState = true
package values;
  localparam logic [7:0] PARAM = 8'hxz;
  localparam time TIME_PARAM = 12;
endpackage
module top;
  import values::*;
  import "DPI-C" context function int check_two_state_vpi();
  typedef enum logic [1:0] { IDLE, ACTIVE } state_t;
  typedef struct packed { logic [7:0] data; bit valid; } packet_t;
  logic scalar = 1;
  reg flag = 1;
  bit declared_bit = 1;
  integer legacy = 7;
  time timestamp = 9;
  logic [64:0] wide = 65'h1_12345678_abcdef01;
  state_t state = ACTIVE;
  packet_t packet = '{data: 8'h5a, valid: 1};
  typedef logic [7:0] message_t [2];
  mailbox #(logic [7:0]) messages;
  mailbox #(message_t) array_messages;
  mailbox boxed_messages;
  initial begin
    message_t payload;
    payload = '{8'h41, 8'h82};
    messages = new;
    array_messages = new;
    boxed_messages = new;
    messages.put(8'hxz);
    array_messages.put(payload);
    boxed_messages.put(8'h5a);
    if (PARAM !== 0 || TIME_PARAM !== 12) $fatal(1, "projected parameters");
    if (check_two_state_vpi()) $fatal(1, "VPI reflection or write projection");
    if (scalar !== 0 || flag !== 0 || wide !== 65'h1_00000000_00001234)
      $fatal(1, "VPI X/Z projection");
    $display("two-state VPI passed");
    $finish;
  end
endmodule
