if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "EmbedText.cmake requires INPUT, OUTPUT, and SYMBOL")
endif()

file(READ "${INPUT}" contents)
string(FIND "${contents}" ")OBELISKASSET\"" terminator)
if(NOT terminator EQUAL -1)
  message(FATAL_ERROR "${INPUT} contains the reserved raw-string terminator")
endif()
if(DEFINED HTML_ELEMENT AND NOT HTML_ELEMENT STREQUAL "")
  string(TOLOWER "${contents}" lowercase_contents)
  string(FIND "${lowercase_contents}" "</${HTML_ELEMENT}" closing_element)
  if(NOT closing_element EQUAL -1)
    message(FATAL_ERROR
      "${INPUT} contains a literal closing ${HTML_ELEMENT} element")
  endif()
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
get_filename_component(input_name "${INPUT}" NAME)
file(MAKE_DIRECTORY "${output_dir}")
set(temporary "${OUTPUT}.tmp")
file(WRITE "${temporary}"
  "// Generated from ${input_name}; do not edit.\n"
  "static constexpr llvm::StringLiteral ${SYMBOL} = R\"OBELISKASSET("
  "${contents}"
  ")OBELISKASSET\";\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${temporary}" "${OUTPUT}"
  RESULT_VARIABLE copy_result)
file(REMOVE "${temporary}")
if(NOT copy_result EQUAL 0)
  message(FATAL_ERROR "failed to update ${OUTPUT}")
endif()
