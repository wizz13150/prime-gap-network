# Collect the native Windows daemon and its dependency closure for qualification.
# No wallet, configuration or chain directory is an input.
cmake_minimum_required(VERSION 3.22)
foreach(required PGN_CORE_EXECUTABLE PGN_RUNTIME_SEARCH_DIR PGN_OUTPUT_ROOT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing explicit ${required}")
  endif()
endforeach()
if(NOT WIN32 OR NOT EXISTS "${PGN_CORE_EXECUTABLE}")
  message(FATAL_ERROR "An existing native Windows executable is required")
endif()
if(EXISTS "${PGN_OUTPUT_ROOT}")
  message(FATAL_ERROR "Qualification output must be new; nothing overwritten")
endif()
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
find_program(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND NAMES objdump REQUIRED)
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${PGN_CORE_EXECUTABLE}"
  DIRECTORIES "${PGN_RUNTIME_SEARCH_DIR}"
  PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
  POST_EXCLUDE_REGEXES [=[.*[/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\].*]=]
  RESOLVED_DEPENDENCIES_VAR runtime_files
  UNRESOLVED_DEPENDENCIES_VAR missing
  CONFLICTING_DEPENDENCIES_PREFIX conflicts)
if(missing OR conflicts_FILENAMES)
  message(FATAL_ERROR "Incomplete or ambiguous native runtime: ${missing}; ${conflicts_FILENAMES}")
endif()
file(MAKE_DIRECTORY "${PGN_OUTPUT_ROOT}")
file(COPY_FILE "${PGN_CORE_EXECUTABLE}" "${PGN_OUTPUT_ROOT}/pgn-core.exe")
file(COPY ${runtime_files} DESTINATION "${PGN_OUTPUT_ROOT}")
file(GLOB payload "${PGN_OUTPUT_ROOT}/*")
list(SORT payload)
set(checksums "")
foreach(path IN LISTS payload)
  file(SHA256 "${path}" digest)
  get_filename_component(name "${path}" NAME)
  string(APPEND checksums "${digest}  ${name}\n")
endforeach()
file(WRITE "${PGN_OUTPUT_ROOT}/SHA256SUMS" "${checksums}")
message(STATUS "Native executable and complete non-system dependency closure collected")
