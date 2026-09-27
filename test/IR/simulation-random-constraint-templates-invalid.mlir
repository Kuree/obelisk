// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  simulation.design @unknown_owner {
    simulation.scope.decl 0
    // expected-error @below {{references an unknown owner class}}
    simulation.random.constraint_template @constraints of @Missing
        attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @empty_references {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random-value references must be absent when empty}}
    simulation.random.constraint_template @constraints of @C attributes {
      references = [],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @duplicate_references {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value references contain a duplicate}}
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>,
        #simulation.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @non_handle_path {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value path field @C_value must be a strong instance class handle}}
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, path = [@C_value], target = @C_value,
          low = 0, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @out_of_range {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value target does not contain packed bit range [4, 12)}}
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, target = @C_value, low = 4, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @unknown_storage {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random-value reference names unknown storage ID 7}}
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = storage, storage = 7 : i64, low = 0, width = 1>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @empty_blocks {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{requires at least one constraint-block reference}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = []
    } {
    }
  }
}

// -----

module {
  simulation.design @wrong_block_storage_type {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block storage ID 0 must be i64}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = storage, storage = 0 : i64>
      ]
    } {
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @no_constraints {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{requires at least one hard or soft constraint}}
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %zero = arith.constant 0 : i1
    }
  }
}

// -----

module {
  simulation.design @bad_value_index {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{reference index is outside the template inventory}}
      %value = simulation.random.constraint_value 1 : i8
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @bad_value_width {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    simulation.random.constraint_template @constraints of @C attributes {
      references = [
        #simulation.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{result width does not match the symbolic reference}}
      %value = simulation.random.constraint_value 0 : i4
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  simulation.design @bad_sink_block {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      // expected-error @below {{constraint-block index is outside the template inventory}}
      simulation.random.hard_constraint %true block 1
    }
  }
}

// -----

module {
  simulation.design @duplicate_soft_priority {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      simulation.random.soft_constraint %true block 0 priority 0
      // expected-error @below {{soft priority is duplicated in its template}}
      simulation.random.soft_constraint %true block 0 priority 0
    }
  }
}

// -----

module {
  simulation.design @noninteger_dataflow {
    simulation.scope.decl 0
    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #simulation.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{random constraint template dataflow must use signless integers}}
      %real = arith.constant 0.0 : f64
      %true = arith.constant true
      simulation.random.hard_constraint %true block 0
    }
  }
}
