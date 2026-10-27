// RUN: talos-opt %s -split-input-file -verify-diagnostics -o /dev/null

module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    // expected-error@+1 {{initializer requires an internal function without a callable source scope}}
    simulation.vpi_initializer_binding 99 storage 0
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    // expected-error@+1 {{initializer requires static storage in its exact physical scope}}
    simulation.vpi_initializer_binding 41 storage 99
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init"
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    // expected-error@+1 {{initializer requires an internal function without a callable source scope}}
    simulation.vpi_initializer_binding 41 storage 0
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 1 : i32 automatic hierarchy "top.x"
    // expected-error@+1 {{initializer requires static storage in its exact physical scope}}
    simulation.vpi_initializer_binding 41 storage 0
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 0 : i32 design hierarchy "top.x"
    // expected-error@+1 {{initializer requires static storage in its exact physical scope}}
    simulation.vpi_initializer_binding 41 storage 0
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    simulation.vpi_initializer_binding 41 storage 0
    // expected-error@+1 {{duplicates an initializer or source storage binding}}
        simulation.vpi_initializer_binding 41 storage 0
  }
}

// -----
module {
  simulation.design @invalid {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 41 in 1 function hierarchy "top.x.init" {internal}
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.x"
    simulation.statement.decl 1 in 41 scope 1 type 38
        simulation.vpi_initializer_binding 41 storage 0
    // expected-error@+1 {{a nonprocedural initializer call cannot name a procedural statement}}
        simulation.vpi_call.decl @call id 20 in 41 scope 1 type 19 name "compute" within 1
  }
}
