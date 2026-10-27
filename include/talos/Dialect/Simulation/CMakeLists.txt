set(LLVM_TARGET_DEFINITIONS SimulationEnums.td)
mlir_tablegen(SimulationEnums.h.inc -gen-enum-decls)
mlir_tablegen(SimulationEnums.cpp.inc -gen-enum-defs)
add_public_tablegen_target(TalosSimulationEnumsIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationAttrs.td)
mlir_tablegen(SimulationAttrs.h.inc -gen-attrdef-decls
              -attrdefs-dialect=simulation)
mlir_tablegen(SimulationAttrs.cpp.inc -gen-attrdef-defs
              -attrdefs-dialect=simulation)
add_public_tablegen_target(TalosSimulationAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationTypes.td)
mlir_tablegen(SimulationTypes.h.inc -gen-typedef-decls
              -typedefs-dialect=simulation)
mlir_tablegen(SimulationTypes.cpp.inc -gen-typedef-defs
              -typedefs-dialect=simulation)
add_public_tablegen_target(TalosSimulationTypesIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationOps.td)
# Sharding emits the op definitions in slices that compile as parallel
# translation units, plus the registerTalosSimulationDialectOperations hook the
# dialect calls instead of expanding a GET_OP_LIST.
mlir_tablegen(SimulationOps.h.inc -gen-op-decls
              --op-shard-count=${TALOS_OP_SHARD_COUNT})
mlir_tablegen(SimulationOpDefs.cpp.inc -gen-op-defs
              --op-shard-count=${TALOS_OP_SHARD_COUNT})
add_public_tablegen_target(TalosSimulationOpsIncGen)
add_dependencies(TalosSimulationOpsIncGen TalosScheduleAttrsIncGen
  TalosScheduleFieldsIncGen)

set(LLVM_TARGET_DEFINITIONS SimulationDialect.td)
mlir_tablegen(SimulationDialect.h.inc -gen-dialect-decls
              -dialect=simulation)
mlir_tablegen(SimulationDialect.cpp.inc -gen-dialect-defs
              -dialect=simulation)
add_public_tablegen_target(TalosSimulationDialectIncGen)

add_subdirectory(Transforms)
