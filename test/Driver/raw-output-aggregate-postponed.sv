// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

module raw_output_aggregate_postponed;
  typedef struct {
    logic [31:0] first;
    logic [31:0] second;
  } pair_t;

  pair_t value;
  pair_t file_value;
  string output_path;
  integer descriptor;
  integer status;
  integer trailing;

  initial begin
    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "wb");

    value.first = 32'h44434241;
    value.second = 32'h48474645;
    $monitor("monitor=%u", value);
    $strobe("strobe=%u", value);
    $fstrobe(descriptor, "%u", value);

    // All three postponed/persistent calls must reevaluate the aggregate
    // after the active-region changes rather than retaining the first value.
    value.first = 32'h4c4b4a49;
    value.second = 32'h504f4e4d;
    #1;

    $fclose(descriptor);
    descriptor = $fopen(output_path, "rb");
    status = $fscanf(descriptor, "%u", file_value);
    trailing = $fgetc(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%0d:%0d", status, file_value === value,
             trailing == "\n");

    // A later change proves that the persistent monitor reruns its outlined
    // raw aggregate formatter rather than printing a time-zero snapshot.
    value.first = 32'h54535251;
    value.second = 32'h58575655;
    #1 $finish;
  end
endmodule

// CHECK-DAG: monitor=IJKLMNOP
// CHECK-DAG: strobe=IJKLMNOP
// CHECK: file=1:1:1
// CHECK-NEXT: monitor=QRSTUVWX
