// RUN: talos -emit-talos %s | FileCheck %s

// IEEE 1800-2023 21.2.1.6 prints an enum element of an assignment pattern by
// its name, including enum types without an imported declaration. The frozen
// domain of each enum type therefore carries its enumerator names.
typedef enum {A = 1, B = 4} E;
module top;
  initial $display("%p", E'(4));
endmodule

// CHECK: talos.enum_domains = [{names = ["A", "B"], type = !talos.enum<"E", {{.*}}>, values = ["1", "4"]}]
