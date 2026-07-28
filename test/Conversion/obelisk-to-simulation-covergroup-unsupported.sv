// RUN: not obelisk -emit-sim -DCTOR %s 2>&1 | FileCheck %s --check-prefix=CTOR
// RUN: not obelisk -emit-sim -DEVENT %s 2>&1 | FileCheck %s --check-prefix=EVENT
// RUN: not obelisk -emit-sim -DFORMAL %s 2>&1 | FileCheck %s --check-prefix=FORMAL
// RUN: not obelisk -emit-sim -DAUTO %s 2>&1 | FileCheck %s --check-prefix=AUTO
// RUN: not obelisk -emit-sim -DBIN_ARRAY %s 2>&1 | FileCheck %s --check-prefix=BIN-ARRAY
// RUN: not obelisk -emit-sim -DWILDCARD %s 2>&1 | FileCheck %s --check-prefix=WILDCARD
// RUN: not obelisk -emit-sim -DTRANSITION %s 2>&1 | FileCheck %s --check-prefix=TRANSITION
// RUN: not obelisk -emit-sim -DIGNORE %s 2>&1 | FileCheck %s --check-prefix=IGNORE
// RUN: not obelisk -emit-sim -DILLEGAL %s 2>&1 | FileCheck %s --check-prefix=ILLEGAL
// RUN: not obelisk -emit-sim -DCROSS %s 2>&1 | FileCheck %s --check-prefix=CROSS
// RUN: not obelisk -emit-sim -DOPTION %s 2>&1 | FileCheck %s --check-prefix=OPTION
// RUN: not obelisk -emit-sim -DNONCONST %s 2>&1 | FileCheck %s --check-prefix=NONCONST
// RUN: not obelisk -emit-sim -DNONCONST_RANGE %s 2>&1 | FileCheck %s --check-prefix=NONCONST-RANGE
// RUN: not obelisk -emit-sim -DWITH_EXPR %s 2>&1 | FileCheck %s --check-prefix=WITH
// RUN: not obelisk -emit-sim -DREAL_POINT %s 2>&1 | FileCheck %s --check-prefix=REAL-POINT
// RUN: not obelisk -emit-sim -DBIN_IFF %s 2>&1 | FileCheck %s --check-prefix=BIN-IFF
// RUN: not obelisk -emit-sim -DQUERY_ONE %s 2>&1 | FileCheck %s --check-prefix=QUERY-ONE
// RUN: not obelisk -emit-sim -DPOINT_METHOD %s 2>&1 | FileCheck %s --check-prefix=POINT-METHOD
// RUN: not obelisk -emit-sim -DCLASS_MEMBER %s 2>&1 | FileCheck %s --check-prefix=CLASS

`ifdef CLASS_MEMBER
class covergroup_owner;
  covergroup cg;
    cp: coverpoint 1 {
      bins one = {1};
    }
  endgroup
endclass
module covergroup_unsupported;
  covergroup_owner owner;
endmodule
`else
module covergroup_unsupported;
  logic clock;
  int source;
  real real_value;

`ifdef CTOR
  covergroup cg(int argument);
`elsif EVENT
  covergroup cg @(posedge clock);
`elsif FORMAL
  covergroup cg with function sample(input int samples[2]);
`else
  covergroup cg;
`endif

`ifdef AUTO
    cp: coverpoint source;
`elsif BIN_ARRAY
    cp: coverpoint source {
      bins values[2] = {1, 2};
    }
`elsif WILDCARD
    cp: coverpoint source {
      wildcard bins values = {4'b1x01};
    }
`elsif TRANSITION
    cp: coverpoint source {
      bins transition = (1 => 2);
    }
`elsif IGNORE
    cp: coverpoint source {
      ignore_bins ignored = {1};
    }
`elsif ILLEGAL
    cp: coverpoint source {
      illegal_bins invalid = {1};
    }
`elsif CROSS
    a: coverpoint source {
      bins one = {1};
    }
    b: coverpoint source {
      bins two = {2};
    }
    ab: cross a, b;
`elsif OPTION
    option.at_least = 2;
    cp: coverpoint source {
      bins one = {1};
    }
`elsif NONCONST
    cp: coverpoint source {
      bins dynamic = {source};
    }
`elsif NONCONST_RANGE
    cp: coverpoint source {
      bins dynamic = {[0:source]};
    }
`elsif WITH_EXPR
    cp: coverpoint source {
      bins selected = cp with (item > 0);
    }
`elsif REAL_POINT
    cp: coverpoint real_value {
      bins zero = {0};
    }
`elsif BIN_IFF
    cp: coverpoint source {
      bins conditional = {1} iff (clock);
    }
`else
    cp: coverpoint source {
      bins one = {1};
    }
`endif
  endgroup

`ifdef QUERY_ONE
  cg c;
  int covered;
  initial begin
    c = new;
    $display("%f", c.get_inst_coverage(covered));
  end
`elsif POINT_METHOD
  cg c;
  initial begin
    c = new;
    $display("%f", c.cp.get_inst_coverage());
  end
`endif
endmodule
`endif

// CTOR: covergroup constructor formals are not supported
// EVENT: coverage events and automatic sampling are not supported
// FORMAL: coverage sample formals must be scalar integral inputs
// AUTO: coverpoints require explicit named bins
// BIN-ARRAY: coverage bin arrays and automatic bin counts are not supported
// WILDCARD: wildcard coverage bins are not supported
// TRANSITION: transition coverage bins are not supported
// IGNORE: ignore_bins are not supported
// ILLEGAL: illegal_bins are not supported
// CROSS: coverage crosses are not supported
// OPTION: covergroup coverage options are not supported
// NONCONST: coverage bin values must be elaboration-time constants
// NONCONST-RANGE: coverage bin range bounds must be elaboration-time constants
// WITH: coverage bin with/select expressions are not supported
// REAL-POINT: coverpoint expressions must have a two-state or four-state integral type
// BIN-IFF: bin-level iff is not supported
// QUERY-ONE: coverage queries require either zero or two output arguments
// POINT-METHOD: coverpoint methods are not supported
// CLASS: class-member and inherited covergroups are not executable
