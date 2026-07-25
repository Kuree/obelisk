// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module invalid_strobe_side_effect;
  logic value;

  function automatic logic mutate();
    value = 1;
    return value;
  endfunction

  initial
    $strobe("value=%0d", mutate());
endmodule

// CHECK: error: 'obelisk_sim.ref.store' op is not permitted in a read-only postponed code unit
