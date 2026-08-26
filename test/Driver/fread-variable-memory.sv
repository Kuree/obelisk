// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native +OUT=%t.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode +OUT=%t.data | FileCheck %s

// IEEE 1800-2017 21.3.4.3: the unpacked-memory form of $fread uses the
// existing extent of a dynamic array or queue. It neither resizes the
// destination nor changes elements outside the selected start/count window.
module fread_variable_memory;
  string path;
  integer descriptor;
  integer count;
  byte dynamic_memory[];
  logic [11:0] queue_memory[$];

  task open_input;
    descriptor = $fopen(path, "r");
  endtask

  initial begin
    if (!$value$plusargs("OUT=%s", path))
      $fatal(0, "missing output path");
    descriptor = $fopen(path, "w");
    $fwrite(descriptor, "ABCDEFGH");
    $fclose(descriptor);

    dynamic_memory = new[4];
    dynamic_memory[0] = "x";
    dynamic_memory[3] = "z";
    open_input;
    count = $fread(dynamic_memory, descriptor, 1, 2);
    $fclose(descriptor);
    $display("dynamic=%0d:%0d:%c%c%c%c", count, dynamic_memory.size(),
             dynamic_memory[0], dynamic_memory[1], dynamic_memory[2],
             dynamic_memory[3]);

    queue_memory = '{12'hfff, 12'hfff, 12'hfff};
    open_input;
    count = $fread(queue_memory, descriptor);
    $fclose(descriptor);
    $display("queue=%0d:%0d:%h,%h,%h", count, queue_memory.size(),
             queue_memory[0], queue_memory[1], queue_memory[2]);

    dynamic_memory = new[0];
    open_input;
    count = $fread(dynamic_memory, descriptor);
    $fclose(descriptor);
    $display("empty=%0d:%0d", count, dynamic_memory.size());
  end
endmodule

// CHECK: dynamic=2:4:xABz
// CHECK-NEXT: queue=6:3:142,344,546
// CHECK-NEXT: empty=0:0
