function(embed_coverage_asset filename symbol output_variable)
  set(input "${CMAKE_CURRENT_SOURCE_DIR}/${filename}")
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${filename}.inc")
  if("${filename}" MATCHES "\\.js$")
    set(html_element script)
  elseif("${filename}" MATCHES "\\.css$")
    set(html_element style)
  else()
    set(html_element "")
  endif()
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=${input}"
      "-DOUTPUT=${output}"
      "-DSYMBOL=${symbol}"
      "-DHTML_ELEMENT=${html_element}"
      -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
    DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
    COMMENT "Embedding ${filename}"
    VERBATIM)
  set_source_files_properties("${output}" PROPERTIES
    GENERATED TRUE HEADER_FILE_ONLY TRUE)
  set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

embed_coverage_asset(CoverageReport.html CoverageReportHtml coverage_report_html)
embed_coverage_asset(CoverageReport.css CoverageReportCss coverage_report_css)
embed_coverage_asset(CoverageReport.js CoverageReportJavaScript coverage_report_js)

add_llvm_executable(obelisk-cov obelisk-cov.cpp
  "${coverage_report_html}"
  "${coverage_report_css}"
  "${coverage_report_js}")
target_include_directories(obelisk-cov PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(obelisk-cov PRIVATE ObeliskCoverage LLVMSupport)
target_compile_features(obelisk-cov PRIVATE cxx_std_17)
set_target_properties(obelisk-cov PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
