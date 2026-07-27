// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_aggregate_member_default;
  parameter bit [3:0] DEFAULT_LO = 4'h5;
  typedef struct {
    bit [3:0] lo = DEFAULT_LO;
    bit [3:0] hi;
  } record_t;
  record_t value;

  task automatic show_local;
    record_t local_value;
    local_value.hi = 4'hb;
    $display("local=%h:%h", local_value.hi, local_value.lo);
  endtask

  initial begin
    value.hi = 4'ha;
    $display("value=%h:%h", value.hi, value.lo);
    show_local();
  end
endmodule

// CHECK: value=a:5
// CHECK: local=b:5
