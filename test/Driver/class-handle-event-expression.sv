// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 9.4.2: a class-handle event expression changes when the
// handle's object identity changes. Mutating a property of that object does
// not change the handle value.
module class_handle_event_expression;
  class payload;
    int value;
  endclass

  class monitor;
    payload observed;
    int changes;

    task run();
      forever begin
        @observed;
        ++changes;
      end
    endtask
  endclass

  monitor mon = new;
  payload first = new;
  payload second = new;

  initial begin
    fork
      mon.run();
    join_none
    #1 mon.observed = first;
    #1 first.value = 1;
    #1 mon.observed = first;
    #1 mon.observed = second;
    #1 mon.observed = null;
    #1;
    $display("changes=%0d", mon.changes);
  end
endmodule

// CHECK: changes=3
