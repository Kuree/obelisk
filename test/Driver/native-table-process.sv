// RUN: obelisk -O0 %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O3 --bytecode-scope=all %s -o %t.all
// RUN: %t.all | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O0 %s -emit-llvm -o %t.ll
// RUN: FileCheck %s --check-prefix=IR < %t.ll

// IEEE 1800-2023 9.4.2, 9.6.2: event widths, X edges and descendant disable.
module native_table_process;
  logic [64:0] source = 0;
  bit other = 1;
  bit killed_event;
  int stage;
  int killed_tail;

  initial begin
    @(source);
    stage = 1;
    @(posedge source);
    stage = 2;
    @(source or negedge other);
    stage = 3;
  end

  initial begin : victim
    fork
      begin
        @(killed_event);
        killed_tail = 1;
      end
    join
  end

  initial begin
    #1 source[64] = 1;
    #1;
    if (stage != 1) $fatal(1, "wide change event was missed");
    source[63] = 1;
    disable victim;
    #1;
    if (stage != 1) $fatal(1, "edge observed an upper bit");
    source[0] = 1'bx;
    #1;
    if (stage != 2) $fatal(1, "0-to-X posedge was missed");
    other = 0;
    killed_event = 1;
    #1;
    if (stage != 3 || killed_tail != 0)
      $fatal(1, "event OR or enclosing disable failed");
    $display("table events and disable passed");
    $finish;
  end
endmodule

// CHECK: table events and disable passed
// IR-DAG: define i32 @{{[^. ]+}}.__obelisk_table_body(
// IR-DAG: define i32 @{{[^ ]+}}.fork.{{[^ ]+}}.__obelisk_table_body(
