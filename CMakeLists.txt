cmake_minimum_required(VERSION 4.2)

set(NAME "PAPER")
set(FRIENDLY_NAME "PAPER")
set(VERSION 0.5.0)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
  if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
    set(${VARIABLE} $ENV{${VARIABLE}})
  endif()
endmacro()

set_from_environment(VCPKG_ROOT)
if(DEFINED VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
  set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
else()
  message(FATAL_ERROR "VCPKG_ROOT is not set")
endif()

if(DEFINED COMMON_LIB_F4VR_PATH AND NOT COMMON_LIB_F4VR_PATH STREQUAL "")
  set(COMMON_LIB_F4VR_PATH_SELECTED "${COMMON_LIB_F4VR_PATH}" CACHE PATH "" FORCE)
else()
  set(COMMON_LIB_F4VR_PATH_SELECTED "${WORKSPACE_ROOT}/libraries_and_tools/CommonLibF4VR" CACHE PATH "" FORCE)
endif()
if(NOT EXISTS "${COMMON_LIB_F4VR_PATH_SELECTED}/CommonLibF4/include/REL/Relocation.h")
  message(FATAL_ERROR "CommonLibF4VR path not found in '${COMMON_LIB_F4VR_PATH_SELECTED}'")
endif()

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_F4 OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR ON CACHE BOOL "" FORCE)

add_subdirectory("${COMMON_LIB_F4VR_PATH_SELECTED}" "CommonLibF4" EXCLUDE_FROM_ALL)

configure_file("${ROOT_DIR}/cmake/version.rc.in" "${BUILD_DIR}/version.rc" @ONLY)
configure_file("${ROOT_DIR}/cmake/Version.h.in" "${BUILD_DIR}/include/Version.h" @ONLY)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${SOURCE_DIR}/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE inls CONFIGURE_DEPENDS "${SOURCE_DIR}/*.inl")
source_group(TREE "${ROOT_DIR}" FILES ${headers} ${sources} ${inls})

add_library(${PROJECT_NAME} SHARED
  ${headers}
  ${sources}
  ${inls}
  "${BUILD_DIR}/include/Version.h"
  "${BUILD_DIR}/version.rc"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  PAPER_API_EXPORTS
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  NOMMNOSOUND
)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${BUILD_DIR}/include"
  "${SOURCE_DIR}"
)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
  CommonLibF4::CommonLibF4
  spdlog::spdlog
  shell32.lib
  version.lib
)

target_precompile_headers(${PROJECT_NAME} PRIVATE "src/PCH.h")
target_compile_options(${PROJECT_NAME} PRIVATE
  /sdl
  /utf-8
  /Zi
  /permissive-
  /Zc:__cplusplus
  /Zc:preprocessor
  /W4
  /wd4715
  "$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
)
target_link_options(${PROJECT_NAME} PRIVATE
  "/DEF:${SOURCE_DIR}/exports.def"
  "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
  "$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
)
string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

find_program(PAPER_PWSH_EXECUTABLE NAMES pwsh REQUIRED)
add_custom_target(PAPERSourceBoundary
  COMMAND "${PAPER_PWSH_EXECUTABLE}" -NoProfile -File
    "${ROOT_DIR}/tests/PaperSourceTests.ps1"
    -Root "${ROOT_DIR}"
  COMMENT "Validating PAPER source boundaries"
  VERBATIM
)
add_dependencies(${PROJECT_NAME} PAPERSourceBoundary)

option(POST_BUILD_COPY_PLUGIN "Copy only PAPER.dll and PAPER.pdb to a mod root." OFF)
set(COPY_PLUGIN_BASE_PATH "" CACHE STRING "Semicolon-separated mod roots containing F4SE/Plugins.")
if(POST_BUILD_COPY_PLUGIN)
  if(COPY_PLUGIN_BASE_PATH STREQUAL "")
    message(FATAL_ERROR "COPY_PLUGIN_BASE_PATH is required when POST_BUILD_COPY_PLUGIN is enabled")
  endif()
  foreach(copy_path IN LISTS COPY_PLUGIN_BASE_PATH)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${copy_path}/F4SE/Plugins"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${copy_path}/F4SE/Plugins/"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${copy_path}/F4SE/Plugins/"
      COMMENT "Deploying ${PROJECT_NAME} to '${copy_path}'"
    )
  endforeach()
endif()

option(BUILD_PAPER_TESTS "Build focused PAPER policy tests." OFF)
if(BUILD_PAPER_TESTS)
  enable_testing()
  add_executable(PAPERPolicyTests "${ROOT_DIR}/tests/PaperPolicyTests.cpp")
  target_compile_features(PAPERPolicyTests PRIVATE cxx_std_23)
  target_include_directories(PAPERPolicyTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME PAPERPolicyTests COMMAND "$<TARGET_FILE:PAPERPolicyTests>")

  add_executable(PAPERObservationApiTests
    "${ROOT_DIR}/tests/PAPERObservationApiTests.cpp"
  )
  target_compile_features(PAPERObservationApiTests PRIVATE cxx_std_23)
  target_compile_options(PAPERObservationApiTests PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
  )
  target_include_directories(PAPERObservationApiTests PRIVATE "${SOURCE_DIR}")
  add_test(
    NAME PAPERObservationApiTests
    COMMAND "$<TARGET_FILE:PAPERObservationApiTests>"
  )

  add_executable(PAPERConfigFileTests
    "${ROOT_DIR}/tests/PaperConfigFileTests.cpp"
    "${SOURCE_DIR}/PaperConfigFile.cpp"
    "${SOURCE_DIR}/PaperConfigDefaults.cpp"
  )
  target_compile_features(PAPERConfigFileTests PRIVATE cxx_std_23)
  target_compile_definitions(PAPERConfigFileTests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_compile_options(PAPERConfigFileTests PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
  )
  target_include_directories(PAPERConfigFileTests PRIVATE
    "${BUILD_DIR}/include"
    "${SOURCE_DIR}"
  )
  add_test(NAME PAPERConfigFileTests COMMAND "$<TARGET_FILE:PAPERConfigFileTests>"
    "${ROOT_DIR}/data/config/PAPER_example.ini")

  add_executable(PAPERWeaponMotionCacheTests
    "${ROOT_DIR}/tests/PAPERWeaponMotionCacheTests.cpp"
    "${SOURCE_DIR}/weapon_motion/WeaponMotionCache.cpp"
  )
  target_compile_features(PAPERWeaponMotionCacheTests PRIVATE cxx_std_23)
  target_compile_definitions(PAPERWeaponMotionCacheTests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_compile_options(PAPERWeaponMotionCacheTests PRIVATE
    /sdl
    /utf-8
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /W4
  )
  target_include_directories(PAPERWeaponMotionCacheTests PRIVATE "${SOURCE_DIR}")
  add_test(
    NAME PAPERWeaponMotionCacheTests
    COMMAND "$<TARGET_FILE:PAPERWeaponMotionCacheTests>"
  )

  add_test(
    NAME PAPERSourceTests
    COMMAND "${PAPER_PWSH_EXECUTABLE}" -NoProfile -File
      "${ROOT_DIR}/tests/PaperSourceTests.ps1"
      -Root "${ROOT_DIR}"
  )
endif()
