// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --test-obelisk-managed-class-layout-analysis

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Hidden
} {
  simulation.design @root_hidden {
    simulation.scope.decl 0
    // expected-error @below {{has a local or protected member but the class bit-stream source is not the current-instance 'this'}}
    simulation.class.decl @Hidden id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.field @Hidden_value of @Hidden at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<protected>
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Root
} {
  simulation.design @abstract_static_cycle {
    simulation.scope.decl 0
    simulation.class.decl @Root id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    // expected-error @below {{class bit-stream type graph contains a cycle}}
    simulation.class.decl @Abstract id 2 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.class.field @Root_child of @Root at 0 :
        !simulation.class_handle<@Abstract> {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
    simulation.class.field @Abstract_self of @Abstract at 0 :
        !simulation.class_handle<@Abstract> {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Root,
  test.class_bitstream_allow_hidden
} {
  simulation.design @nested_hidden {
    simulation.scope.decl 0
    simulation.class.decl @Root id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    // expected-error @below {{has a local or protected member reached through a nested class bit-stream handle}}
    simulation.class.decl @Child id 2 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.field @Root_child of @Root at 0 :
        !simulation.class_handle<@Child> {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
    simulation.class.field @Child_value of @Child at 0 : i8 {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<local>
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @A
} {
  simulation.design @cycle {
    simulation.scope.decl 0
    // expected-error @below {{class bit-stream type graph contains a cycle}}
    simulation.class.decl @A id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.decl @B id 2 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.field @A_b of @A at 0 :
        !simulation.class_handle<@B> {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
    simulation.class.field @B_a of @B at 0 :
        !simulation.class_handle<@A> {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Bad
} {
  simulation.design @unsupported {
    simulation.scope.decl 0
    simulation.class.decl @Bad id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    // expected-error @below {{is not a legal member of a class bit-stream type}}
    simulation.class.field @Bad_process of @Bad at 0 :
        !simulation.process {
      is_static = false, is_weak = false,
      simulation.class_bitstream_member,
      simulation.class_bitstream_visibility = #simulation.member_visibility<public>
    }
  }
}
