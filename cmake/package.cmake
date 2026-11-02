# Post-build script: packages a Release build into a .7z for a mod manager.
# The package holds only what a player installs: F4SE/Plugins/PAPER.dll, its
# PDB (crash loggers read it) and the GPL license. Development files never
# ship: mod authors take include/PAPER from the repository at the release tag,
# and PAPER creates its runtime INI from compiled defaults.

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

file(GLOB_RECURSE STAGED_FILES LIST_DIRECTORIES false RELATIVE "${STAGE_DIR}" "${STAGE_DIR}/*")
set(EXPECTED_FILES F4SE/Plugins/PAPER.dll F4SE/Plugins/PAPER.pdb LICENSE)
list(SORT STAGED_FILES)
list(SORT EXPECTED_FILES)
if(NOT STAGED_FILES STREQUAL EXPECTED_FILES)
  message(FATAL_ERROR "package.cmake: the package must hold only ${EXPECTED_FILES}; staged: ${STAGED_FILES}")
endif()

file(REMOVE "${TARGET_ARCHIVE}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${TARGET_ARCHIVE}" --format=7zip -- .
  WORKING_DIRECTORY "${STAGE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)
message("Packaged '${TARGET_ARCHIVE}'")
