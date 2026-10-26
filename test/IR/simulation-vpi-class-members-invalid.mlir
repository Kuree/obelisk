// RUN: obelisk-opt %s --split-input-file --verify-diagnostics -o /dev/null

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{references an unknown class field}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @missing
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{owner anchor must be a class definition}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @package field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = true, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{static class fields require exactly one static backing}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{static class fields require exactly one static backing}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 0 : i64
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = true, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{static storage must exist and match the class field type}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>, static_storage = 99 : i64
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{duplicate VPI source inventory ID 1}}
    simulation.vpi_class_member.decl @member id 1 in 0 owner @canonical field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = true, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{static event backing requires a scalar named event}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @field
        hierarchy "pkg.C.x" debug "x" {
      vpi_type = #simulation.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>, static_event = 0 : i64
    }
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    simulation.class.method @method of @cls slot 0 signature_id 1 :
        (!simulation.context, !simulation.class_handle<@cls>) -> i32 {
      is_final = false, is_pure = true, is_static = false,
      is_task = false, is_virtual = true
    }
    simulation.vpi_object.anchor @first id 2 type 20 in 0 parent @canonical
        ordinal 0 hierarchy "pkg.C.f" debug "f" {class_method = @method}
    // expected-error @+1 {{duplicates a class method in the same owner}}
    simulation.vpi_object.anchor @second id 3 type 20 in 0 parent @canonical
        ordinal 1 hierarchy "pkg.C.f" debug "f" {class_method = @method}
  }
}

// -----

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    // expected-error @+1 {{anonymous enum VPI value requires an exact source type identity}}
    simulation.vpi_class_member.decl @member id 2 in 0 owner @canonical field @field
        hierarchy "pkg.C.state" debug "state" {
      vpi_type = #simulation.vpi_type<kind = enum, name = "anonymous", isSigned = true,
        isFourState = false, range = [], children = [
          #simulation.vpi_type<kind = int, isSigned = true, isFourState = false,
            range = [31, 0], children = [], childNames = []>
        ], childNames = []>
    }
  }
}

// -----


module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.vpi_object.anchor @package id 0 type 600 in 0 ordinal 0
        hierarchy "pkg" debug "pkg"
    simulation.vpi_object.anchor @definition id 1 type 652 in 0 parent @package
        ordinal 0 hierarchy "pkg.C" debug "C" {
      backing = #simulation.vpi_backing<kind = class, symbol = @cls>
    }
    simulation.class.decl @cls id 1 {
      is_abstract = false, is_interface = false, is_final = false}
    simulation.class.field @field of @cls at 0 : i32 {
      is_static = false, is_weak = false}
    simulation.vpi_typespec.decl @canonical id 0 in 0 owner @definition
        hierarchy "pkg.C$type" debug "C" {
      origin = 3 : i32,
      target_type = #simulation.vpi_type<kind = class, isSigned = false,
        isFourState = false, symbol = @cls, range = [], children = [], childNames = []>
    }
    simulation.class.method @method of @cls slot 0 signature_id 1 :
        (!simulation.context, !simulation.class_handle<@cls>) -> i32 {
      is_final = false, is_pure = true, is_static = false,
      is_task = false, is_virtual = true
    }
    // expected-error @+1 {{class_method requires a nonempty declaration name}}
    simulation.vpi_object.anchor @method_anchor id 2 type 20 in 0 parent @canonical
        ordinal 0 hierarchy "pkg.C.f" debug "" {class_method = @method}
  }
}
