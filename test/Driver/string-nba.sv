// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module string_nba;
  string value;

  initial begin
    value <= #1 {"delayed ", "value"};
    value <= #2 {"delayed v", "alue"};
    #3 $display("%s", value);
  end
endmodule

// CHECK: delayed value
