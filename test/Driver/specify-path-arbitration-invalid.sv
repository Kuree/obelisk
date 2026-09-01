// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

module invalid_arbitrated_path(
    input wire enable, input wire direction, input wire data,
    input wire path_only,
    output wire value);
  assign value = enable && direction ? data : 1'bz;
  specify
    (path_only => value) = 2;
    (enable *> value) = 4;
  endspecify
endmodule

// A declared source absent from the driver sensitivity cannot update its
// snapshot until an unrelated later driver wake, so reject that stale
// event-to-path arbitration shape.
// CHECK: error: simple specify path driver must directly observe every declared whole input
