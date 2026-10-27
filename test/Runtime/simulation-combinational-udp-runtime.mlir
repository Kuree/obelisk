// RUN: talos-opt %s \
// RUN:   --pass-pipeline='builtin.module(talos-sim-prepare-unit-lowering,simulation.design(simulation.func(talos-sim-lower-unit),talos-sim-build-compute-graph,talos-sim-verify-compute-graph),encode-talos-sim-to-bytecode{vpi=off require-bytecode=true},convert-talos-sim-processes-to-llvm-coroutines)' \
// RUN:   | talos-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Hand-authored semantic/runtime coverage for combinational UDP lookup.  The
// Z0 input exercises four-state normalization and exact x matching in both
// execution tiers.  The lowering test covers the complete ordered table
// structure and the external SV test covers the full dynamic input matrix.
// CHECK: 1

!logic1 = !talos.integral<1, false, true, 0 : 0, logic>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @combinational_udp_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.code_unit.decl 9210000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9210001 in 0 continuous hierarchy "top.udp"
    simulation.code_unit.decl 9210002 in 0 initial hierarchy "top.check"

    simulation.func @__talos_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9210000 : i64,
                    simulation.lowered} {
      %a = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %b = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %out = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %net = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      simulation.ref.store %z to %a : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %zero to %b : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %udp = simulation.spawn @udp(%ctx, %out, %a, %b) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %check = simulation.spawn @check(%ctx, %net) :
          !simulation.context,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @udp(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %a: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9210001 : i64,
                    schedule.primitive_name = "udp_nonansi",
                    simulation.udp_metadata = #simulation.udp<name = "udp_nonansi", ports = ["out", "a", "b"], portDirections = [1, 0, 0], sequential = false, edgeSensitive = false,
                      rows = [#simulation.udp_row<inputs = "00", state = "", output = "0", edge = false>,
                        #simulation.udp_row<inputs = "0?", state = "", output = "0", edge = false>,
                        #simulation.udp_row<inputs = "1b", state = "", output = "1", edge = false>,
                        #simulation.udp_row<inputs = "x0", state = "", output = "1", edge = false>]>,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.a", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.b", argument = 3, kind = direct, copyOut = false>]} {
      talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 10 : i64, semantic_type = !logic1} {
        talos.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        talos.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !logic1} {}
      }
      talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic1} {}
      talos.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic1} {}
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9210002 : i64,
                    simulation.lowered} {
      %tick = simulation.time.constant 1
      simulation.suspend.delay %tick to ^sample
    ^sample:
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %value) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<1>
      simulation.return
    }
  }
}
