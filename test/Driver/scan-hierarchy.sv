// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

// IEEE 1800-2017 21.3.4.3: %m assigns the current hierarchical path without
// consuming an input field. It still matches preceding format text and counts
// as one assignment unless suppressed.
module scan_hierarchy;
  string output_path;
  string hierarchy;
  string second_hierarchy;
  int descriptor;
  int iteration;
  int position;
  int status;
  byte character;

  initial begin : named
    hierarchy = "preserved";
    character = "?";
    status = $sscanf("tag=Q", "tag=%M%*m%c", hierarchy, character);
    $display("string=%0d:%s:%c", status, hierarchy, character);

    hierarchy = "preserved";
    character = "?";
    status = $sscanf("badQ", "tag=%m%c", hierarchy, character);
    $display("mismatch=%0d:%s:%c", status, hierarchy, character);

    hierarchy = "";
    second_hierarchy = "";
    character = 0;
    status = $sscanf("Z", "%m%m%c", hierarchy, second_hierarchy, character);
    $display("consecutive=%0d:%s:%s:%c", status, hierarchy,
             second_hierarchy, character);

    hierarchy = "";
    status = $sscanf("", "%m", hierarchy);
    $display("empty-string=%0d:%s", status, hierarchy);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "tag=R");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    for (iteration = 0; iteration < 256; ++iteration) begin
      status = $fscanf(descriptor, "%m", hierarchy);
      if (status != 1 || hierarchy != "scan_hierarchy.named")
        $fatal(0, "repeated %%m failed at %0d", iteration);
    end
    position = $ftell(descriptor);
    if (position != 0)
      $fatal(0, "repeated %%m moved the file position to %0d", position);
    hierarchy = "";
    character = 0;
    status = $fscanf(descriptor, "tag=%m%c", hierarchy, character);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%s:%c:%0d", status, hierarchy, character, position);

    descriptor = $fopen(output_path, "w");
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    hierarchy = "";
    status = $fscanf(descriptor, "%m", hierarchy);
    position = $ftell(descriptor);
    character = $fgetc(descriptor);
    $fclose(descriptor);
    $display("empty-file=%0d:%s:%0d:%0d", status, hierarchy, position,
             character);
  end
endmodule

// CHECK: string=2:scan_hierarchy.named:Q
// CHECK-NEXT: mismatch=0:preserved:?
// CHECK-NEXT: consecutive=3:scan_hierarchy.named:scan_hierarchy.named:Z
// CHECK-NEXT: empty-string=1:scan_hierarchy.named
// CHECK-NEXT: file=2:scan_hierarchy.named:R:5
// CHECK-NEXT: empty-file=1:scan_hierarchy.named:0:-1
