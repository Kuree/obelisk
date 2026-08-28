// XFAIL: *
// Slang v11 requires $dumpfile's argument to be a string even though the LRM
// permits an integral expression. Keep this frontend-only reproducer so an
// upstream fix turns the XPASS into a visible signal.
// RUN: %obelisk -emit-obelisk %s -o /dev/null

module dump_vcd_integral_name_xfail;
  logic [8*12-1:0] name = "integral.vcd";
  initial $dumpfile(name);
endmodule
