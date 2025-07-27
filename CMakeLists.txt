cmake_minimum_required(VERSION 3.13..3.24)
project(mangetsu LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "core")

if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(SEND_ERROR "In-source builds are not allowed.")
endif ()
#
# Specify whether to build the GUI or not
#
option(BUILD_GUI "Build GUI programs [default: OFF]" OFF)
#
# Windows builds are statically linked to external libraries by default.
#
if(WIN32)
    option(LINK_STATIC_EXES "Link executables statically [default: ON]" ON)
else()
    option(LINK_STATIC_EXES "Link executables statically [default: OFF]" OFF)
endif()
#
# Internal libraries for CLI programs are built statically by default;
# this is overridable by supplying this option, but needs further testing.
# Note, libraries used by GUI programs are *always* built statically.
#
option(BUILD_SHARED_LIBS "Build shared libraries [default: OFF]" OFF)
#
# Set default target to "Release".
# src: https://www.kitware.com/cmake-and-the-default-build-type/
#
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS
    "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
#
# GCC Compiler options for...
#
# ...all targets
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas
    -Werror -Wno-error=unused-variable -Wno-error=unused-but-set-variable
    -Wno-error=pragmas -Wno-error=unused-local-typedefs)
# ...debug only
if(CMAKE_BUILD_TYPE MATCHES "^([D|d][E|e][B|b][U|u][G|g])$")
  add_compile_options(-gdwarf-4 -g3 -g -O0 -no-pie -fno-omit-frame-pointer)
  if(NOT WIN32) # possibly too coarse a guard
     add_compile_options("-Wl,--export-dynamic")     # ELF executables
  else()
     add_compile_options("-Wl,--export-all-symbols") # PE executables
  endif()
endif()
# ...release only (probably doesn't hurt for MinSizeRel and RelWithDebInfo too)
if(CMAKE_BUILD_TYPE MATCHES "([R|r][E|e][L|l]|[E|e][A|a][S|s][E|e])")
  add_compile_options(-fpie)
endif()
# ...mingw64 (guarded to prevent evaluation on Linux or non-mingw Windows)
if(MINGW)
    if($ENV{MSYSTEM} MATCHES "MINGW64|UCRT64")
      add_compile_definitions(_FILE_OFFSET_BITS=64)
    endif()
endif()
#
# Linker options for static and shared builds.
#
if(LINK_STATIC_EXES)
  if(MINGW)
    add_link_options(-static)
  else()
    add_link_options(-static-pie)
  endif()
else()
  add_link_options(-pie)
endif()

