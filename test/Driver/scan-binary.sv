// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s --check-prefix=SIM
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

// IEEE 1800-2017 Table 21-8: %u consumes native-endian 32-bit two-state
// words, while %z consumes native s_vpi_vecval-compatible aval/bval pairs.
module scan_binary;
  typedef struct {
    byte first;
    logic [3:0] second;
  } raw_struct_t;
  typedef struct {
    logic [3:0] head;
    raw_struct_t tail;
  } nested_raw_struct_t;
  typedef union {
    logic [7:0] first;
    logic [15:0] wider_alias;
  } raw_union_t;

  string output_path;
  string raw;
  string reverse_raw;
  string partial;
  logic [68:0] source;
  logic [68:0] two_state;
  logic [68:0] four_state;
  logic [68:0] destination_u;
  logic [68:0] destination_z;
  raw_struct_t raw_struct;
  nested_raw_struct_t nested_struct;
  raw_union_t raw_union;
  integer descriptor;
  integer position;
  integer status;

  initial begin
    source = '0;
    source[0] = 1;
    source[31] = 1;
    source[32] = 1'bx;
    source[33] = 1'bz;
    source[68] = 1;
    two_state = source;
    two_state[32] = 0;
    two_state[33] = 0;
    four_state = source;

    // A 69-bit destination uses three words: 12 bytes for %u and 24 for %z.
    raw = $sformatf("%u%z", source, source);
    destination_u = '1;
    destination_z = '0;
    status = $sscanf(raw, "%U%Z", destination_u, destination_z);
    $display("roundtrip=%0d:%0d:%0d:%0d:%0d:%0d", status,
             destination_u === two_state, destination_z === four_state,
             destination_u[32] === 1'b0, destination_u[33] === 1'b0,
             raw.len());

    destination_z = '0;
    status = $sscanf(raw, "%*12u%z", destination_z);
    $display("suppressed=%0d:%0d", status,
             destination_z === four_state);

    reverse_raw = $sformatf("%z%u", source, source);
    destination_u = '1;
    status = $sscanf(reverse_raw, "%*24z%u", destination_u);
    $display("suppressed-z=%0d:%0d", status,
             destination_u === two_state);

    destination_u = '1;
    status = $sscanf(raw, "%11u", destination_u);
    $display("short-width=%0d:%0d", status, destination_u === '1);

    destination_u = '1;
    status = $sscanf({"bad=", raw}, "tag=%u", destination_u);
    $display("prefix-mismatch=%0d:%0d", status, destination_u === '1);

    // Dropping the last byte from a %z record fails without assigning.
    partial = raw.substr(12, raw.len() - 2);
    destination_z = '0;
    status = $sscanf(partial, "%z", destination_z);
    $display("partial-string=%0d:%0d", status, destination_z === '0);

    // Raw aggregate destinations recursively concatenate unpacked structure
    // members in declaration order. An unpacked union uses only its first
    // declared member, matching the raw-format legality rule.
    raw = $sformatf("%z%z%z%z%z%z", 8'ha5, 4'bxz10, 4'h6, 8'ha5,
                    4'bxz10, 8'b10xz01z1);
    status = $sscanf(raw, "%z%z%z", raw_struct, nested_struct, raw_union);
    $display("aggregates=%0d:%0d:%0d:%0d", status,
             raw_struct.first === 8'ha5 && raw_struct.second === 4'bxz10,
             nested_struct.head === 4'h6 &&
                 nested_struct.tail.first === 8'ha5 &&
                 nested_struct.tail.second === 4'bxz10,
             raw_union.first === 8'b10xz01z1);
    raw = $sformatf("%u%u", 8'ha5, 4'bxz10);
    status = $sscanf(raw, "%u", raw_struct);
    $display("aggregate-u=%0d:%0d", status,
             raw_struct.first === 8'ha5 && raw_struct.second === 4'b0010);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "tag=%u%z", source, source);
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    destination_u = '0;
    destination_z = '0;
    status = $fscanf(descriptor, "tag=%u%z", destination_u, destination_z);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("file=%0d:%0d:%0d:%0d", status,
             destination_u === two_state, destination_z === four_state,
             position);

    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "%z%z%z%z%z%z", 8'ha5, 4'bxz10, 4'h6, 8'ha5,
            4'bxz10, 8'b10xz01z1);
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    status = $fscanf(descriptor, "%z%z%z", raw_struct, nested_struct,
                     raw_union);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("aggregate-file=%0d:%0d:%0d:%0d:%0d", status,
             raw_struct.first === 8'ha5 && raw_struct.second === 4'bxz10,
             nested_struct.head === 4'h6 &&
                 nested_struct.tail.first === 8'ha5 &&
                 nested_struct.tail.second === 4'bxz10,
             raw_union.first === 8'b10xz01z1, position);

    descriptor = $fopen(output_path, "w");
    $fwrite(descriptor, "%u", source);
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    destination_z = '0;
    status = $fscanf(descriptor, "%z", destination_z);
    position = $ftell(descriptor);
    $fclose(descriptor);
    $display("partial-file=%0d:%0d:%0d", status,
             destination_z === '0, position);
  end
endmodule

// SIM: obelisk_sim.string.scan_raw
// SIM-SAME: four_state = false
// SIM-SAME: !obelisk_sim.logic<69>
// SIM: obelisk_sim.string.skip_raw
// SIM-SAME: byte_count = 12 : i64
// SIM: obelisk_sim.file.scan_raw
// SIM-SAME: four_state = false
// SIM-SAME: !obelisk_sim.logic<69>

// CHECK: roundtrip=2:1:1:1:1:36
// CHECK-NEXT: suppressed=1:1
// CHECK-NEXT: suppressed-z=1:1
// CHECK-NEXT: short-width=0:1
// CHECK-NEXT: prefix-mismatch=0:1
// CHECK-NEXT: partial-string=0:1
// CHECK-NEXT: aggregates=3:1:1:1
// CHECK-NEXT: aggregate-u=1:1
// CHECK-NEXT: file=2:1:1:40
// CHECK-NEXT: aggregate-file=3:1:1:1:48
// CHECK-NEXT: partial-file=-1:1:12
