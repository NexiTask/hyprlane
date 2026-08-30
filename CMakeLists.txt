cmake_minimum_required(VERSION 3.25)

project(
    hyprlane
    VERSION 1.0.0
    DESCRIPTION "Hyprland layout similar to PaperWM and niri"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HYPRLANE_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)

function(hyprlane_enable_warnings target)
    target_compile_options(
        ${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
    if(HYPRLANE_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
    endif()
endfunction()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED IMPORTED_TARGET hyprland)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET hyprutils hyprlang pixman-1 libdrm pangocairo)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua>=5.5)

set(
    HYPRLANE_SOURCES
    src/column.cpp
    src/config.cpp
    src/decorations.cpp
    src/dispatchers.cpp
    src/enums.cpp
    src/functions.cpp
    src/main.cpp
    src/overview.cpp
    src/row.cpp
    src/scroller.cpp
    src/sizes.cpp
    src/window_rule_effects.cpp
    src/window.cpp
    src/workspace_config.cpp
)

add_library(hyprlane SHARED ${HYPRLANE_SOURCES})
target_compile_features(hyprlane PRIVATE cxx_std_23)
target_compile_definitions(hyprlane PRIVATE WLR_USE_UNSTABLE)
target_compile_options(hyprlane PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fno-gnu-unique>)
target_link_libraries(hyprlane PRIVATE PkgConfig::HYPRLAND PkgConfig::DEPS PkgConfig::LUA)
set_target_properties(
    hyprlane
    PROPERTIES
        CXX_EXTENSIONS OFF
        PREFIX ""
)
hyprlane_enable_warnings(hyprlane)

install(TARGETS hyprlane LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(BUILD_TESTING)
    add_executable(
        hyprlane_unit_tests
        tests/unit_tests.cpp
        src/workspace_config.cpp
    )
    target_compile_features(hyprlane_unit_tests PRIVATE cxx_std_23)
    target_include_directories(hyprlane_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(hyprlane_unit_tests PROPERTIES CXX_EXTENSIONS OFF)
    hyprlane_enable_warnings(hyprlane_unit_tests)
    add_test(NAME hyprlane.unit COMMAND hyprlane_unit_tests)
endif()