# Set large file support options for both platforms
if(WIN32)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
else()
    add_compile_options(-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
endif()
#
find_path(IMAGEQUANT_INCLUDE_DIRS libimagequant.h)
find_library(IMAGEQUANT_LIBRARIES NAMES imagequant)
if (IMAGEQUANT_INCLUDE_DIRS AND IMAGEQUANT_LIBRARIES)
    include_directories(${IMAGEQUANT_INCLUDE_DIR})
    set(IMAGEQUANT_FOUND TRUE)
else()
    message(FATAL_ERROR "libimagequant not found.")
endif()
#
if(LINK_STATIC_EXES)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib")
endif()
find_package(PNG REQUIRED)
#
message(STATUS "PNG_INCLUDE_DIRS: ${PNG_INCLUDE_DIRS}")
message(STATUS "PNG_LIBRARIES: ${PNG_LIBRARIES}")
message(STATUS "IMAGEQUANT_INCLUDE_DIRS: ${IMAGEQUANT_INCLUDE_DIRS}")
message(STATUS "IMAGEQUANT_LIBRARIES: ${IMAGEQUANT_LIBRARIES}")
#
# Add libraries
#
add_library(mg_util
  src/util/fs.cpp
)
set_target_properties(mg_util
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)
target_include_directories(mg_util PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

add_library(mg_data
  src/data/mzp.cpp
  src/data/mzx.cpp
  src/data/mrg.cpp
  src/data/nam.cpp
  src/data/nxx.cpp
  src/data/hfa.cpp
  src/data/mzpimage.cpp
  src/data/cbg.cpp
  src/data/lenzu.cpp
)
target_include_directories(mg_data PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mg_data PUBLIC
    z
    mg_util
)
set_target_properties(mg_data
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)
#
# Neither Windows nor MSYS2 have a libmman wrapper, so
# one is downloaded, compiled, and installed here, and set as a
# dependency and target link library for mg_util (fs.cpp). Should
# work with MSVC but needs testing (along w/everything else).
#
if(MINGW)
  find_package(PkgConfig)
  pkg_check_modules(MMAN mman) # prevents rebuild if already installed
  if(NOT MMAN_FOUND)
      include(ExternalProject)
      ExternalProject_Add(mman-win32
        GIT_REPOSITORY https://github.com/Dimoks/mman-win32.git
        GIT_TAG 94b1620
        UPDATE_DISCONNECTED 1 # prevents rebuilds on every make/ninja run
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/mman-win32
      )
      ExternalProject_Get_Property(mman-win32 BINARY_DIR)
      set(MMAN_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/mman-win32/include)
      set(MMAN_LIBRARY_DIRS ${CMAKE_BINARY_DIR}/mman-win32/lib)
      set(MMAN_LIBRARIES mman)
      add_dependencies(mg_util mman-win32)
  endif()
  target_include_directories(mg_util PUBLIC ${MMAN_INCLUDE_DIRS})
  target_link_directories(mg_util PUBLIC ${MMAN_LIBRARY_DIRS})
  target_link_libraries(mg_util PRIVATE ${MMAN_LIBRARIES})
endif(MINGW)
#
# Add GUI libs and exe(s); put behind a build flag so that people
# don't need to mess with dependencies as much by default
#
if(BUILD_GUI)
  # OpenGL
  include(FindOpenGL)
  # Glfw for windowing
  find_package(PkgConfig)
  pkg_check_modules(PC_LIBGLFW REQUIRED glfw3)
  add_library(imgui
      STATIC
      vendor/imgui/imgui.cpp
      vendor/imgui/imgui_draw.cpp
      vendor/imgui/imgui_impl_glfw.cpp
      vendor/imgui/imgui_impl_opengl2.cpp
      vendor/imgui/imgui_tables.cpp
      vendor/imgui/imgui_widgets.cpp
  )
  set_target_properties(imgui
      PROPERTIES POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(imgui PUBLIC
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/vendor/gnu
      ${CMAKE_SOURCE_DIR}/vendor/imgui
      ${OPENGL_INCLUDE_DIRS}
      ${PC_LIBGLFW_INCLUDE_DIRS}
  )
  target_link_libraries(imgui
      ${OPENGL_LIBRARIES}
      ${PC_LIBGLFW_LIBRARIES}
  )
  #
  # Add GUI programs
  #
  add_executable(data_explorer
      src/tools/data_explorer.cpp
  )
  target_include_directories(data_explorer PUBLIC
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/vendor/gnu
      ${CMAKE_SOURCE_DIR}/vendor/imgui
      ${OPENGL_INCLUDE_DIRS}
      ${PC_LIBGLFW_INCLUDE_DIRS}
  )
  target_link_libraries(data_explorer
      mg_data
      imgui
  )
endif(BUILD_GUI)
#
# Add CLI programs
#
add_executable(nxx_decompress
    src/tools/nxx_decompress.cpp
)
target_include_directories(nxx_decompress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nxx_decompress
    mg_data
)

add_executable(nxgx_compress
    src/tools/nxgx_compress.cpp
)
target_include_directories(nxgx_compress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nxgx_compress
    mg_data
)

add_executable(nxcx_compress
    src/tools/nxcx_compress.cpp
)
target_include_directories(nxcx_compress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nxcx_compress
    mg_data
)

add_executable(mzx_decompress
    src/tools/mzx_decompress.cpp
)
target_include_directories(mzx_decompress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzx_decompress
    mg_data
)

add_executable(mzx_compress
    src/tools/mzx_compress.cpp
)
target_include_directories(mzx_compress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzx_compress
    mg_data
)

add_executable(mzp_info
    src/tools/mzp_info.cpp
)
target_include_directories(mzp_info PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzp_info
    mg_data
)

add_executable(mzp_extract
    src/tools/mzp_extract.cpp
)
target_include_directories(mzp_extract PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzp_extract
    mg_data
    stdc++fs
)

add_executable(mzp_compress
    src/tools/mzp_compress.cpp
)
target_include_directories(mzp_compress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzp_compress
    mg_data
)

add_executable(mzp_localize
    src/tools/mzp_localize.cpp
)
target_include_directories(mzp_localize PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mzp_localize
    mg_data
)

add_executable(mzpimg_extract
    src/tools/mzpimg_extract.cpp
)
target_include_directories(mzpimg_extract PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${PNG_INCLUDE_DIRS}
    ${IMAGEQUANT_INCLUDE_DIRS}
)
target_link_libraries(mzpimg_extract
    mg_data
    ${PNG_LIBRARIES}
    ${IMAGEQUANT_LIBRARIES}
)

add_executable(mzpimg_compress
    src/tools/mzpimg_compress.cpp
)
target_include_directories(mzpimg_compress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${PNG_INCLUDE_DIRS}
    ${IMAGEQUANT_INCLUDE_DIRS}
)
target_link_libraries(mzpimg_compress
    mg_data
    ${PNG_LIBRARIES}
    ${IMAGEQUANT_LIBRARIES}
)

add_executable(nam_read
    src/tools/nam_read.cpp
)
target_include_directories(nam_read PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(nam_read
    mg_data
)

add_executable(script_text_to_content_json
    src/tools/script_text_to_content_json.cpp
)
target_include_directories(script_text_to_content_json PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(script_text_to_content_json
    mg_data
    ssl
    crypto
)

add_executable(repack_script_text_translation
    src/tools/repack_script_text_translation.cpp
)
target_include_directories(repack_script_text_translation PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/vendor/json
)
target_link_libraries(repack_script_text_translation
    mg_data
    ssl
    crypto
)

add_executable(mrg_extract
    src/tools/mrg_extract.cpp
)
target_include_directories(mrg_extract PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mrg_extract
    mg_data
    stdc++fs
)

add_executable(mrg_pack
    src/tools/mrg_pack.cpp
)
target_include_directories(mrg_pack PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mrg_pack
    mg_data
    stdc++fs
)

add_executable(mrg_info
    src/tools/mrg_info.cpp
)
target_include_directories(mrg_info PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mrg_info
    mg_data
    stdc++fs
)

add_executable(mrg_replace
    src/tools/mrg_replace.cpp
)
target_include_directories(mrg_replace PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mrg_replace
    mg_data
    stdc++fs
)

add_executable(hfa_extract
    src/tools/hfa_extract.cpp
)
target_include_directories(hfa_extract PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(hfa_extract
    mg_data
    stdc++fs
)

add_executable(cbg_convert
    src/tools/cbg_convert.cpp
)
target_include_directories(cbg_convert PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${PNG_INCLUDE_DIRS}
)
target_link_libraries(cbg_convert
    mg_data
    ${PNG_LIBRARIES}
)

add_executable(lenzu_decompress
    src/tools/lenzu_decompress.cpp
)
target_include_directories(lenzu_decompress PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(lenzu_decompress
    mg_data
)

if (WIN32)
  target_link_libraries(script_text_to_content_json ws2_32 crypt32)
  target_link_libraries(repack_script_text_translation ws2_32 crypt32)
endif()

include(cmake/Install.cmake) # installation configuration
