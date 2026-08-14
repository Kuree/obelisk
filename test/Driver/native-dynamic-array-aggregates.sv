// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out

module native_dynamic_array_aggregates;
  typedef struct {
    int number;
    byte tiny;
  } record_t;
  typedef int pair_t[2];
  record_t records[];
  record_t record_result[$];
  pair_t pairs[];
  pair_t pair_result[$];

  initial begin
    record_t record_value;
    pair_t pair_value;
    record_value.number = 7;
    record_value.tiny = 2;
    records = '{record_value, record_value};
    record_result = records.unique();
    records[0].number = 8;
    records[0].tiny = 3;
    $display("records=%0d result=%0d/%0d write=%0d/%0d",
             record_result.size(), record_result[0].number,
             record_result[0].tiny, records[0].number, records[0].tiny);
    pair_value[0] = 1;
    pair_value[1] = 2;
    pairs = '{pair_value, pair_value};
    pair_result = pairs.unique();
    pairs[0][0] = 4;
    pairs[0][1] = 5;
    $display("pairs=%0d result=%0d/%0d write=%0d/%0d", pair_result.size(),
             pair_result[0][0], pair_result[0][1], pairs[0][0], pairs[0][1]);
  end

  // CHECK: records=1 result=7/2 write=8/3
  // CHECK-NEXT: pairs=1 result=1/2 write=4/5
endmodule
