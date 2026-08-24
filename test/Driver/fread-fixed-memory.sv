// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 21.3.4.3: $fread accepts an integral variable or an unpacked
// memory, with optional start and count arguments for the memory form.
module fread_fixed_memory;
  integer descriptor;
  integer count;
  byte descending[3:0];
  byte ascending[0:3];
  logic [11:0] words[0:2];

  task open_input;
    descriptor = $fopen("obelisk-fread-fixed-memory.tmp", "r");
  endtask

  initial begin
    descriptor = $fopen("obelisk-fread-fixed-memory.tmp", "w");
    $fwrite(descriptor, "ABCDEFGH");
    $fclose(descriptor);

    open_input;
    count = $fread(descending, descriptor);
    $display("descending=%0d:%c%c%c%c", count, descending[3], descending[2],
             descending[1], descending[0]);
    $fclose(descriptor);

    open_input;
    count = $fread(ascending, descriptor, 1, 2);
    $display("slice=%0d:%c%c", count, ascending[1], ascending[2]);
    $fclose(descriptor);

    open_input;
    count = $fread(ascending, descriptor, , 1);
    $display("default=%0d:%c", count, ascending[0]);
    $fclose(descriptor);

    open_input;
    count = $fread(words, descriptor);
    $display("words=%0d:%h,%h,%h", count, words[0], words[1], words[2]);
    $fclose(descriptor);

    ascending[3] = "Z";
    open_input;
    count = $fread(ascending, descriptor, 9, 1);
    $display("outside=%0d:%c", count, ascending[3]);
    $fclose(descriptor);
  end
endmodule

// CHECK: descending=4:DCBA
// CHECK-NEXT: slice=2:AB
// CHECK-NEXT: default=1:A
// CHECK-NEXT: words=6:142,344,546
// CHECK-NEXT: outside=0:Z
