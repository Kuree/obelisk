// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 21.3.4: a decimal field width limits the characters consumed
// by one conversion. Assignment suppression still consumes its field but has
// no destination and does not contribute to the return count.
module scan_field_width;
  int first;
  int second;
  int status;
  int descriptor;
  int next_character;
  logic [31:0] hex_value;
  string word;

  initial begin
    first = 0;
    word = "";
    status = $sscanf("12345 tail", "%3d%*2d %2s", first, word);
    $display("string=%0d:%0d:%s", status, first, word);

    second = 77;
    status = $sscanf("bad 9", "%*d %d", second);
    $display("suppression-failure=%0d:%0d", status, second);

    // Width parsing is linear in the format length and saturates instead of
    // overflowing or expanding into width-proportional IR.
    status = $sscanf("42", "%999999999999999999999999999999999999999d",
                     first);
    $display("saturated=%0d:%0d", status, first);

    // Verilator's regression uses every digit to exercise its own parser.
    status = $sscanf("P20=4cff0000", "P%h=%80123456789h", first, hex_value);
    $display("long=%0d:%0h:%0h", status, first, hex_value);

    descriptor = $fopen("obelisk-scan-field-width.tmp", "w");
    $fwrite(descriptor, "12345 tail");
    $fclose(descriptor);
    descriptor = $fopen("obelisk-scan-field-width.tmp", "r");
    first = 0;
    word = "";
    status = $fscanf(descriptor, "%3d%*2d %2s", first, word);
    next_character = $fgetc(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%0d:%s:%c", status, first, word, next_character);
  end
endmodule

// CHECK: string=2:123:ta
// CHECK-NEXT: suppression-failure=0:77
// CHECK-NEXT: saturated=1:42
// CHECK-NEXT: long=2:20:4cff0000
// CHECK-NEXT: file=2:123:ta:i
