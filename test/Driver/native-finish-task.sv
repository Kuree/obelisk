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
// RUN: FileCheck %s --check-prefix=STDERR < %t.o3.native.err

module native_finish_task;
  task automatic inner_finish();
    $display("in-task");
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
// STDERR: $finish: {{.*}}native-finish-task.sv:21: simulation time 0
// STDERR-NOT: {{.}}
