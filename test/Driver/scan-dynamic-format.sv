// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=native \
// RUN:   --native-scheduler=auto %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=native \
// RUN:   --native-scheduler=auto -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=AUTO-LLVM
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

// Managed string state excludes this actor from the static AOT scheduler, but
// auto still compiles the actor as native LLVM and calls the dynamic scan
// services directly instead of silently falling back to design bytecode.
// AUTO-LLVM: call i32 @obelisk_rt_v1_scan_dynamic_validate
// AUTO-LLVM: call i32 @obelisk_rt_v1_string_scan_dynamic
// AUTO-LLVM: call i32 @obelisk_rt_v1_file_scan_dynamic

module scan_dynamic_format;
  timeunit 1ns;
  timeprecision 100ps;

  string format;
  string text;
  string word;
  string hierarchy;
  string output_path;
  logic [15:0] value;
  integer bin_value;
  integer oct_value;
  integer dec_value;
  integer hex_value;
  logic strength;
  real real_value;
  real exponent_value;
  real general_value;
  time time_value;
  byte character;
  integer descriptor;
  integer position;
  integer status;

  initial begin : named
    format = "tag=%4H %*4s %s%%";
    status = $sscanf("tag=1x?z SKIP word%", format, value, word);
    $display("mixed=%0d:%b:%s", status, value, word);

    // The same call site observes a different plan after the runtime format
    // variable changes; repeated uses of either value hit the bounded cache.
    format = "%m%c";
    status = $sscanf("Q", format, hierarchy, character);
    $display("hierarchy=%0d:%s:%c", status, hierarchy, character);

    format = "%3v %f %t";
    $timeformat(-9, 1, "", 0);
    status = $sscanf("PuH 1.25 2.5", format, strength, real_value,
                     time_value);
    $display("numeric=%0d:%b:%.2f:%0d", status, strength, real_value,
             time_value);

    format = "%*1u%c";
    status = $sscanf("AQ", format, character);
    $display("raw-suppression=%0d:%c", status, character);

    format = "%B %O %D %X %E %G";
    status = $sscanf("10 17 -12 A5 1e2 2.5", format, bin_value,
                     oct_value, dec_value, hex_value, exponent_value,
                     general_value);
    $display("families=%0d:%0d:%0d:%0d:%0h:%.1f:%.1f", status,
             bin_value, oct_value, dec_value, hex_value, exponent_value,
             general_value);

    format = "";
    status = $sscanf("", format);
    $display("empty=%0d", status);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "A=7f SKIP tail!");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    format = "A=%2h %*4s %s!";
    status = $fscanf(descriptor, format, value, word);
    position = $ftell(descriptor);
    $display("file=%0d:%h:%s:%0d", status, value, word, position);
    format = "%d";
    status = $fscanf(descriptor, format, value);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("eof=%0d:%0d", status, position);

    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "A=q");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    format = "A=%D";
    status = $fscanf(descriptor, format, value);
    position = $ftell(descriptor);
    $display("mismatch=%0d:%0d", status, position);
    format = "%c";
    status = $fscanf(descriptor, format, character);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("after-mismatch=%0d:%c:%0d", status, character, position);
  end
endmodule

// CHECK: mixed=2:0001xxxxzzzzzzzz:word
// CHECK-NEXT: hierarchy=2:scan_dynamic_format.named:Q
// CHECK-NEXT: numeric=3:1:1.25:3
// CHECK-NEXT: raw-suppression=1:Q
// CHECK-NEXT: families=6:2:15:-12:a5:100.0:2.5
// CHECK-NEXT: empty=0
// CHECK-NEXT: file=2:007f:tail!:15
// CHECK-NEXT: eof=-1:15
// CHECK-NEXT: mismatch=0:2
// CHECK-NEXT: after-mismatch=1:q:3
