set(LLVM_TARGET_DEFINITIONS SimulationEnums.td)
mlir_tablegen(SimulationEnums.h.inc -gen-enum-decls)
mlir_tablegen(SimulationEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(ObeliskSimulationEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationTypes.td)
mlir_tablegen(SimulationTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=obelisk_sim)
mlir_tablegen(SimulationTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=obelisk_sim)
add_public_tablegen_target(ObeliskSimulationTypesIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationOps.td)
mlir_tablegen(SimulationOps.h.inc -gen-op-decls)
mlir_tablegen(SimulationOps.cpp.inc -gen-op-defs)
add_public_tablegen_target(ObeliskSimulationOpsIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationDialect.td)
mlir_tablegen(SimulationDialect.h.inc -gen-dialect-decls
              -dialect=obelisk_sim)
mlir_tablegen(SimulationDialect.cpp.inc -gen-dialect-defs
              -dialect=obelisk_sim)
add_public_tablegen_target(ObeliskSimulationDialectIncGen)
