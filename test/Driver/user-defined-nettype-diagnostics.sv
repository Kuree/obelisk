// RUN: not obelisk -fno-lto -O0 -DIMPURE %s -o %t.impure 2>&1 \
// RUN:   | FileCheck %s --check-prefix=IMPURE
// RUN: not obelisk -fno-lto -O0 -DMUTATE %s -o %t.mutate 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MUTATE
// RUN: not obelisk -fno-lto -O0 -DRANDOM %s -o %t.random 2>&1 \
// RUN:   | FileCheck %s --check-prefix=RANDOM
// RUN: not obelisk -fno-lto -O0 -DUNRESOLVED %s -o %t.unresolved 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNRESOLVED
// RUN: not obelisk -fno-lto -O0 -DSTATIC %s -o %t.static 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STATIC

// IEEE 1800-2017 6.6.7: a resolution function is automatic and may neither
// mutate its driver array nor preserve state or produce side effects.

package diagnostic_nettypes;
  int calls;
  function automatic real impure_resolver(input real drivers[]);
    calls++;
    impure_resolver = drivers.size() ? drivers[0] : 0.0;
  endfunction
  nettype real impure_real with impure_resolver;

  function automatic int mutating_resolver(input int drivers[]);
    if (drivers.size())
      drivers[0] = 42;
    drivers = new[2](drivers);
    return 0;
  endfunction
  nettype int mutating_int with mutating_resolver;

  function automatic int random_helper();
    return $urandom;
  endfunction
  function automatic int random_resolver(input int drivers[]);
    return drivers.size() ? drivers[0] : random_helper();
  endfunction
  nettype int random_int with random_resolver;
  nettype int unresolved_int;

  function int static_resolver(input int drivers[]);
    return drivers.size() ? drivers[0] : 0;
  endfunction
  nettype int static_int with static_resolver;
endpackage

module user_defined_nettype_diagnostics;
`ifdef IMPURE
  import diagnostic_nettypes::*;
  impure_real value;
  assign value = 1.0;
`endif
`ifdef MUTATE
  import diagnostic_nettypes::*;
  mutating_int mutating_value;
  assign mutating_value = 1;
`endif
`ifdef RANDOM
  import diagnostic_nettypes::*;
  random_int random_value;
  assign random_value = 1;
`endif
`ifdef UNRESOLVED
  import diagnostic_nettypes::*;
  unresolved_int unresolved_value;
  assign unresolved_value = 1;
  assign unresolved_value = 2;
`endif
`ifdef STATIC
  import diagnostic_nettypes::*;
  static_int static_value;
  assign static_value = 1;
`endif
endmodule

// IMPURE: error: user-defined net resolution function
// IMPURE-SAME: preserves state or has side effects through design storage
// MUTATE: error: user-defined net resolution function
// MUTATE-SAME: writes or resizes its driver-value input array
// RANDOM: error: user-defined net resolution function
// RANDOM-SAME: has a stateful or externally visible side effect
// UNRESOLVED: error: unresolved user-defined net has multiple drivers
// STATIC: error: user-defined net resolution function
// STATIC-SAME: must be automatic
