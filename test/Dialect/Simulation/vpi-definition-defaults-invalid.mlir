// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @missing_root {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{original default fact requires exactly one source root}}
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @root_without_fact {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P"
    simulation.vpi_expression.decl @value id 0 type 7 owned_by #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 : i32 {definition = @cell, vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    // expected-error @+1 {{definition default requires one available parameter Expr slot}}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @unavailable_and_available {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    // expected-error @+1 {{original default is exclusive with unavailability}}
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default, unavailable_default}
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @self_reference {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
    // expected-error @+1 {{default leaf must be an exact preceding parameter of the same kind}}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @P>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @forward_reference {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
    simulation.vpi_definition_member.decl @Q of @cell type 41 ordinal 1 name "Q"
    // expected-error @+1 {{default leaf must be an exact preceding parameter of the same kind}}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @Q>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @foreign_definition {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P"
    simulation.vpi_definition_member.decl @Q of @cell type 41 ordinal 1 name "Q" {has_original_default}
    simulation.vpi_definition.decl @other type 32 name "other"
    simulation.vpi_definition_member.decl @foreign of @other type 41 ordinal 0 name "foreign"
    // expected-error @+1 {{default leaf must be an exact preceding parameter of the same kind}}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @Q> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @foreign>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @two_contexts {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
    // expected-error @+1 {{requires exactly one nonnegative scope or definition context}}
    simulation.vpi_expression.decl @value id 0 type 7 in 0 owned_by #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 : i32 {definition = @cell, vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @wrong_context {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
    simulation.vpi_definition.decl @other type 32 name "other"
    // expected-error @+1 {{definition occurrence must match its declaring parameter}}
    simulation.vpi_expression.decl @value id 0 type 7 owned_by #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 : i32 {definition = @other, vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @two_owners {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P" {has_original_default}
    simulation.vpi_definition_member.decl @Q of @cell type 41 ordinal 1 name "Q" {has_original_default}
    // expected-error @+1 {{definition occurrence must match its declaring parameter}}
    simulation.vpi_expression.decl @value id 0 type 7 owned_by #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 : i32 {definition = @cell, vpi_type = #int, immutable_value = #simulation.frozen_constant<value = [1 : i32, 0 : i32], isSigned = true> : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @P> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @Q> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
  }
}

// -----

#int = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module {
  simulation.design @instance_owner_reference {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P"
    simulation.code_unit.decl 1 in 0 initial hierarchy "start"
    simulation.statement.decl 1 in 1 scope 0 type 22
    // expected-error @+1 {{shared declaration symbols require an exact definition default}}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = statement, id = 1 : i64> selector 71 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @P>
  }
}

// -----

module {
  simulation.design @event_operator {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_definition.decl @cell type 32 name "cell"
    simulation.vpi_definition_member.decl @P of @cell type 41 ordinal 0 name "P"
    simulation.vpi_definition_member.decl @Q of @cell type 41 ordinal 1 name "Q" {has_original_default}
    // expected-error @+1 {{definition default requires an unbound value expression operator}}
    simulation.vpi_expression.decl @value id 0 type 39 owned_by #simulation.vpi_ref<kind = symbol, symbol = @Q> selector 102 handle ordinal 0 {definition = @cell, source_only, op_type = 35 : i32}
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @Q> selector 102 handle ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @value>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @value> selector 97 iterate ordinal 0 to #simulation.vpi_ref<kind = symbol, symbol = @P>
    simulation.vpi_relation.decl #simulation.vpi_ref<kind = symbol, symbol = @value> selector 97 iterate ordinal 1 to #simulation.vpi_ref<kind = symbol, symbol = @P>
  }
}
