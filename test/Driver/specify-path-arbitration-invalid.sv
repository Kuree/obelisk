// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

module invalid_arbitrated_path(
    input wire enable, input wire direction, input wire data,
    output wire value);
  assign value = enable && direction ? data : 1'bz;
  specify
    (data => value) = 2;
    (enable *> value) = 4;
  endspecify
endmodule

// A source that affects the driver but is absent from every path would make
// event-to-path arbitration incomplete, so it remains a targeted diagnostic.
// CHECK: error: simple specify path driver must depend only on its declared whole inputs
