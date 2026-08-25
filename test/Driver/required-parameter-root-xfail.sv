// XFAIL: *
// RUN: obelisk --top=required_parameter_root_xfail -emit-obelisk %s -o /dev/null

// A module whose parameter has no default is legal to declare and can only be
// instantiated with an override. Even with an explicit independent --top,
// Slang elaborates this declaration as another root, gives the unset parameter
// ErrorType, and prevents the selected root from compiling. Keep this single
// frontend-owned failure visible without masking a purported passing RUN or
// patching Slang.
module requires_override #(parameter VALUE);
  initial $fatal(1, "uninstantiable module was selected as a root");
endmodule

module required_parameter_root_xfail;
  initial $finish;
endmodule
