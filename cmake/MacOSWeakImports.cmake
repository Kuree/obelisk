# Called during staging and after building the host runtime. Mach-O requires
# explicit permission for optional weak imports; do not allow unresolved
# strong references as a side effect.
foreach(input LLVM_NM RUNTIME OUTPUT_PREFIX)
  if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${input} for macOS weak-import discovery")
  endif()
endforeach()
execute_process(COMMAND "${LLVM_NM}" --undefined-only -m "${RUNTIME}"
  OUTPUT_VARIABLE runtime_symbols COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCHALL "\\(undefined\\) weak external [^ \r\n]+"
  weak_symbols "${runtime_symbols}")
set(weak_imports)
foreach(symbol IN LISTS weak_symbols)
  string(REGEX REPLACE ".* weak external " "" symbol "${symbol}")
  list(APPEND weak_imports "${symbol}")
endforeach()
list(REMOVE_DUPLICATES weak_imports)
list(SORT weak_imports)
set(driver_flags "")
foreach(symbol IN LISTS weak_imports)
  string(APPEND driver_flags "-Xlinker -U -Xlinker ${symbol}\n")
endforeach()
string(JOIN "\n" weak_imports ${weak_imports})
file(WRITE "${OUTPUT_PREFIX}.txt" "${weak_imports}\n")
file(WRITE "${OUTPUT_PREFIX}.rsp" "${driver_flags}")
