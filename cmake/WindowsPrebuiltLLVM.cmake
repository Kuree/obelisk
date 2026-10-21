# The official Windows LLVM archive lacks MLIR. Install a checksum-pinned,
# prebuilt SDK locally; no LLVM/MLIR source build or global conda is involved.
if(_obelisk_host_processor MATCHES "^(aarch64|arm64|ARM64)$")
  set(_obelisk_windows_platform "win-arm64")
  set(_obelisk_windows_lock "${CMAKE_CURRENT_LIST_DIR}/windows-arm64-sdk.lock")
  set(_obelisk_mamba_sha256
    "2a2f41f6bc3e385899d076794f3f9e664fa264aceb08b0a2eb5380eae7411761")
else()
  set(_obelisk_windows_platform "win-64")
  set(_obelisk_windows_lock "${CMAKE_CURRENT_LIST_DIR}/windows-sdk.lock")
  set(_obelisk_mamba_sha256
    "97a336f4ab794bd96a6a4da5e6ed63e75a1d31830414a182419b23d3b36f3fe0")
endif()
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
    "https://conda.anaconda.org/conda-forge/${_obelisk_windows_platform}/micromamba-2.9.0-0.tar.bz2"
    "${_obelisk_mamba_archive}"
    EXPECTED_HASH "SHA256=${_obelisk_mamba_sha256}"
    TLS_VERIFY ON SHOW_PROGRESS)
  file(ARCHIVE_EXTRACT INPUT "${_obelisk_mamba_archive}"
    DESTINATION "${_obelisk_mamba_dir}"
    PATTERNS "Library/bin/micromamba.exe")
  execute_process(
    COMMAND "${_obelisk_mamba_dir}/Library/bin/micromamba.exe" create
      --no-rc --root-prefix "${CMAKE_BINARY_DIR}/mamba-root"
      --prefix "${_obelisk_windows_prefix}" --file "${_obelisk_windows_lock}"
      --platform "${_obelisk_windows_platform}"
      --yes
    RESULT_VARIABLE _obelisk_mamba_result)
  if(NOT _obelisk_mamba_result EQUAL 0)
    message(FATAL_ERROR "Could not install the prebuilt Windows LLVM SDK")
  endif()
  file(WRITE "${_obelisk_windows_stamp}" "${_obelisk_windows_lock_hash}\n")
endif()
set(OBELISK_LLVM_DIST_DIR "${_obelisk_windows_prefix}/Library")
