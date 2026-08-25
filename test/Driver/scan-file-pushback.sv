// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

module scan_file_pushback;
  string output_path;
  string text;
  string hierarchy;
  logic [31:0] raw_value;
  integer descriptor;
  integer status;
  integer character;
  integer number;

  initial begin
    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");

    status = $ungetc("Q", descriptor);
    status = $fscanf(descriptor, "%c", character);
    $display("char=%0d:%c:%0d:%0d", status, character, $feof(descriptor),
             $ftell(descriptor));

    status = $ungetc("S", descriptor);
    status = $fscanf(descriptor, "%s", text);
    $display("string=%0d:%s:%0d:%0d", status, text, $feof(descriptor),
             $ftell(descriptor));

    number = 77;
    status = $ungetc("Q", descriptor);
    status = $fscanf(descriptor, "%d", number);
    $display("mismatch=%0d:%0d:%0d:%0d", status, number,
             $feof(descriptor), $ftell(descriptor));
    character = $fgetc(descriptor);
    $display("restored=%c:%0d:%0d", character, $feof(descriptor),
             $ftell(descriptor));

    status = $ungetc("Q", descriptor);
    status = $fscanf(descriptor, "Q%m", hierarchy);
    $display("prefix=%0d:%0d:%0d", status, $feof(descriptor),
             $ftell(descriptor));

    status = $ungetc("Q", descriptor);
    status = $fscanf(descriptor, "%*1u%m", hierarchy);
    $display("raw=%0d:%0d:%0d", status, $feof(descriptor),
             $ftell(descriptor));

    raw_value = 32'hdeadbeef;
    status = $ungetc("Q", descriptor);
    status = $fscanf(descriptor, "%u", raw_value);
    $display("raw-partial=%0d:%0h:%0d:%0d", status, raw_value,
             $feof(descriptor), $ftell(descriptor));

    $fclose(descriptor);
  end
endmodule

// CHECK: char=1:Q:1:0
// CHECK-NEXT: string=1:S:1:0
// CHECK-NEXT: mismatch=0:77:0:0
// CHECK-NEXT: restored=Q:1:0
// CHECK-NEXT: prefix=1:1:0
// CHECK-NEXT: raw=1:1:0
// CHECK-NEXT: raw-partial=-1:deadbeef:1:0
