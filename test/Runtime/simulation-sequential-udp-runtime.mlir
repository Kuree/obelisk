// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @sequential_udp_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 9230000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9230001 in 0 continuous hierarchy "top.udp"
    obelisk_sim.code_unit.decl 9230002 in 0 initial hierarchy "top.stimulus"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9230000 : i64,
                    obelisk_sim.lowered} {
      %input = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %out = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %net = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %udp = obelisk_sim.spawn @udp(%ctx, %out, %input) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %stimulus = obelisk_sim.spawn @stimulus(%ctx, %input, %net) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @udp(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9230001 : i64,
                    schedule.primitive_name = "udp_rise",
                    obelisk_sim.udp_metadata = {
                      init_value = "1'b0", is_edge_sensitive = true,
                      is_sequential = true, name = "udp_rise",
                      port_directions = array<i64: 2, 0>,
                      port_names = ["out", "input"],
                      table_edges = array<i64: 1>, table_inputs = ["r"],
                      table_outputs = array<i64: 49>,
                      table_states = array<i64: 63>},
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.input", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.input", referenced_symbol = @input, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    obelisk_sim.func private @stimulus(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %input: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9230002 : i64,
                    obelisk_sim.lowered} {
      %first_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %first_tick to ^drive_zero
    ^drive_zero:
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %input : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %second_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %second_tick to ^drive_one
    ^drive_one:
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %one to %input : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %sample_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %sample_tick to ^sample
    ^sample:
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format = obelisk_sim.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %value) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
