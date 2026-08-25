// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

// IEEE 1800-2017 Table 21-8: %t matches a floating-point field, rounds it at
// the current $timeformat precision, and scales it into the caller's timeunit.
module scan_time;
  timeunit 1ns;
  timeprecision 100ps;

  string output_path;
  real scanned;
  real negative;
  real overflow;
  time ticks;
  time overflow_ticks;
  integer signed_value;
  integer descriptor;
  integer position;
  integer status;
  byte character;

  initial begin
    // With no executed $timeformat, the input unit is the 100 ps design
    // precision and the field is rounded to zero decimal places.
    status = $sscanf("10.5", "%t", scanned);
    $display("default=%0d:%.2f", status, scanned);

    $timeformat(-9, 1, "", 0);
    status = $sscanf("1.25 -1.25 2.5e1 -1.6", "%t %T %t %t", scanned,
                     negative, ticks, signed_value);
    $display("nanoseconds=%0d:%.2f:%.2f:%0d:%0d", status, scanned,
             negative, ticks, signed_value);

    // Exact IEEE 1800-2017 Table 21-8 example: 10.345 ms rounds to 10.35 ms
    // and a 1 ns caller therefore receives 10,350,000.0.
    $timeformat(-3, 2, " ms", 10);
    status = $sscanf("10.345", "%t", scanned);
    $display("table-21-8=%0d:%.1f", status, scanned);

    scanned = 99.0;
    character = "?";
    status = $sscanf("bad=1.25Q", "tag=%t%c", scanned, character);
    $display("mismatch=%0d:%.2f:%c", status, scanned, character);

    character = 0;
    status = $sscanf("tag=1.25Q", "tag=%*T%c", character);
    $display("suppressed=%0d:%c", status, character);

    // A runtime state change is observed by the next scan. Picoseconds are
    // finer and microseconds are coarser than this scope's nanosecond unit.
    $timeformat(-12, 0, "", 0);
    status = $sscanf("1250", "%t", scanned);
    $display("picoseconds=%0d:%.2f", status, scanned);
    $timeformat(-6, 1, "", 0);
    status = $sscanf("1.25", "%t", scanned);
    $display("microseconds=%0d:%.2f", status, scanned);

    // Numeric overflow follows the existing real parser and destination
    // conversion behavior: an out-of-range field produces numeric zero for
    // both real and integral/time destinations. The runtime unit test also
    // directly guards non-finite passthrough through the scaling helper.
    status = $sscanf("1e400", "%t", overflow);
    $display("real-overflow=%0d:%0d", status, overflow == 0.0);
    status = $sscanf("1e400", "%t", overflow_ticks);
    $display("time-overflow=%0d:%0d", status, overflow_ticks == 0);

    $timeformat(-9, 1, "", 0);
    status = $sscanf("12.34Q", "%4t%c", scanned, character);
    $display("width=%0d:%.2f:%c", status, scanned, character);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "tag=1.25e0X");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    $timeformat(-9, 1, "", 0);
    status = $fscanf(descriptor, "tag=%t%c", scanned, character);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%.2f:%c:%0d", status, scanned, character, position);
  end
endmodule

// CHECK: default=1:1.10
// CHECK-NEXT: nanoseconds=4:1.30:-1.30:25:-2
// CHECK-NEXT: table-21-8=1:10350000.0
// CHECK-NEXT: mismatch=0:99.00:?
// CHECK-NEXT: suppressed=1:Q
// CHECK-NEXT: picoseconds=1:1.25
// CHECK-NEXT: microseconds=1:1300.00
// CHECK-NEXT: real-overflow=1:1
// CHECK-NEXT: time-overflow=1:1
// CHECK-NEXT: width=2:12.30:4
// CHECK-NEXT: file=2:1.30:X:11
