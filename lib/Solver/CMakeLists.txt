set(OBELISK_SOLVER_SOURCES
  BooleanDNF.cpp
  RandomProgramSMT.cpp
  SolverHeuristic.cpp
  SolverZ3.cpp
)
set(OBELISK_SOLVER_LIBS MLIRSupport)

if(OBELISK_ENABLE_Z3)
  # add_mlir_library compiles through an internal object target. Define the
  # backend before creating it so both implementation translation units select
  # exactly one analyzeRandomProgram definition.
  add_compile_definitions(OBELISK_ENABLE_Z3=1)
  if(EMSCRIPTEN)
    add_compile_definitions(OBELISK_Z3_SINGLE_THREADED=1)
  endif()
  # Z3's public C++ header predates LLVM's suggest-override policy.
  set_source_files_properties(BooleanDNF.cpp SolverZ3.cpp PROPERTIES
    COMPILE_OPTIONS "-fexceptions;-Wno-suggest-override")
  list(APPEND OBELISK_SOLVER_LIBS MLIRIR MLIRSMT)
endif()

add_mlir_library(ObeliskSolver
  ${OBELISK_SOLVER_SOURCES}

  LINK_LIBS PUBLIC
  ${OBELISK_SOLVER_LIBS}
)

target_include_directories(ObeliskSolver PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

if(OBELISK_ENABLE_Z3)
  # add_mlir_library compiles sources through obj.ObeliskSolver, so order both
  # the object compilation and final archive after the external install. The
  # former is what guarantees z3++.h exists before SolverZ3.cpp is compiled;
  # the latter guarantees the imported archive exists at link time.
  add_dependencies(obj.ObeliskSolver obelisk_z3_project)
  add_dependencies(ObeliskSolver obelisk_z3_project)
  target_link_libraries(ObeliskSolver PRIVATE ObeliskZ3)
endif()
