// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/query.sv | not obelisk-opt --lower-obelisk-to-sim -o /dev/null 2>&1 | FileCheck %s --check-prefix=QUERY
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/member.sv | not obelisk-opt '--test-obelisk-edit-attributes=op=obelisk.sv.expression.member_access match={member_name = "a"} set={field_ordinal = 1 : i64}' --lower-obelisk-to-sim -o /dev/null 2>&1 | FileCheck %s --check-prefix=MEMBER
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/randc.sv | not obelisk-opt '--test-obelisk-edit-attributes=op=obelisk.sv.symbol.class_property match={name = "h"} set={rand_mode = 2 : i32}' --lower-obelisk-to-sim -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANDC

// IEEE 1800-2023 18.8: rand_mode() returns the mode of one variable, so a
// query on a whole structure, a nested structure or an array of a rand
// structure is rejected instead of being reduced to a single bit.
// QUERY-DAG: query.sv:13:{{[0-9]+}}: error: rand_mode() queries require a singular random member
// QUERY-DAG: query.sv:17:{{[0-9]+}}: error: rand_mode() queries require a singular random member
// QUERY-DAG: query.sv:21:{{[0-9]+}}: error: rand_mode() queries require a singular random member

// IEEE 1800-2023 18.8: only a variable declared rand or randc has a random
// mode, so a selected structure member without that qualifier is rejected
// (the source frontend rejects the ordinary member, so the selection is
// retargeted from member a to the ordinary member crc).
// MEMBER: member.sv:10:{{[0-9]+}}: error: rand_mode selects a structure member that is not declared rand or randc

// IEEE 1800-2023 18.4: an unpacked structure shall not be declared randc
// (the source frontend rejects the declaration, so the rand property is
// retargeted to randc).
// RANDC: randc.sv:6:{{[0-9]+}}: error: unpacked structures shall not be randc

//--- query.sv
typedef struct { rand bit x; } n_t;
typedef struct {
  rand bit [3:0] a;
  bit [3:0] crc;
  rand n_t nested;
  rand bit [3:0] data[1:0];
} h_t;
class C;
  rand h_t h;
endclass
module whole;
  C object = new; int status;
  initial status = object.h.rand_mode();
endmodule
module nest;
  C object = new; int status;
  initial status = object.h.nested.rand_mode();
endmodule
module arr;
  C object = new; int status;
  initial status = object.h.data.rand_mode();
endmodule

//--- member.sv
typedef struct {
  rand bit [3:0] a;
  bit [3:0] crc;
} h_t;
class C;
  rand h_t h;
endclass
module top;
  C object = new;
  initial object.h.a.rand_mode(0);
endmodule

//--- randc.sv
typedef struct {
  rand bit [3:0] a;
  bit [3:0] crc;
} h_t;
class C;
  rand h_t h;
endclass
module top;
  C object = new;
endmodule
