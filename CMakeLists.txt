cmake_minimum_required(VERSION 4.2)

set(NAME "PAPER")
set(FRIENDLY_NAME "PAPER")
set(VERSION 2.0.0)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SOURCE_DIR "${ROOT_DIR}/src")
set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
get_filename_component(WORKSPACE_ROOT "${ROOT_DIR}/../.." ABSOLUTE)
# ROCK publishes its provider API from its own include/ directory.
set(ROCK_INCLUDE_DIR "${WORKSPACE_ROOT}/main_projects/ROCK/include" CACHE PATH "ROCK public include directory")
if(NOT EXISTS "${ROCK_INCLUDE_DIR}/ROCK/Client.h")
  message(FATAL_ERROR "ROCK headers not found in '${ROCK_INCLUDE_DIR}'; set ROCK_INCLUDE_DIR.")
endif()
# RPS UI Framework draws PAPER's optional debug visualization.
set(RPSUI_INCLUDE_DIR "${WORKSPACE_ROOT}/../ui_mods/RPS_UI_Framework/SDK/include" CACHE PATH "RPS UI Framework public include directory")
if(NOT EXISTS "${RPSUI_INCLUDE_DIR}/RPSUIWorldDrawApi.h")
  message(FATAL_ERROR "RPS UI Framework headers not found in '${RPSUI_INCLUDE_DIR}'; set RPSUI_INCLUDE_DIR.")
endif()
# PAPER's own provider API: self-contained headers PAPER compiles against.
set(PAPER_PUBLIC_INCLUDE_DIR "${ROOT_DIR}/include")
include_directories("${PAPER_PUBLIC_INCLUDE_DIR}" "${ROCK_INCLUDE_DIR}" "${RPSUI_INCLUDE_DIR}")

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
file(GLOB PAPER_PUBLIC_HEADERS CONFIGURE_DEPENDS "${PAPER_PUBLIC_INCLUDE_DIR}/PAPER/*.h")
source_group(TREE "${ROOT_DIR}" FILES ${headers} ${sources} ${inls} ${PAPER_PUBLIC_HEADERS})

add_library(${PROJECT_NAME} SHARED
  ${PAPER_PUBLIC_HEADERS}
  ${headers}
  ${sources}
  ${inls}
  "${BUILD_DIR}/include/Version.h"
  "${BUILD_DIR}/version.rc"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE
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

# Release packages are an explicit path (the release preset); the fast preset
# never builds an archive. The package carries only runtime files; see
# cmake/package.cmake.
option(PAPER_PACKAGE_RELEASE "Package Release builds into a .7z archive after PAPER is built." OFF)
if(PAPER_PACKAGE_RELEASE)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DPROJECT_FRIENDLY_NAME=${FRIENDLY_NAME}"
      "-DPROJECT_VERSION=${PROJECT_VERSION}"
      "-DROOT_DIR=${ROOT_DIR}"
      "-DPACKAGE_DIR=${BUILD_DIR}/package"
      "-DTARGET_FILE=$<TARGET_FILE:${PROJECT_NAME}>"
      "-DTARGET_PDB_FILE=$<TARGET_PDB_FILE:${PROJECT_NAME}>"
      "-DCONFIG=$<CONFIG>"
      -P "${ROOT_DIR}/cmake/package.cmake"
    COMMENT "Packaging ${PROJECT_NAME} release archive"
  )
endif()

