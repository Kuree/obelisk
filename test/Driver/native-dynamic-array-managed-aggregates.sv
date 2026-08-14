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

class dynamic_array_managed_item;
  int value;
  function new(int value);
    this.value = value;
  endfunction
endclass

module native_dynamic_array_managed_aggregates;
  typedef struct {
    string text;
    dynamic_array_managed_item item;
    int nested[];
  } record_t;
  record_t value;
  record_t copy;
  record_t records[];
  record_t result[$];

  initial begin
    value.text = "hello";
    value.item = new(9);
    value.nested = '{1, 2};
    copy = value;
    copy.nested[1] = 8;
    $display("direct original=%0d copy=%0d", value.nested[1],
             copy.nested[1]);
    records = '{value, value};
    value.nested[0] = 7;
    result = records.unique();
    $display("result=%0d text=%s item=%0d nested=%0d/%0d",
             result.size(), result[0].text, result[0].item.value,
             result[0].nested[0], result[0].nested[1]);
  end

  // CHECK: direct original=2 copy=8
  // CHECK-NEXT: result=1 text=hello item=9 nested=1/2
endmodule
