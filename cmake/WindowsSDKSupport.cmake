# Some prebuilt libxml2 import libraries incorrectly expose DllMain. A client
# executable must use its own CRT entry point, never the DLL's private one.
# Regenerate only that import index with the matching SDK tools; the downloaded
# library and DLL stay untouched, and every other imported symbol is retained.
if(NOT TARGET LibXml2::LibXml2)
  return()
endif()
get_target_property(_obelisk_xml_library LibXml2::LibXml2 IMPORTED_LOCATION)
if(NOT _obelisk_xml_library OR NOT EXISTS "${_obelisk_xml_library}")
  return()
endif()
execute_process(
  COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-nm.exe"
    --defined-only --extern-only --just-symbol-name "${_obelisk_xml_library}"
  RESULT_VARIABLE _obelisk_xml_status OUTPUT_VARIABLE _obelisk_xml_symbols
  ERROR_VARIABLE _obelisk_xml_diagnostic)
if(NOT _obelisk_xml_status EQUAL 0)
  message(FATAL_ERROR "Could not inspect the SDK's libxml2: ${_obelisk_xml_diagnostic}")
endif()
string(REPLACE "\r" "" _obelisk_xml_symbols "${_obelisk_xml_symbols}")
string(REPLACE "\n" ";" _obelisk_xml_symbols "${_obelisk_xml_symbols}")
if(NOT "__imp_DllMain" IN_LIST _obelisk_xml_symbols)
  return()
endif()
set(_obelisk_xml_support "${CMAKE_BINARY_DIR}/windows-sdk-support")
set(_obelisk_xml_clean_library "${_obelisk_xml_support}/libxml2.lib")
file(SHA256 "${_obelisk_xml_library}" _obelisk_xml_hash)
string(APPEND _obelisk_xml_hash ":${OBELISK_WINDOWS_HOST_DLLTOOL_MACHINE}")
set(_obelisk_xml_saved_hash "")
if(EXISTS "${_obelisk_xml_support}/libxml2.sha256")
  file(READ "${_obelisk_xml_support}/libxml2.sha256" _obelisk_xml_saved_hash)
endif()
if(NOT _obelisk_xml_saved_hash STREQUAL _obelisk_xml_hash OR
   NOT EXISTS "${_obelisk_xml_clean_library}")
  file(MAKE_DIRECTORY "${_obelisk_xml_support}")
  set(_obelisk_xml_exports "EXPORTS\n")
  foreach(symbol IN LISTS _obelisk_xml_symbols)
    if(NOT symbol MATCHES "^__imp_(.+)$")
      continue()
    endif()
    set(name "${CMAKE_MATCH_1}")
    if(name STREQUAL "DllMain")
      continue()
    endif()
    if(name IN_LIST _obelisk_xml_symbols)
      string(APPEND _obelisk_xml_exports "  ${name}\n")
    else()
      string(APPEND _obelisk_xml_exports "  ${name} DATA\n")
    endif()
  endforeach()
  file(WRITE "${_obelisk_xml_support}/libxml2.def" "${_obelisk_xml_exports}")
  execute_process(
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-dlltool.exe" -I "${_obelisk_xml_library}"
    RESULT_VARIABLE _obelisk_xml_status OUTPUT_VARIABLE _obelisk_xml_dll
    ERROR_VARIABLE _obelisk_xml_diagnostic OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _obelisk_xml_status EQUAL 0 OR NOT _obelisk_xml_dll)
    message(FATAL_ERROR "Could not identify the SDK's libxml2 DLL: ${_obelisk_xml_diagnostic}")
  endif()
  execute_process(
    COMMAND "${OBELISK_LLVM_TOOLS_DIR}/llvm-dlltool.exe"
      -m "${OBELISK_WINDOWS_HOST_DLLTOOL_MACHINE}"
      -D "${_obelisk_xml_dll}" -d "${_obelisk_xml_support}/libxml2.def"
      -l "${_obelisk_xml_clean_library}"
    RESULT_VARIABLE _obelisk_xml_status ERROR_VARIABLE _obelisk_xml_diagnostic)
  if(NOT _obelisk_xml_status EQUAL 0)
    message(FATAL_ERROR "Could not regenerate libxml2 imports: ${_obelisk_xml_diagnostic}")
  endif()
  file(WRITE "${_obelisk_xml_support}/libxml2.sha256" "${_obelisk_xml_hash}")
endif()
set_target_properties(LibXml2::LibXml2 PROPERTIES
  IMPORTED_LOCATION "${_obelisk_xml_clean_library}")
