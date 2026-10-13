// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out 2> %t.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode 2> %t.bytecode.err | diff -u %t.native.out -
// RUN: diff -u %t.native.err %t.bytecode.err

module native_finish_task;
  task automatic inner_finish();
    $display("in-task");
    // STDERR: $finish: {{.*}}native-finish-task.sv:[[# @LINE + 1]]: simulation time 0
    $finish;
    $display("inner-after");
  endtask

  task automatic outer_finish();
    inner_finish();
    $display("outer-after");
  endtask

  initial begin
    fork
      begin
        $display("before");
        outer_finish();
        $display("caller-after");
      end
      begin
        #1;
        $display("late-fork");
      end
    join
    $display("join-after");
  end

  final $display("final");
endmodule

// STDOUT: before
// STDOUT-NEXT: in-task
// STDOUT-NEXT: final
// STDOUT-NOT: inner-after
// STDOUT-NOT: outer-after
// STDOUT-NOT: caller-after
// STDOUT-NOT: late-fork
// STDOUT-NOT: join-after
// IEEE 1800-2017 20.2, Table 20-1: the default verbosity of 1 prints the
// simulation time and the location of the call.
// STDERR-NOT: {{.}}
