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
// RUN: FileCheck %s < %t.o0.native.out

class container_holder;
  int values[$];
endclass

module native_class_container_sensitivity;
  container_holder holder;
  int sink;

  always_comb begin
    sink = holder == null ? -2
                          : (holder.values.size() ? holder.values[0] : -1);
    $display("class-wake %0d", sink);
  end

  initial begin
    #1 holder = new;
    holder.values = '{1};
    #1 holder.values[0] = 7;
    #1 holder.values.reverse();
    #1 holder.values.delete();
    #1 holder = new;
    holder.values = '{9};
    #1 $finish;
  end

  // IEEE 1800-2017 9.2.2.2.1 includes expressions reached through class
  // properties in always_comb sensitivity. Both property replacement and an
  // in-place mutation of its current container must wake the process.
  // CHECK: class-wake -2
  // CHECK-NEXT: class-wake 1
  // CHECK-NEXT: class-wake 7
  // CHECK-NEXT: class-wake 7
  // CHECK-NEXT: class-wake -1
  // CHECK-NEXT: class-wake 9
endmodule
