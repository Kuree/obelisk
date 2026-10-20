# The official Windows LLVM archive lacks MLIR. Install a checksum-pinned,
# prebuilt SDK locally; no LLVM/MLIR source build or global conda is involved.
set(_obelisk_windows_lock "${CMAKE_CURRENT_LIST_DIR}/windows-sdk.lock")
file(SHA256 "${_obelisk_windows_lock}" _obelisk_windows_lock_hash)
set(_obelisk_windows_prefix "${CMAKE_BINARY_DIR}/sdk")
set(_obelisk_windows_stamp "${_obelisk_windows_prefix}/.obelisk-sdk-lock")
set(_obelisk_windows_installed_hash "")
if(EXISTS "${_obelisk_windows_stamp}")
  file(READ "${_obelisk_windows_stamp}" _obelisk_windows_installed_hash)
  string(STRIP "${_obelisk_windows_installed_hash}" _obelisk_windows_installed_hash)
endif()
if(NOT _obelisk_windows_installed_hash STREQUAL _obelisk_windows_lock_hash)
  if(EXISTS "${_obelisk_windows_prefix}/conda-meta/history")
    message(FATAL_ERROR
      "The Windows SDK was installed with a different package lock. Use a "
      "new build directory, or set OBELISK_LLVM_INSTALL_DIR to an existing SDK.")
  endif()
  set(_obelisk_mamba_archive "${CMAKE_BINARY_DIR}/_downloads/micromamba.tar.bz2")
  set(_obelisk_mamba_dir "${CMAKE_BINARY_DIR}/micromamba")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_downloads" "${_obelisk_mamba_dir}")
  file(DOWNLOAD
    "https://conda.anaconda.org/conda-forge/win-64/micromamba-2.9.0-0.tar.bz2"
    "${_obelisk_mamba_archive}"
    EXPECTED_HASH "SHA256=97a336f4ab794bd96a6a4da5e6ed63e75a1d31830414a182419b23d3b36f3fe0"
    TLS_VERIFY ON SHOW_PROGRESS)
  file(ARCHIVE_EXTRACT INPUT "${_obelisk_mamba_archive}"
    DESTINATION "${_obelisk_mamba_dir}"
    PATTERNS "Library/bin/micromamba.exe")
  execute_process(
    COMMAND "${_obelisk_mamba_dir}/Library/bin/micromamba.exe" create
      --no-rc --root-prefix "${CMAKE_BINARY_DIR}/mamba-root"
      --prefix "${_obelisk_windows_prefix}" --file "${_obelisk_windows_lock}"
      --yes
    RESULT_VARIABLE _obelisk_mamba_result)
  if(NOT _obelisk_mamba_result EQUAL 0)
    message(FATAL_ERROR "Could not install the prebuilt Windows LLVM SDK")
  endif()
  file(WRITE "${_obelisk_windows_stamp}" "${_obelisk_windows_lock_hash}\n")
endif()
set(OBELISK_LLVM_DIST_DIR "${_obelisk_windows_prefix}/Library")
