cmake_minimum_required(VERSION 3.14)
project(trophy VERSION 2.0.4 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

# Trophy 2.0.4 ported to ClanLib 4.x (through the cl1compat layer in src/).
#
#   cmake -S . -B build \
#         -DCLANLIB_SOURCE_DIR=/path/to/ClanLib-4.1.0 \
#         -DCLANLIB_BUILD_DIR=/path/to/where/the/clanlib/libs/were/built
#   cmake --build build
#   ./build/trophy
#
# If the automatic library search does not work, pass the libraries yourself:
#   -DCLANLIB_LIBRARIES="/path/libclanApp.a;/path/libclanDisplay.a;..."

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CLANLIB_SOURCE_DIR "" CACHE PATH "ClanLib 4.1.0 source directory (contains Sources/)")
set(CLANLIB_BUILD_DIR  "" CACHE PATH "Directory (searched recursively) containing the built ClanLib libraries")
set(CLANLIB_LIBRARIES  "" CACHE STRING "Explicit list of ClanLib libraries (overrides the search)")

if(NOT CLANLIB_SOURCE_DIR OR NOT EXISTS "${CLANLIB_SOURCE_DIR}/Sources/API/core.h")
    message(FATAL_ERROR "Set -DCLANLIB_SOURCE_DIR=<ClanLib-4.1.0 directory> (it must contain Sources/API/core.h)")
endif()

# ClanLib's headers are included as <ClanLib/...>; in the source tree that
# directory is Sources/API. Make a link with the right name.
set(CLAN_INC "${CMAKE_BINARY_DIR}/clanlib_include")
file(MAKE_DIRECTORY "${CLAN_INC}")
if(NOT EXISTS "${CLAN_INC}/ClanLib")
    file(CREATE_LINK "${CLANLIB_SOURCE_DIR}/Sources/API" "${CLAN_INC}/ClanLib" SYMBOLIC)
endif()

# --- ClanLib libraries -------------------------------------------------------
if(NOT CLANLIB_LIBRARIES)
    if(NOT CLANLIB_BUILD_DIR)
        message(FATAL_ERROR "Set -DCLANLIB_BUILD_DIR=<directory with the built libclan*.a / .dylib> or -DCLANLIB_LIBRARIES=...")
    endif()
    set(_found "")
    foreach(_name clanApp clanGL clanDisplay clanSound clanCore)
        file(GLOB_RECURSE _cands
             "${CLANLIB_BUILD_DIR}/lib${_name}.a" "${CLANLIB_BUILD_DIR}/lib${_name}-*.a"
             "${CLANLIB_BUILD_DIR}/lib${_name}.dylib" "${CLANLIB_BUILD_DIR}/lib${_name}-*.dylib")
        if(NOT _cands)
            message(FATAL_ERROR "Could not find lib${_name} below ${CLANLIB_BUILD_DIR}. Pass -DCLANLIB_LIBRARIES=... instead.")
        endif()
        list(GET _cands 0 _lib)
        message(STATUS "Using ${_lib}")
        list(APPEND _found "${_lib}")
    endforeach()
    set(CLANLIB_LIBRARIES "${_found}")
endif()

# --- Trophy ------------------------------------------------------------------
# Works whether this file is in trophy-2.0.4/ or in trophy-2.0.4/src/
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/catrophy.cpp")
    set(TROPHY_ROOT "${CMAKE_CURRENT_LIST_DIR}")
    set(TROPHY_SRC  "${CMAKE_CURRENT_LIST_DIR}/src")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/catrophy.cpp")
    set(TROPHY_SRC  "${CMAKE_CURRENT_LIST_DIR}")
    get_filename_component(TROPHY_ROOT "${TROPHY_SRC}" DIRECTORY)
else()
    message(FATAL_ERROR "Cannot find catrophy.cpp next to or below CMakeLists.txt")
endif()
if(NOT EXISTS "${TROPHY_ROOT}/resources/resources.xml")
    message(WARNING "${TROPHY_ROOT}/resources/resources.xml not found: the game will not start")
endif()

file(GLOB TROPHY_SOURCES "${TROPHY_SRC}/*.cpp" "${TROPHY_SRC}/utils/*.cpp")
if(APPLE)
    list(APPEND TROPHY_SOURCES "${TROPHY_SRC}/cl1_cocoa.mm")
endif()
# The network code is not used by the game.
list(FILTER TROPHY_SOURCES EXCLUDE REGEX "canet(server|client)\\.cpp$")

add_executable(trophy ${TROPHY_SOURCES})

target_include_directories(trophy PRIVATE
    "${TROPHY_SRC}"
    "${CLAN_INC}"
    "${CLANLIB_SOURCE_DIR}/Sources")

# The game changes into this directory to find resources.xml and tracks/
target_compile_definitions(trophy PRIVATE
    VERSION="${PROJECT_VERSION}"
    PKGDATADIR="${TROPHY_ROOT}/resources"
    GL_SILENCE_DEPRECATION)

target_compile_options(trophy PRIVATE -w)

target_link_libraries(trophy PRIVATE ${CLANLIB_LIBRARIES})
if(APPLE)
    target_link_libraries(trophy PRIVATE
        "-framework Cocoa" "-framework OpenGL" "-framework CoreAudio"
        "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation"
        "-framework IOKit" "-framework QuartzCore" z)
endif()
