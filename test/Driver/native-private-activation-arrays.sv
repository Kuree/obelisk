// RUN: talos --vpi=off -O3 --native-scheduler=eval %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos --vpi=off -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

// Private arrays initialized by a bounded loop can be local to each evaluator
// invocation. Partial writes must retain state between invocations (LRM 6.21).
// Preserve four-state values and invalid array indices (LRM 7.4.5).
module native_private_activation_arrays;
  logic [2:0] source;
  logic enable;
  integer selected;
  logic [2:0] cells[4:36];
  logic [2:0] held[2];
  logic [2:0] reduced, read_value;
  logic [5:0] retained;
  always_comb begin
    for (int i = 4; i <= 36; ++i)
      cells[i] = source ^ 3'(i - 4);
    cells[selected] ^= 3'b101;
    reduced = 0;
    for (int i = 4; i <= 36; ++i)
      reduced ^= cells[i];
    read_value = cells[selected];
  end
  always_comb begin
    if (enable) begin
      held[0] = source;
      held[1] = ~source;
    end
    retained = {held[1], held[0]};
  end
  initial begin
    source = 0; enable = 1; selected = 4;
    #1 $display("%b %b %b", reduced, read_value, retained);
    source = 3; enable = 0; selected = 5;
    #1 $display("%b %b %b", reduced, read_value, retained);
    source = 'x; selected = 3;
    #1 $display("%b %b %b", reduced, read_value, retained);
    source = 7; enable = 1; selected = 37;
    #1 $display("%b %b %b", reduced, read_value, retained);
    source = 2; selected = 'x;
    #1 $display("%b %b %b", reduced, read_value, retained);
    selected = 36;
    #1 $display("%b %b %b", reduced, read_value, retained);
    $finish;
  end
endmodule
// CHECK: 101 101 111000
// CHECK-NEXT: 110 111 111000
// CHECK-NEXT: xxx xxx 111000
// CHECK-NEXT: 111 xxx 000111
// CHECK-NEXT: 010 xxx 101010
// CHECK-NEXT: 111 111 101010
