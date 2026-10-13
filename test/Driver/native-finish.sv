// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=EMPTY --allow-empty < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

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
