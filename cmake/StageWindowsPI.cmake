# Export lists let foreign DLLs import the simulator's standard DPI/VPI API.
# Leave the image name to the caller: llvm-dlltool -D simulator.exe -d ...
execute_process(
  COMMAND "${LLVM_NM}" --defined-only --extern-only --just-symbol-name "${RUNTIME}"
  RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE diagnostic)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "Could not inspect the Windows PI API: ${diagnostic}")
endif()
string(REPLACE "\r" "" symbols "${symbols}")
string(REPLACE "\n" ";" symbols "${symbols}")
list(FILTER symbols INCLUDE REGEX "^(sv[A-Z][A-Za-z0-9_]*|vpi_[A-Za-z0-9_]+)$")
list(REMOVE_DUPLICATES symbols)
list(SORT symbols)
string(JOIN "\n  " exports ${symbols})
file(WRITE "${OUTPUT}" "EXPORTS\n  ${exports}\n")
