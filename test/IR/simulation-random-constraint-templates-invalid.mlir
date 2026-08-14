// RUN: obelisk-opt --split-input-file --verify-diagnostics %s

module {
  obelisk_sim.design @unknown_owner {
    obelisk_sim.scope.decl 0
    // expected-error @below {{references an unknown owner class}}
    obelisk_sim.random.constraint_template @constraints of @Missing
        attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @empty_references {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random-value references must be absent when empty}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_references {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value references contain a duplicate}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>,
        #obelisk_sim.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @non_handle_path {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value path field @C_value must be a strong instance class handle}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, path = [@C_value], target = @C_value,
          low = 0, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @out_of_range {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    // expected-error @below {{random-value target does not contain packed bit range [4, 12)}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, target = @C_value, low = 4, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @unknown_storage {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{random-value reference names unknown storage ID 7}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = storage, storage = 7 : i64, low = 0, width = 1>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @empty_blocks {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{requires at least one constraint-block reference}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = []
    } {
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_block_storage_type {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{constraint-block storage ID 0 must be i64}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = storage, storage = 0 : i64>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @no_constraints {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    // expected-error @below {{requires at least one hard or soft constraint}}
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %zero = arith.constant 0 : i1
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_value_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{reference index is outside the template inventory}}
      %value = obelisk_sim.random.constraint_value 1 : i8
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_value_width {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @C_value of @C at 0 : i8 {
      is_static = false, is_weak = false
    }
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      references = [
        #obelisk_sim.random_value_reference<
          kind = object_field, target = @C_value, low = 0, width = 8>
      ],
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{result width does not match the symbolic reference}}
      %value = obelisk_sim.random.constraint_value 0 : i4
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}

// -----

module {
  obelisk_sim.design @bad_sink_block {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      // expected-error @below {{constraint-block index is outside the template inventory}}
      obelisk_sim.random.hard_constraint %true block 1
    }
  }
}

// -----

module {
  obelisk_sim.design @duplicate_soft_priority {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      %true = arith.constant true
      obelisk_sim.random.soft_constraint %true block 0 priority 0
      // expected-error @below {{soft priority is duplicated in its template}}
      obelisk_sim.random.soft_constraint %true block 0 priority 0
    }
  }
}

// -----

module {
  obelisk_sim.design @noninteger_dataflow {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.random.constraint_template @constraints of @C attributes {
      constraint_blocks = [
        #obelisk_sim.random_constraint_block_reference<
          kind = object_block, index = 0 : i32>
      ]
    } {
      // expected-error @below {{random constraint template dataflow must use signless integers}}
      %real = arith.constant 0.0 : f64
      %true = arith.constant true
      obelisk_sim.random.hard_constraint %true block 0
    }
  }
}
