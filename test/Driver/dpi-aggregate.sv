// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_aggregate_impl.c -I%resource_dir/include -o %t.o
// RUN: %obelisk --target=native -o %t.native %s %t.o
// RUN: %t.native | FileCheck %s
// RUN: %obelisk --execution-tier=bytecode -o %t.bytecode %s %t.o
// RUN: %t.bytecode | FileCheck %s
// RUN: %obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h
// RUN: %llvm_dist/bin/clang -x c -fsyntax-only -include %t.h \
// RUN:   -I%resource_dir/include /dev/null

module dpi_aggregate;
  typedef struct {
    int id;
    logic [4:0] state;
    byte bytes[2:0];
    string label;
    chandle token;
  } payload_t;

  import "DPI-C" context task mutate_aggregate(
      inout payload_t payload, inout int values[3:1]);

  task automatic aggregate_work(
      inout payload_t payload, inout int values[3:1]);
    $display("export-in=%0d/%b/%h,%h,%h/%s values=%0d,%0d,%0d",
             payload.id, payload.state, payload.bytes[2], payload.bytes[1],
             payload.bytes[0], payload.label, values[3], values[2], values[1]);
    payload.id = 52;
    payload.bytes[0] = 8'h55;
    payload.label = "from-sv";
    values[3] = 201;
  endtask
  export "DPI-C" c_aggregate_work = task aggregate_work;

  payload_t payload =
      '{17, 5'bx10z1, '{8'h31, 8'h32, 8'h33}, "initial", null};
  int values[3:1] = '{11, 22, 33};

  initial begin
    mutate_aggregate(payload, values);
    $display("after=%0d/%b/%h,%h,%h/%s values=%0d,%0d,%0d",
             payload.id, payload.state, payload.bytes[2], payload.bytes[1],
             payload.bytes[0], payload.label, values[3], values[2],
             values[1]);
  end
endmodule

// CHECK: raw=17/19/12/33,32,31/initial/0 values=33,22,11
// CHECK: export-in=42/1zx10/41,42,43/from-c values=101,102,103
// CHECK: export-raw=52/55,42,41/from-sv/1234 values=103,102,201
// CHECK: after=52/1zx10/41,42,55/from-sv values=201,102,103
// HEADER: typedef struct dpi_aggregate {
// HEADER: svLogicVecVal state[1];
// HEADER: int8_t bytes[3];
// HEADER: const char * label;
// HEADER: void * token;
// HEADER-DAG: int mutate_aggregate(dpi_aggregate *arg0, int32_t arg1[3]);
// HEADER-DAG: int c_aggregate_work(dpi_aggregate *arg0, int32_t arg1[3]);
