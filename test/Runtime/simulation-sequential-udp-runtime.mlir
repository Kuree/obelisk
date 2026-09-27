// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// The first activation records X. The two later stores produce X->0 and 0->1;
// only the latter is an `r` row, proving previous-input state survives a real
// scheduler suspension in both tiers.
// CHECK: 1

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @sequential_udp_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.code_unit.decl 9230000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9230001 in 0 continuous hierarchy "top.udp"
    simulation.code_unit.decl 9230002 in 0 initial hierarchy "top.stimulus"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9230000 : i64,
                    simulation.lowered} {
      %input = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %out = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %udp = simulation.spawn @udp(%ctx, %out, %input) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %stimulus = simulation.spawn @stimulus(%ctx, %input, %net) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @udp(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9230001 : i64,
                    schedule.primitive_name = "udp_rise",
                    simulation.udp_metadata = {
                      init_value = "1'b0", is_edge_sensitive = true,
                      is_sequential = true, name = "udp_rise",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "input"],
                      table_edges = array<i64: 1>, table_inputs = ["r"],
                      table_outputs = array<i64: 49>,
                      table_states = array<i64: 63>},
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.input", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.input", referenced_symbol = @input, semantic_type = !logic1} {}
      simulation.return
    }

    simulation.func private @stimulus(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9230002 : i64,
                    simulation.lowered} {
      %first_tick = simulation.time.constant 1
      simulation.suspend.delay %first_tick to ^drive_zero
    ^drive_zero:
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %input : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %second_tick = simulation.time.constant 1
      simulation.suspend.delay %second_tick to ^drive_one
    ^drive_one:
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.ref.store %one to %input : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %sample_tick = simulation.time.constant 1
      simulation.suspend.delay %sample_tick to ^sample
    ^sample:
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %value) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      simulation.return
    }
  }
}