option(BUILD_PAPER_TESTS "Build focused PAPER policy tests." OFF)
if(BUILD_PAPER_TESTS)
  enable_testing()

  # Public headers: each compiles alone inside a packed caller with only the
  # public include directory (their layout asserts run there), and the one
  # consumer example builds.
  set(PAPER_PUBLIC_HEADER_SOURCES "${BUILD_DIR}/public-header-tests/Main.cpp")
  file(CONFIGURE OUTPUT "${BUILD_DIR}/public-header-tests/Main.cpp" CONTENT "int main() { return 0; }\n")
  foreach(PAPER_PUBLIC_HEADER IN LISTS PAPER_PUBLIC_HEADERS)
    get_filename_component(PAPER_PUBLIC_HEADER_NAME "${PAPER_PUBLIC_HEADER}" NAME_WE)
    set(PAPER_PUBLIC_HEADER_SOURCE "${BUILD_DIR}/public-header-tests/${PAPER_PUBLIC_HEADER_NAME}.cpp")
    file(CONFIGURE OUTPUT "${PAPER_PUBLIC_HEADER_SOURCE}"
      CONTENT "#pragma pack(push, 1)\n#include <PAPER/${PAPER_PUBLIC_HEADER_NAME}.h>\n#pragma pack(pop)\n")
    list(APPEND PAPER_PUBLIC_HEADER_SOURCES "${PAPER_PUBLIC_HEADER_SOURCE}")
  endforeach()
  add_executable(PAPERPublicHeaderTests ${PAPER_PUBLIC_HEADER_SOURCES} "${ROOT_DIR}/tests/ConsumerExample.cpp")
  set_target_properties(PAPERPublicHeaderTests PROPERTIES INCLUDE_DIRECTORIES "${PAPER_PUBLIC_INCLUDE_DIR}")
  target_compile_features(PAPERPublicHeaderTests PRIVATE cxx_std_20)
  target_compile_options(PAPERPublicHeaderTests PRIVATE /W4 /permissive-)
  add_test(NAME PAPERPublicHeaderTests COMMAND "$<TARGET_FILE:PAPERPublicHeaderTests>")
  add_executable(PAPERDiscoveryTests "${ROOT_DIR}/tests/DiscoveryTests.cpp")
  target_compile_features(PAPERDiscoveryTests PRIVATE cxx_std_23)
  target_compile_options(PAPERDiscoveryTests PRIVATE /UNDEBUG)
  target_include_directories(PAPERDiscoveryTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME PAPERDiscoveryTests COMMAND "$<TARGET_FILE:PAPERDiscoveryTests>")

  add_executable(PAPERPhysicalReloadTests "${ROOT_DIR}/tests/PhysicalReloadLeaseTests.cpp")
  target_compile_features(PAPERPhysicalReloadTests PRIVATE cxx_std_23)
  target_compile_options(PAPERPhysicalReloadTests PRIVATE /UNDEBUG)
  target_include_directories(PAPERPhysicalReloadTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME PAPERPhysicalReloadTests COMMAND "$<TARGET_FILE:PAPERPhysicalReloadTests>")
  add_executable(PAPERAttachmentBindingTests "${ROOT_DIR}/tests/AttachmentBindingTests.cpp")
  target_compile_features(PAPERAttachmentBindingTests PRIVATE cxx_std_23)
  target_include_directories(PAPERAttachmentBindingTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME PAPERAttachmentBindingTests COMMAND "$<TARGET_FILE:PAPERAttachmentBindingTests>")
  add_executable(PAPERMergedReloadTests "${ROOT_DIR}/tests/MergedReloadPolicyTests.cpp")
  target_compile_features(PAPERMergedReloadTests PRIVATE cxx_std_23)
  target_include_directories(PAPERMergedReloadTests PRIVATE "${SOURCE_DIR}")
  add_test(NAME PAPERMergedReloadTests COMMAND "$<TARGET_FILE:PAPERMergedReloadTests>")

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
endif()

# Tests share build-fast but stay out of the default build, so building and deploying the
# plugin never compiles them. Build them with `--target build_tests`, then run ctest.
function(fo4dev_collect_targets directory result)
  get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(child IN LISTS children)
    cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${child}" NORMALIZE child_is_ours)
    if(child_is_ours)
      fo4dev_collect_targets("${child}" child_targets)
      list(APPEND targets ${child_targets})
    endif()
  endforeach()
  set(${result} ${targets} PARENT_SCOPE)
endfunction()
fo4dev_collect_targets("${CMAKE_SOURCE_DIR}" fo4dev_targets)
add_custom_target(build_tests)
foreach(fo4dev_target IN LISTS fo4dev_targets)
  get_target_property(fo4dev_target_type ${fo4dev_target} TYPE)
  string(TOLOWER "${fo4dev_target}" fo4dev_target_name)
  if(fo4dev_target_name MATCHES "test" AND fo4dev_target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
    set_target_properties(${fo4dev_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    if(fo4dev_target_type STREQUAL "EXECUTABLE")
      add_dependencies(build_tests ${fo4dev_target})
    endif()
  endif()
endforeach()
