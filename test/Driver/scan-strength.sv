// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

// IEEE 1800-2017 Tables 21-4 through 21-6 and Table 21-8: %v matches one
// canonical three-character strength field and assigns its four-state logic
// component to an integral destination.
module scan_strength;
  string output_path;
  logic zero;
  logic one;
  logic unknown;
  logic impedance;
  logic low;
  logic high;
  logic [7:0] all_mnemonics;
  logic [3:0] wide;
  integer signed_value;
  integer descriptor;
  integer invalid_count;
  integer next_character;
  integer position;
  integer status;
  byte character;

  initial begin
    status = $sscanf("St0 St1 StX HiZ PuL PuH", "%v %V %v %v %v %v",
                     zero, one, unknown, impedance, low, high);
    $display("canonical=%0d:%b:%b:%b:%b:%b:%b", status, zero, one,
             unknown, impedance, low, high);

    // These are the range and unequal-strength examples from Table 21-6.
    status = $sscanf("520 65X", "%v %v", zero, unknown);
    $display("digit-pairs=%0d:%b:%b", status, zero, unknown);

    status = $sscanf("Su1 St1 Pu1 La1 We1 Me1 Sm1 HiZ",
                     "%v %v %v %v %v %v %v %v", all_mnemonics[7],
                     all_mnemonics[6], all_mnemonics[5], all_mnemonics[4],
                     all_mnemonics[3], all_mnemonics[2], all_mnemonics[1],
                     all_mnemonics[0]);
    $display("mnemonics=%0d:%b", status, all_mnemonics);

    // Parsed strength components use the ordinary integral destination
    // conversion path after the scanner has discarded the strength itself.
    status = $sscanf("PuH 520", "%v %v", wide, signed_value);
    $display("destinations=%0d:%b:%0d", status, wide, signed_value);

    character = 0;
    status = $sscanf("StXQ", "%*3V%c", character);
    $display("suppressed-width=%0d:%c", status, character);
    zero = 1;
    status = $sscanf("St0", "%2v", zero);
    $display("short-width=%0d:%b", status, zero);

    zero = 1;
    character = "?";
    status = $sscanf("bad=St0Q", "tag=%v%c", zero, character);
    $display("prefix-mismatch=%0d:%b:%c", status, zero, character);

    // Input field spelling is exact. Equal or reversed known-value ranges,
    // equal X components, digit-pair L/H/Z, and impossible Hi/non-Hi Z forms
    // are not canonical strength formats.
    invalid_count = 0;
    zero = 1;
    invalid_count += $sscanf("st1", "%v", zero);
    invalid_count += $sscanf("ST1", "%v", zero);
    invalid_count += $sscanf("Stx", "%v", zero);
    invalid_count += $sscanf("Hi0", "%v", zero);
    invalid_count += $sscanf("StZ", "%v", zero);
    invalid_count += $sscanf("550", "%v", zero);
    invalid_count += $sscanf("250", "%v", zero);
    invalid_count += $sscanf("500", "%v", zero);
    invalid_count += $sscanf("50X", "%v", zero);
    invalid_count += $sscanf("05X", "%v", zero);
    invalid_count += $sscanf("66X", "%v", zero);
    invalid_count += $sscanf("52L", "%v", zero);
    $display("invalid=%0d:%b", invalid_count, zero);

    zero = 1;
    status = $sscanf("", "%v", zero);
    $display("empty-string=%0d:%b", status, zero);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "tag=65XQ");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    status = $fscanf(descriptor, "tag=%V%c", unknown, character);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%b:%c:%0d", status, unknown, character, position);

    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "tag=st1Q");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    zero = 1;
    status = $fscanf(descriptor, "tag=%v", zero);
    position = $ftell(descriptor);
    next_character = $fgetc(descriptor);
    $fclose(descriptor);
    $display("file-mismatch=%0d:%b:%0d:%c", status, zero, position,
             next_character);

    descriptor = $fopen(output_path, "w");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    status = $fscanf(descriptor, "%v", zero);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("empty-file=%0d:%0d", status, position);
  end
endmodule

// CHECK: canonical=6:0:1:x:z:0:1
// CHECK-NEXT: digit-pairs=2:0:x
// CHECK-NEXT: mnemonics=8:1111111z
// CHECK-NEXT: destinations=2:0001:0
// CHECK-NEXT: suppressed-width=1:Q
// CHECK-NEXT: short-width=0:1
// CHECK-NEXT: prefix-mismatch=0:1:?
// CHECK-NEXT: invalid=0:1
// CHECK-NEXT: empty-string=0:1
// CHECK-NEXT: file=2:x:Q:8
// CHECK-NEXT: file-mismatch=0:1:4:s
// CHECK-NEXT: empty-file=-1:0
