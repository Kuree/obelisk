// RUN: talos -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

module native_dynamic_array_events;
  event first, second;
  event events[];
  event selected[$];

  initial begin
    events = new[3];
    events[0] = first;
    events[1] = second;
    events[2] = first;
    selected = events.unique();
    $display("sizes=%0d/%0d same=%0d/%0d default=%0d", events.size(),
             selected.size(), selected[0] == first, selected[1] == second,
             events[-1] == null);
  end

  // CHECK: sizes=3/2 same=1/1 default=1
endmodule
