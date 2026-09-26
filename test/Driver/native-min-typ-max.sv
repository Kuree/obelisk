// RUN: obelisk -O0 --timing=typ %s -o %t.typ.o0.native
// RUN: %t.typ.o0.native | FileCheck %s --check-prefix=TYP
// RUN: obelisk -O0 --execution-tier=bytecode --timing=typ %s -o %t.typ.o0.bytecode
// RUN: %t.typ.o0.bytecode | FileCheck %s --check-prefix=TYP
// RUN: obelisk -O3 --timing=typ %s -o %t.typ.o3.native
// RUN: %t.typ.o3.native | FileCheck %s --check-prefix=TYP
// RUN: obelisk -O3 --execution-tier=bytecode --timing=typ %s -o %t.typ.o3.bytecode
// RUN: %t.typ.o3.bytecode | FileCheck %s --check-prefix=TYP
// RUN: obelisk -O3 --timing=min %s -o %t.min.native
// RUN: %t.min.native | FileCheck %s --check-prefix=MIN
// RUN: obelisk -O3 --execution-tier=bytecode --timing=min %s -o %t.min.bytecode
// RUN: %t.min.bytecode | FileCheck %s --check-prefix=MIN
// RUN: obelisk -O3 --timing=max %s -o %t.max.native
// RUN: %t.max.native | FileCheck %s --check-prefix=MAX
// RUN: obelisk -O3 --execution-tier=bytecode --timing=max %s -o %t.max.bytecode
// RUN: %t.max.bytecode | FileCheck %s --check-prefix=MAX
// RUN: not obelisk --timing=middle %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID

// IEEE 1800-2017 11.11 defines min:typ:max expressions. Selection applies
// consistently to constants, procedural delays, and continuous-assign delays.

`timescale 1ns/1ns
module native_min_typ_max;
  localparam int selected = 11:22:33;
  int min_delay = 4;
  int typ_delay = 5;
  int max_delay = 6;
  int scope_value;
  int scope_min = 41;
  int scope_typ = 42;
  int scope_max = 43;
  int scope_status;
  wire delayed;
  assign #(1:2:3) delayed = 1'b1;

  initial begin
    scope_status = std::randomize(scope_value) with {
      scope_value == (scope_min:scope_typ:scope_max);
    };
    $display("constraint status=%0d scope=%0d", scope_status, scope_value);
    $display("snapshot time=%0t delayed=%b", $time, delayed);
    #1 $display("snapshot time=%0t delayed=%b", $time, delayed);
    #1 $display("snapshot time=%0t delayed=%b", $time, delayed);
    #1 $display("snapshot time=%0t delayed=%b", $time, delayed);
  end

  initial begin
    #(min_delay:typ_delay:max_delay);
    $display("selected=%0d time=%0t delayed=%b", selected, $time, delayed);
  end
endmodule

// TYP: constraint status=1 scope=42
// TYP-NEXT: snapshot time=0 delayed=z
// TYP-NEXT: snapshot time=1 delayed=z
// TYP-NEXT: snapshot time=2 delayed=1
// TYP-NEXT: snapshot time=3 delayed=1
// TYP-NEXT: selected=22 time=5 delayed=1

// MIN: constraint status=1 scope=41
// MIN-NEXT: snapshot time=0 delayed=z
// MIN-NEXT: snapshot time=1 delayed=1
// MIN-NEXT: snapshot time=2 delayed=1
// MIN-NEXT: snapshot time=3 delayed=1
// MIN-NEXT: selected=11 time=4 delayed=1

// MAX: constraint status=1 scope=43
// MAX-NEXT: snapshot time=0 delayed=z
// MAX-NEXT: snapshot time=1 delayed=z
// MAX-NEXT: snapshot time=2 delayed=z
// MAX-NEXT: snapshot time=3 delayed=1
// MAX-NEXT: selected=33 time=6 delayed=1

// INVALID: unsupported min:typ:max selection 'middle'; expected min, typ, or max
