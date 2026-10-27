// RUN: talos-opt %s --split-input-file --verify-diagnostics

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    // expected-error@+1 {{modport export call requires an extern member}}
    simulation.vpi_interface_call.dispatch @call member @f access 2
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    // expected-error@+1 {{interface access must be plain, import, or export}}
    simulation.vpi_interface_call.dispatch @call member @f access 4
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    // expected-error@+1 {{instance must name a physical interface scope}}
    simulation.vpi_interface_instance.bind 1 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{code unit must name a task or function declaration}}
    simulation.vpi_callable.decl 44 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{declaration must preserve the member's exact original ABI}}
    simulation.vpi_callable.decl 45 signature () -> i64 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{only extern members must retain an explicit export inventory}}
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f {interface_exports = array<i64: 46>}
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{exports must name unique positive provider IDs distinct from the declaration}}
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f {interface_exports = array<i64: 45>}
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{signature and formal directions must have equal arity}}
    simulation.vpi_callable.decl 45 signature () -> i32 directions [0] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{code-unit ID must be positive}}
    simulation.vpi_callable.decl -1 signature () -> i32 directions [] member @f
    simulation.vpi_callable.decl 46 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions [] {is_extern}
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    // expected-error@+1 {{only an extern task may use fork/join aggregation}}
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f {interface_exports = array<i64: 46>, is_fork_join}
    simulation.vpi_callable.decl 46 signature () -> i32 directions []
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    simulation.vpi_interface_call.dispatch @call member @f access 0
  }
}

// -----

#bus = #simulation.vpi_type<kind = virtual_interface, isSigned = false, isFourState = false, name = "bus", symbol = @bus, modport = "", range = [], children = [], childNames = []>
module {
  simulation.design @dispatch {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.scope.decl 2 parent 1 hierarchy "top.bus" vpi_kind 601
    simulation.scope.decl 3 parent 0 hierarchy "second" vpi_kind 32
    simulation.scope.decl 4 parent 3 hierarchy "second.bus" vpi_kind 601
    simulation.vpi_object.anchor @owner id 0 type 32 in 1 ordinal 0 hierarchy "top" debug "top" {backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>}
    simulation.vpi_typespec.decl @bus id 0 in 1 owner @owner hierarchy "bus" debug "bus" {origin = 1 : i32, target_type = #bus}
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 2 function hierarchy "top.bus.f" debug "f"
    simulation.code_unit.decl 46 in 4 function hierarchy "second.bus.f" debug "f"
    simulation.vpi_interface_member.decl @f of @bus name "f" function signature () -> i32 directions []
    simulation.vpi_interface_instance.bind 2 to @bus
    simulation.vpi_interface_instance.bind 4 to @bus
    simulation.vpi_callable.decl 45 signature () -> i32 directions [] member @f
    simulation.vpi_call.decl @call id 10 in 44 scope 1 type 19 name "f" {is_indirect}
    // expected-error@+1 {{member inventory must cover every instance of its nominal interface}}
    simulation.vpi_interface_call.dispatch @call member @f access 1
  }
}
