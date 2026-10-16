// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

// named assignment
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  // expected-error @+1 {{parameter assignment must be unnamed}}
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "a" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// missing availability
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  // expected-error @+1 {{parameter assignment requires exactly one RHS availability fact}}
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name ""
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// conflicting availability
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  // expected-error @+1 {{parameter assignment requires exactly one RHS availability fact}}
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs, unavailable_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// unnamed default selection
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  // expected-error @+1 {{empty named actual requires unavailable source-default RHS}}
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {param_assign_default, unavailable_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// declaration flags
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  // expected-error @+1 {{parameter assignment cannot carry declaration facts}}
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs, is_local_param}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// effective assignment type
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  // expected-error @+1 {{parameter assignments have no effective specialization type}}
  simulation.vpi_definition_member.specialize @p member @a type #int
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// wrong target kind
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs, param_assign_type}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  // expected-error @+1 {{type parameter default cannot be a value expression}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// nonchild target
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 0 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  // expected-error @+1 {{assignment Lhs must name a nonlocal parameter of an exact child instance}}
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// local target
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N" {is_local_param}
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  // expected-error @+1 {{assignment Lhs must name a nonlocal parameter of an exact child instance}}
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// missing endpoint
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  // expected-error @+1 {{requires every parameter assignment's exact child endpoint}}
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// wrong endpoint representation
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  // expected-error @+1 {{assignment Lhs requires an exact contextual parameter}}
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = storage, id = 2>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// duplicate endpoint
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_member.decl @b of @parent type 40 ordinal 1 name "" {unavailable_rhs}
  simulation.vpi_definition_member.bind scope 1 member @b expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  // expected-error @+1 {{duplicates a parameter override target in one parent instance}}
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// value assignment concrete type
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  // expected-error @+1 {{requires an available TypeParameter original default member}}
  simulation.vpi_definition_type_default @a type #int
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// wrong root selector
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  simulation.vpi_definition.decl @parent type 32 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 32 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 102 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  // expected-error @+1 {{definition default requires one available parameter Expr slot}}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 102 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}

// -----

// interface parent
#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
#seven = #simulation.frozen_constant<value = [7 : i32, 0 : i32], isSigned = true> : i32
module {
 simulation.design @test {
  simulation.scope.decl 0 hierarchy "$root"
  // expected-error @+1 {{VPI definition kind must be a module, interface, program, gate, or switch}}
  simulation.vpi_definition.decl @parent type 600 name "parent"
  simulation.vpi_definition_member.decl @a of @parent type 40 ordinal 0 name "" {has_original_rhs}
  simulation.vpi_definition_specialization.decl @p of @parent
  simulation.scope.decl 1 parent 0 hierarchy "parent" vpi_kind 600 definition @parent specialization @p
  simulation.vpi_definition.decl @child type 32 name "child"
  simulation.vpi_definition_member.decl @N of @child type 41 ordinal 0 name "N"
  simulation.vpi_definition_specialization.decl @c of @child
  simulation.vpi_definition_member.specialize @c member @N type #int {immutable_value = #seven}
  simulation.scope.decl 2 parent 1 hierarchy "parent.child" vpi_kind 32 definition @child specialization @c
  simulation.vpi_definition_member.bind scope 1 member @a expr <kind = definition_member, id = 2, symbol = @N>
  simulation.vpi_expression.decl @rhs id 0 type 7 owned_by <kind = symbol, symbol = @a> selector 82 handle ordinal 0 : i32 {definition = @parent, immutable_value = #seven, vpi_type = #int}
  simulation.vpi_relation.decl <kind = symbol, symbol = @a> selector 82 handle ordinal 0 to <kind = symbol, symbol = @rhs>
 }
}
