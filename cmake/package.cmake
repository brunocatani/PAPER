# Post-build script: packages a Release build into a .7z for a mod manager.
# Layout: F4SE/Plugins/PAPER.dll and .pdb, LICENSE, and include/PAPER (the
# public provider API headers, which ship next to the DLL). INIs never ship:
# PAPER creates its runtime INI from compiled defaults.

string(TOLOWER "${CONFIG}" CONFIG_LOWER)
if(NOT CONFIG_LOWER STREQUAL "release")
  message("Skipping packaging: not a Release build: '${CONFIG}'")
  return()
endif()

foreach(REQUIRED IN ITEMS PROJECT_FRIENDLY_NAME PROJECT_VERSION ROOT_DIR PACKAGE_DIR TARGET_FILE TARGET_PDB_FILE)
  if(NOT DEFINED ${REQUIRED} OR "${${REQUIRED}}" STREQUAL "")
    message(FATAL_ERROR "package.cmake: ${REQUIRED} is not set")
  endif()
endforeach()

set(STAGE_DIR "${PACKAGE_DIR}/staging")
string(TIMESTAMP PACKAGE_DATE "%Y%m%d")
set(TARGET_ARCHIVE "${PACKAGE_DIR}/${PROJECT_FRIENDLY_NAME} - v${PROJECT_VERSION} - ${PACKAGE_DATE}.7z")

message("Packaging release build into '${TARGET_ARCHIVE}'")
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${STAGE_DIR}/F4SE/Plugins")
file(COPY "${TARGET_FILE}" "${TARGET_PDB_FILE}" DESTINATION "${STAGE_DIR}/F4SE/Plugins")
file(COPY "${ROOT_DIR}/LICENSE" DESTINATION "${STAGE_DIR}")
file(COPY "${ROOT_DIR}/include/PAPER" DESTINATION "${STAGE_DIR}/include")

file(GLOB_RECURSE STAGED_INIS LIST_DIRECTORIES false "${STAGE_DIR}/*.ini")
if(STAGED_INIS)
  message(FATAL_ERROR "package.cmake: an INI cannot ship in the package: ${STAGED_INIS}")
endif()

file(REMOVE "${TARGET_ARCHIVE}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${TARGET_ARCHIVE}" --format=7zip -- .
  WORKING_DIRECTORY "${STAGE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
message("Packaged '${TARGET_ARCHIVE}'")
