// RUN: obelisk --top=required_parameter_root_xfail -emit-obelisk %s -o /dev/null

// A module whose parameter has no default is legal to declare and can only be
// instantiated with an override (IEEE 1800-2017 6.20.1). With an explicit
// independent --top, its frontend-only semantic-checking instance is not part
// of the selected executable hierarchy and must not block that hierarchy.
module requires_override #(parameter VALUE);
  initial $fatal(1, "uninstantiable module was selected as a root");
endmodule

module required_parameter_root_xfail;
  initial $finish;
endmodule
