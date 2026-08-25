// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

module scan_numeric_exact;
  logic [62:0] value63;
  logic [63:0] value64;
  logic [64:0] value65;
  logic [127:0] value128;
  logic [4095:0] value4096;
  logic [15:0] mixed;
  logic [8:0] decimal_x;
  logic [8:0] decimal_z;
  logic [7:0] unchanged = 8'ha5;
  string wide_source;
  string output_path;
  integer descriptor;
  integer position;
  integer status;
  byte first_character;
  byte second_character;

  initial begin
    status = $sscanf({"7fffffffffffffff ffffffffffffffff ",
                      "1ffffffffffffffff ffffffffffffffffffffffffffffffff"},
                     "%h %h %h %h", value63, value64, value65, value128);
    $display("boundaries=%0d:%h:%h:%h:%h", status, value63, value64,
             value65, value128);

    status = $sscanf("1x?z x ?", "%h %d %d", mixed, decimal_x, decimal_z);
    $display("four-state=%0d:%b:%b:%b", status, mixed, decimal_x, decimal_z);

    // Power-of-two fields have no optional sign in Table 21-8. A mismatch
    // leaves both the destination and the first input character untouched.
    status = $sscanf("-1", "%h", unchanged);
    $display("unsigned-sign=%0d:%h", status, unchanged);

    status = $sscanf("1g", "%h%c", mixed, first_character);
    $display("partial=%0d:%h:%c", status, mixed, first_character);

    wide_source = {1024{"a"}};
    status = $sscanf(wide_source, "%h", value4096);
    $display("wide=%0d:%h:%h", status, value4096[4095:4080],
             value4096[15:0]);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "1x?zQ ?R -1");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    status = $fscanf(descriptor, "%h%c %d%c", mixed, first_character,
                     decimal_z, second_character);
    position = $ftell(descriptor);
    $display("file=%0d:%b:%c:%b:%c:%0d", status, mixed, first_character,
             decimal_z, second_character, position);
    status = $fscanf(descriptor, " %h", unchanged);
    position = $ftell(descriptor);
    first_character = $fgetc(descriptor);
    $fclose(descriptor);
    $display("file-mismatch=%0d:%h:%0d:%c", status, unchanged, position,
             first_character);
  end
endmodule

// CHECK: boundaries=4:7fffffffffffffff:ffffffffffffffff:1ffffffffffffffff:ffffffffffffffffffffffffffffffff
// CHECK-NEXT: four-state=3:0001xxxxzzzzzzzz:xxxxxxxxx:zzzzzzzzz
// CHECK-NEXT: unsigned-sign=0:a5
// CHECK-NEXT: partial=2:0001:g
// CHECK-NEXT: wide=1:aaaa:aaaa
// CHECK-NEXT: file=4:0001xxxxzzzzzzzz:Q:zzzzzzzzz:R:8
// CHECK-NEXT: file-mismatch=0:a5:9:-
