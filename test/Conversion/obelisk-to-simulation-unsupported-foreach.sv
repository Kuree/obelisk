// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module unsupported_foreach;
  int values[];

  initial
    foreach (values[index])
      values[index] = index;
endmodule

// CHECK: foreach over runtime-sized or associative collections is not supported
