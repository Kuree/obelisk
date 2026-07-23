// RUN: not obelisk -DMIXED_UWIRE -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=MIXED
// RUN: not obelisk -DSTRENGTH -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=STRENGTH
// RUN: not obelisk -DDELAY -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=DELAY
// RUN: not obelisk -DASSIGN_STRENGTH -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=ASSIGN-STRENGTH
// RUN: not obelisk -DASSIGN_DELAY -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=ASSIGN-DELAY
// RUN: not obelisk -DWIRED -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=WIRED
// RUN: not obelisk -DDYNAMIC_REF -O0 -emit-sim %s 2>&1 | FileCheck %s --check-prefix=REF
// RUN: not obelisk -DOVERLAPPING_UWIRE -O0 %s -o %t.uwire 2>&1 | FileCheck %s --check-prefix=UWIRE-DRIVERS

`ifdef MIXED_UWIRE
module unsupported_uwire_child(output uwire value);
  assign value = 1'b1;
endmodule
module unsupported_mixed_uwire;
  wire value;
  unsupported_uwire_child child(value);
endmodule
`endif

`ifdef ASSIGN_STRENGTH
module unsupported_assign_strength;
  wire value;
  assign (strong1, pull0) value = 1'b1;
endmodule
`endif

`ifdef ASSIGN_DELAY
module unsupported_assign_delay;
  wire value;
  assign #1 value = 1'b1;
endmodule
`endif

`ifdef OVERLAPPING_UWIRE
module unsupported_overlapping_uwire;
  uwire value;
  assign value = 1'b0;
  assign value = 1'b1;
endmodule
`endif

`ifdef STRENGTH
module unsupported_strength;
  wire (strong1, pull0) value = 1'b1;
endmodule
`endif

`ifdef DELAY
module unsupported_delay;
  wire #1 value = 1'b1;
endmodule
`endif

`ifdef WIRED
module unsupported_wired_resolution;
  wand value;
endmodule
`endif

`ifdef DYNAMIC_REF
module unsupported_ref_child(ref logic value);
endmodule
module unsupported_dynamic_ref;
  logic [1:0] values;
  int index;
  unsupported_ref_child child(values[index]);
endmodule
`endif

// MIXED: connected component mixes uwire with resolved wire/tri nets
// STRENGTH: net strengths are not supported: Pull,Strong
// DELAY: net delays are not supported: #1
// ASSIGN-STRENGTH: continuous-assignment strengths are not supported: Pull,Strong
// ASSIGN-DELAY: continuous-assignment delays are not supported: #1
// WIRED: unsupported net resolution kind wand
// REF: invalid expression for pass by reference
// UWIRE-DRIVERS: uwire connectivity component 0[0] has more than one driver
