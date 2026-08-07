set(OBELISK_SOLVER_SOURCES
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
  # Z3's public C++ header predates LLVM's suggest-override policy.
  set_source_files_properties(SolverZ3.cpp PROPERTIES
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
  # The imported target's dependency guarantees the archive exists at link
  # time. This explicit edge also installs the pinned headers before any
  # ObeliskSolver source is compiled, preventing accidental system-Z3 use.
  add_dependencies(ObeliskSolver obelisk_z3_project)
  target_link_libraries(ObeliskSolver PRIVATE ObeliskZ3)
endif()
