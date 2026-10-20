file(GLOB runtime_dlls
  "${SDK}/bin/zlib*.dll" "${SDK}/bin/zstd*.dll" "${SDK}/bin/libzstd*.dll"
  "${SDK}/bin/libxml*.dll" "${SDK}/bin/iconv*.dll" "${SDK}/bin/charset*.dll"
  "${SDK}/bin/liblzma*.dll"
  "${SDK}/bin/icu*.dll" "${SDK}/bin/msvcp*.dll" "${SDK}/bin/vcruntime*.dll")
if(NOT runtime_dlls)
  return()
endif()
list(REMOVE_DUPLICATES DIRECTORIES)
foreach(directory IN LISTS DIRECTORIES)
  file(MAKE_DIRECTORY "${directory}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    ${runtime_dlls} "${directory}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
