// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: FileCheck %s --check-prefix=EMPTY --allow-empty < %t.o3.native.err

module native_finish;
  int side;

  function automatic int finish_level();
    $display("argument");
    side++;
    return 0;
  endfunction

  function automatic int inner_finish();
    $display("in-function");
    $finish(finish_level());
    $display("inner-after");
    return 7;
  endfunction

  function automatic int outer_finish();
    int value;
    value = inner_finish();
    $display("outer-after value=%0d", value);
    return value;
  endfunction

  initial begin
    #1;
    $display("before");
    side = outer_finish();
    $display("caller-after side=%0d", side);
  end

  initial begin
    #2;
    $display("late");
  end

  final $display("final side=%0d", side);
endmodule

// STDOUT: before
// STDOUT-NEXT: in-function
// STDOUT-NEXT: argument
// STDOUT-NEXT: final side=1
// STDOUT-NOT: inner-after
// STDOUT-NOT: outer-after
// STDOUT-NOT: caller-after
// STDOUT-NOT: late
// EMPTY-NOT: {{.}}
