// RUN: talos -O0 --vpi=off --native-scheduler=eval %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: talos -O3 --vpi=full --native-scheduler=eval %s -o %t.full
// RUN: %t.full | FileCheck %s
// RUN: talos -O3 --vpi=full --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// CHECK: containers passed

module native_state_containers;
  logic [20:0] value;
  logic sentinel;
  logic [36:0] wide;
  logic [48:0] other;
  logic [8:0] tail;
  int index;
  initial begin
    value = '1;
    sentinel = 1;
    wide = '1;
    other = '1;
    tail = '1;
    for (index = 0; index < 21; index++) begin
      value[index] = 0;
      #1;
      if (value !== (21'h1fffff << (index + 1))) $fatal(1, "bit update");
      if (sentinel !== 1 || wide !== '1 || other !== '1 || tail !== '1)
        $fatal(1, "neighbor overwritten");
    end
    index = 20;
    value[index +: 3] = 3'bz01;
    if (value[20] !== 1 || value[19:0] !== 0) $fatal(1, "clipped write");
    index = 21;
    value[index] = 0;
    if (value[index] !== 1'bx || value[20] !== 1) $fatal(1, "padding exposed");
    index = -1;
    value[index] = 0;
    if (value[index] !== 1'bx) $fatal(1, "negative select");
    value = 'z;
    wide = 'x;
    other = 'z;
    tail = 'x;
    #1;
    if (value !== 'z || wide !== 'x || other !== 'z || tail !== 'x || sentinel !== 1)
      $fatal(1, "four-state or tail store");
    $display("containers passed");
    $finish;
  end
endmodule
