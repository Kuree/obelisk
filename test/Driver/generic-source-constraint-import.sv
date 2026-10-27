// RUN: talos --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: talos --std=1800-2023 -emit-talos %s -o /dev/null

// Import acceptance must not bind bodies against an invalid specialization.
// The matching external definition is observed at its prototype position.
virtual class unused_constraints #(type T);
  rand T value;
  constraint dependent { value.payload > 0; }
  extern constraint external;
  pure constraint required;
  constraint implicit_empty;
  class nested;
    rand T inner;
    constraint own { inner.payload > 0; }
  endclass
endclass
constraint unused_constraints::external { value.payload < 8; }

module generic_source_constraint_import;
endmodule
