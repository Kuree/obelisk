// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: z3

// IEEE 1800-2023 18.4: packed untagged aggregates are integral domains.
// Nested enum members may take unnamed encodings, including in randc and
// wide packed structs; standalone enum variables retain exact enumerators.
// CHECK: encodings 0 0 0 0 0 0
// CHECK-NEXT: encodings 1 1 1 1 1 1
// CHECK-NEXT: encodings 2 2 2 2 2 2
// CHECK-NEXT: encodings 3 3 3 3 3 3
// CHECK-NEXT: packed enum domains PASS

typedef enum bit [1:0] {A = 0, B = 3} e_t;
typedef struct packed { e_t e; } s_t;
typedef union packed { e_t e; bit [1:0] bits; } u_t;
typedef struct packed { s_t s; e_t e; } nested_t;
typedef struct packed { bit [64:0] pad; e_t e; } wide_t;

class Standalone;
  rand e_t e;
endclass

class Values;
  rand s_t s;
  rand u_t u;
  rand nested_t nested;
  rand wide_t wide;
endclass

class Cycle;
  randc s_t s;
endclass

module random_packed_enum_domains;
  initial begin
    Standalone standalone = new;
    Values v = new;
    Cycle c = new;
    s_t local_struct;
    bit [3:0] seen = 0;
    for (int i = 0; i < 4; i++) begin
      if (!v.randomize() with {
            s == i;
            u == i;
            nested == i * 4'd5;
            wide == (i | 67'd73786976294838206464);
          })
        $fatal(1, "packed aggregate lost encoding %0d", i);
      $display("encodings %0d %0d %0d %0d %0d %0d", i, v.s.e, v.u.e,
               v.nested.s.e, v.nested.e, v.wide.e);
      if (v.s.e != i || v.u.e != i || v.nested.s.e != i || v.nested.e != i ||
          v.wide.e != i)
        $fatal(1, "packed enum member was restricted");
      if (!randomize(local_struct) with { local_struct.e == i; })
        $fatal(1, "scope packed aggregate lost encoding");
    end
    repeat (4) begin
      if (!c.randomize()) $fatal(1, "packed randc failed");
      if (seen[c.s.e]) $fatal(1, "packed randc repeated an encoding");
      seen[c.s.e] = 1;
    end
    if (seen != 4'b1111) $fatal(1, "packed randc missed unnamed encodings");
    if (standalone.randomize() with { e == 1; })
      $fatal(1, "standalone enum admitted an unnamed encoding");
    $display("packed enum domains PASS");
  end
endmodule
