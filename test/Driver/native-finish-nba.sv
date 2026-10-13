// RUN: obelisk -O3 --native-scheduler=aot %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=EMPTY --allow-empty < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_finish_nba;
  logic value;

  initial begin
    value = 1'b0;
    value <= 1'b1;
    $finish(0);
  end

  final $display("final value=%0d", value);
endmodule

// STDOUT: final value=0
// EMPTY-NOT: {{.}}
