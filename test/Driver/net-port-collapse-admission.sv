// RUN: obelisk -O0 --vpi=read -emit-sim %s -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT

// IEEE 1800-2023 23.3.3.2, 23.3.3.3, 23.3.3.7.
module ordinary(input wire [7:0] pin);
endmodule
module converted(input wire signed [7:0] pin);
endmodule
module selected(input wire [3:0] pin);
endmodule
module variable_port(input var logic [7:0] pin);
endmodule
module pulled(input tri0 [7:0] pin);
endmodule
module scalar(input wire pin);
endmodule
module admission;
  logic [7:0] drive = '1;
  wire [7:0] bus = drive;
  wire #2 delayed = drive[0];
  ordinary good(bus);
  converted conversion(bus);
  selected slice(bus[3:0]);
  variable_port variable_connection(bus);
  pulled mixed(bus);
  scalar delayed_connection(delayed);
  initial begin
    #3;
    if (good.pin !== '1 || conversion.pin !== '1 || slice.pin !== '1 ||
        variable_connection.pin !== '1 || mixed.pin !== '1 ||
        delayed_connection.pin !== '1) $fatal(1, "initial");
    drive = 0;
    #3;
    if (good.pin !== 0 || conversion.pin !== 0 || slice.pin !== 0 ||
        variable_connection.pin !== 0 || mixed.pin !== 0 ||
        delayed_connection.pin !== 0) $fatal(1, "updated");
    $display("admission passed");
    $finish;
  end
endmodule

// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "admission.conversion.pin"
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "admission.slice.pin"
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "admission.mixed.pin"
// CHECK-DAG: simulation.storage.decl {{.*}} hierarchy "admission.variable_connection.pin"
// CHECK-DAG: simulation.net.decl {{.*}} hierarchy "admission.delayed_connection.pin"
// CHECK-DAG: simulation.vpi_net_identity.decl {{.*}} hierarchy "admission.good.pin"
// OUTPUT: admission passed
