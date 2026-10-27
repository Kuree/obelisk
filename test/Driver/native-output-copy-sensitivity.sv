// RUN: talos -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval %s -o %t.eval
// RUN: %t.eval | FileCheck %s
// IEEE 1800-2023 9.2.2.2.1 excludes writes through output/inout function
// arguments from implicit sensitivity. Snapshots taken before those calls
// must not be overwritten by an extra activation from the copy-out itself.
// CHECK: before=xx/xx/xx after=10/11/12
// CHECK-NEXT: before=10/11/12 after=20/21/22
// CHECK-NEXT: before=20/21/22 after=xx/xx/xx
// CHECK-NEXT: before=xx/xx/xx after=30/31/32
module top;
  logic [7:0] source, scalar, vector[2];
  logic [7:0] before_scalar, before_first, before_second;
  function automatic void assign_value(output logic [7:0] target,
                                       input logic [7:0] value);
    target = value;
  endfunction
  function automatic void update_value(inout logic [7:0] target,
                                       input logic [7:0] value);
    target = value;
  endfunction
  always_comb begin
    before_scalar = scalar;
    before_first = vector[0];
    before_second = vector[1];
    assign_value(scalar, source);
    for (int i = 0; i < 2; i++)
      update_value(vector[i], source + 8'(i + 1));
  end
  task automatic show;
    $display("before=%h/%h/%h after=%h/%h/%h", before_scalar,
             before_first, before_second, scalar, vector[0], vector[1]);
  endtask
  initial begin
    source = 'h10;
    #1 show();
    source = 'h20;
    #1 show();
    source = 'x;
    #1 show();
    source = 'h30;
    #1 show();
    $finish;
  end
endmodule
