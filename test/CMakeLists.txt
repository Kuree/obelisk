find_program(OBELISK_LIT_EXECUTABLE NAMES llvm-lit lit)
if(NOT OBELISK_LIT_EXECUTABLE)
  set(_obelisk_lit_fallback "${PROJECT_SOURCE_DIR}/env/bin/lit")
  if(EXISTS "${_obelisk_lit_fallback}")
    set(OBELISK_LIT_EXECUTABLE "${_obelisk_lit_fallback}")
  else()
    message(FATAL_ERROR
      "LLVM lit was not found on PATH and the fallback does not exist at "
      "${_obelisk_lit_fallback}")
  endif()
endif()

set(LLVM_EXTERNAL_LIT "${OBELISK_LIT_EXECUTABLE}"
    CACHE FILEPATH "LLVM lit runner used by Obelisk tests" FORCE)
message(STATUS "Using lit: ${LLVM_EXTERNAL_LIT}")

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

add_lit_testsuite(check-obelisk "Running the Obelisk regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
  obelisk
  obelisk-opt
  obelisk-translate
)
set_target_properties(check-obelisk PROPERTIES FOLDER "Tests")
