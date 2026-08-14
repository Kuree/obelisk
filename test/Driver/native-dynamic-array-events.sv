// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

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
