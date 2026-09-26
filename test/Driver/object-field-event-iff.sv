// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 9.4.2.3: an iff expression is tested only when the event
// expression changes as requested. Both expressions may select object fields.
module object_field_event_iff;
  class monitor;
    bit clock;
    bit enabled;
    int fires;

    task run();
      forever @(posedge clock iff enabled) ++fires;
    endtask
  endclass

  monitor mon = new;

  initial begin
    fork
      mon.run();
    join_none

    mon.enabled = 1;
    #1 mon.enabled = 0;
    #1 mon.clock = 1;
    #1 mon.clock = 0;
    #1 mon.enabled = 1;
    #1 mon.clock = 1;
    #1 mon.enabled = 0;
    #1 mon.enabled = 1;
    #1 mon.clock = 0;
    #1 mon.clock = 1;
    #1;
    $display("fires=%0d", mon.fires);
  end
endmodule

// CHECK: fires=2
