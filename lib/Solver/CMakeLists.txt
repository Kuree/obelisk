set(TALOS_SOLVER_SOURCES
  BitVectorProof.cpp
  BooleanDNF.cpp
  RandomProgramSMT.cpp
  RandomSamplerZ3.cpp
  SolverHeuristic.cpp
  SolverZ3.cpp
)
set(TALOS_SOLVER_LIBS MLIRSupport LLVMCore)

if(TALOS_ENABLE_Z3)
  # add_mlir_library compiles through an internal object target. Define the
  # backend before creating it so both implementation translation units select
  # exactly one analyzeRandomProgram definition.
  add_compile_definitions(TALOS_ENABLE_Z3=1)
  if(EMSCRIPTEN)
    add_compile_definitions(TALOS_Z3_SINGLE_THREADED=1)
  endif()
  # Z3's public C++ header predates LLVM's suggest-override policy.
  set_source_files_properties(BitVectorProof.cpp BooleanDNF.cpp SolverZ3.cpp PROPERTIES
    COMPILE_OPTIONS "-fexceptions;-Wno-suggest-override")
  list(APPEND TALOS_SOLVER_LIBS MLIRIR MLIRSMT)
endif()

add_mlir_library(TalosSolver
  ${TALOS_SOLVER_SOURCES}

  LINK_LIBS PUBLIC
  ${TALOS_SOLVER_LIBS}
)

target_include_directories(TalosSolver PRIVATE
  "${PROJECT_SOURCE_DIR}/runtime/include"
)

if(TALOS_ENABLE_Z3)
  # add_mlir_library compiles sources through obj.TalosSolver, so order both
  # the object compilation and final archive after the external install. The
  # former is what guarantees z3++.h exists before SolverZ3.cpp is compiled;
  # the latter guarantees the imported archive exists at link time.
  add_dependencies(obj.TalosSolver talos_z3_project)
  add_dependencies(TalosSolver talos_z3_project)
  target_include_directories(obj.TalosSolver SYSTEM PRIVATE
    "$<TARGET_PROPERTY:TalosZ3,INTERFACE_INCLUDE_DIRECTORIES>")
  target_link_libraries(TalosSolver PRIVATE TalosZ3)
endif()
