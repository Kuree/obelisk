// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --test-obelisk-managed-class-layout-analysis

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Hidden
} {
  obelisk_sim.design @root_hidden {
    obelisk_sim.scope.decl 0
    // expected-error @below {{has a local or protected member but the class bit-stream source is not the current-instance 'this'}}
    obelisk_sim.class.decl @Hidden id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @Hidden_value of @Hidden at 0 : i8 {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 1 : i32
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Root,
  test.class_bitstream_allow_hidden
} {
  obelisk_sim.design @nested_hidden {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @Root id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    // expected-error @below {{has a local or protected member reached through a nested class bit-stream handle}}
    obelisk_sim.class.decl @Child id 2 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @Root_child of @Root at 0 :
        !obelisk_sim.class_handle<@Child> {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 0 : i32
    }
    obelisk_sim.class.field @Child_value of @Child at 0 : i8 {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 2 : i32
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @A
} {
  obelisk_sim.design @cycle {
    obelisk_sim.scope.decl 0
    // expected-error @below {{class bit-stream type graph contains a cycle}}
    obelisk_sim.class.decl @A id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.decl @B id 2 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @A_b of @A at 0 :
        !obelisk_sim.class_handle<@B> {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 0 : i32
    }
    obelisk_sim.class.field @B_a of @B at 0 :
        !obelisk_sim.class_handle<@A> {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 0 : i32
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  test.class_bitstream_source = @Bad
} {
  obelisk_sim.design @unsupported {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @Bad id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    // expected-error @below {{is not a legal member of a class bit-stream type}}
    obelisk_sim.class.field @Bad_process of @Bad at 0 :
        !obelisk_sim.process {
      is_static = false, is_weak = false,
      obelisk_sim.class_bitstream_member,
      obelisk_sim.class_bitstream_visibility = 0 : i32
    }
  }
}
