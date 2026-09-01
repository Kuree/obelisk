// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 21.3.4.4: a partial $fread updates only the bits
// represented by bytes actually read. Drive unit lowering directly so this
// test covers exactly the pass responsible for preserving the other bits.

!logic9 = !obelisk.integral<9, false, true, 8 : 0, logic>
!integer = !obelisk.integral<32, true, true, 31 : 0, integer>

module {
  obelisk_sim.design @fread_partial {
    obelisk_sim.code_unit.decl 9901001 in 0 function
        hierarchy "top.fread_partial"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<9> design
        hierarchy "top.value"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design
        hierarchy "top.fd"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<32> design
        hierarchy "top.count"

    // CHECK-LABEL: obelisk_sim.func @read
    // CHECK: %[[DATA:.*]], %[[COUNT:.*]] = obelisk_sim.file.read_packed
    // CHECK: %[[OLD:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[READ_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[DATA]]
    // CHECK: %[[LOW:[0-9]+]] = arith.muli
    // CHECK: %[[PART:.*]] = obelisk_sim.logic.dyn_extract %[[READ_LOGIC]] from %[[LOW]]
    // CHECK: %[[MERGED:.*]] = obelisk_sim.logic.dyn_insert %[[PART]] into %[[OLD]] at %[[LOW]]
    // CHECK: obelisk_sim.ref.store %[[MERGED]] to %arg1
    obelisk_sim.func @read(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.logic<9>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %fd: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %count: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.fd", argument = 2,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.count", argument = 3,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9901001 : i64,
          obelisk_sim.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, node_id = 2 : i64,
            semantic_type = !integer} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.count",
              referenced_symbol = @count, semantic_type = !integer} {
          }
          obelisk.sv.expression.call attributes {
              argument_count = 2 : i64, callee_name = "$fread",
              constraint_restrictions = [],
              defaulted_arguments = array<i64: 0, 0>,
              has_inline_constraints = false, has_iterator_expression = false,
              has_output_arguments = true, has_this_class = false,
              is_signed = true, is_super_class = false, is_system_call = true,
              node_id = 4 : i64, semantic_type = !integer,
              subroutine_kind = 0 : i32, system_library_cell = "work.top",
              system_scope_path = "top"} {
            obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32, is_signed = false,
                node_id = 5 : i64, semantic_type = !logic9} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 6 : i64,
                  referenced_path = "top.value", referenced_symbol = @value,
                  semantic_type = !logic9} {
              }
              obelisk.sv.expression.empty_argument attributes {
                  is_signed = false, node_id = 7 : i64,
                  semantic_type = !logic9} {
              }
            }
            obelisk.sv.expression.named_value attributes {
                is_signed = true, node_id = 8 : i64,
                referenced_path = "top.fd", referenced_symbol = @fd,
                semantic_type = !integer} {
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
