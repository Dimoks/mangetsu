if(${CMAKE_VERSION} VERSION_LESS "3.13.0") 
    message("Project requires at CMake 3.13.0 to build properly")
endif()
cmake_minimum_required(VERSION 3.13..3.24)
project(mangetsu LANGUAGES C CXX)

set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/;/usr/local/")

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
if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
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
# Set debug compile options and set default target to "Release". 
#
set(default_build_type "Release")
if(CMAKE_BUILD_TYPE MATCHES Debug)
  add_compile_options(-gdwarf-4 -g3 -g -rdynamic -O0 -fno-omit-frame-pointer)
elseif(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS 
    "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
#
# GCC compiler options for all build targets
#
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas -Werror
    -Wno-error=unused-variable -Wno-error=unused-but-set-variable -Wno-error=pragmas
    -Wno-error=unused-local-typedefs -fPIE)
#
# GCC compiler options/defs specific to mingw64 x86_64
# Guarded by initial if to prevent evaluation on Linux or Windows 
#
if(DEFINED ENV{MSYSTEM})
    if($ENV{MSYSTEM} STREQUAL "MINGW64" OR $ENV{MSYSTEM} STREQUAL "UCRT64")
      add_compile_definitions(_FILE_OFFSET_BITS=64)
    endif()
endif()
#
# Linker options for static and shared builds.
#
if(LINK_STATIC_EXES)
  if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -lwinpthread -static)
  else()
    add_link_options(-static-pie)
  endif()
else()
    if(MINGW)
      add_link_options(-lwinpthread)
    endif()
  add_link_options(-pie)
endif()
#
# Put UI stuff behind a build flag so that people don't have to mess around
# with deps as much by default
#
if(BUILD_GUI)
  # OpenGL
  include(FindOpenGL)
  include_directories(${OPENGL_INCLUDE_DIRS})
  # Glfw for windowing
  find_package(PkgConfig)
  pkg_check_modules(PC_LIBGLFW REQUIRED glfw3)
  include_directories(${PC_LIBGLFW_INCLUDE_DIRS})
endif()
#
# Actually add our target libraries and executables. 
#
include_directories(include)

# JSON library
include_directories(vendor/json)

# Only build imgui lib if UI is enabled
if(BUILD_GUI)
  include_directories(vendor/gnu/)
  include_directories(vendor/imgui/)
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
  target_link_libraries(imgui
     ${OPENGL_LIBRARIES}
     ${PC_LIBGLFW_LIBRARIES}
  )
  #
  # To accomodate format specifiers used by the imgui library, 
  # which don't work with the UCRT64 implementation of printf(),
  # substitute the MINGW implementation of stdio.
  #
  if(DEFINED ENV{MSYSTEM})
      if($ENV{MSYSTEM} STREQUAL "UCRT64")
        target_compile_definitions(imgui INTERFACE 
            __USE_MINGW_ANSI_STDIO)
      endif()
  endif()
endif(BUILD_GUI)
#
# Beginning of general lib/exec add_() section
#
add_library(mg_util
  src/util/fs.cpp
)
#
# Neither Windows nor MSYS2 have a libmman wrapper, so 
# one is downloaded, compiled, and installed here, and set as a 
# dependency and target link library for mg_util (fs.cpp). Should
# work with MSVC but needs testing (along w/everything else).  
#
if(DEFINED ENV{MSYSTEM})
  include(ExternalProject)
  ExternalProject_Add(mman-win32
    GIT_REPOSITORY https://github.com/bilditup1/mman-win32.git
    GIT_TAG 4f4e24d
    UPDATE_DISCONNECTED 1 # prevents rebuilds on every make/ninja run
  )
  add_dependencies(mg_util mman-win32)
  target_link_libraries(mg_util PRIVATE
       mman
  )
endif()

set_target_properties(mg_util
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)
add_library(mg_data
  src/data/mzp.cpp
  src/data/mzx.cpp
  src/data/mrg.cpp
  src/data/nam.cpp
  src/data/nxx.cpp
)
target_link_libraries(mg_data
    z
    mg_util
)
set_target_properties(mg_data    
    PROPERTIES POSITION_INDEPENDENT_CODE ON
)

add_executable(nxx_decompress
    src/tools/nxx_decompress.cpp
)
target_link_libraries(nxx_decompress
    mg_data
)

add_executable(nxgx_compress
    src/tools/nxgx_compress.cpp
)
target_link_libraries(nxgx_compress
    mg_data
)

add_executable(mzx_decompress
    src/tools/mzx_decompress.cpp
)
target_link_libraries(mzx_decompress
    mg_data
)

add_executable(mzx_compress
    src/tools/mzx_compress.cpp
)
target_link_libraries(mzx_compress
    mg_data
)

add_executable(mzp_info
    src/tools/mzp_info.cpp
)
target_link_libraries(mzp_info
    mg_data
)

add_executable(mzp_extract
    src/tools/mzp_extract.cpp
)
target_link_libraries(mzp_extract
    mg_data
    stdc++fs
)

add_executable(mzp_compress
    src/tools/mzp_compress.cpp
)
target_link_libraries(mzp_compress
    mg_data
)

add_executable(nam_read
    src/tools/nam_read.cpp
)
target_link_libraries(nam_read
    mg_data
)

if (BUILD_GUI)
  add_executable(data_explorer
      src/tools/data_explorer.cpp
  )
  target_link_libraries(data_explorer
      mg_data
      imgui
  )
endif()

add_executable(script_text_to_content_json
    src/tools/script_text_to_content_json.cpp
)
target_link_libraries(script_text_to_content_json
    mg_data
    ssl
    crypto
)

add_executable(repack_script_text_translation
    src/tools/repack_script_text_translation.cpp
)
target_link_libraries(repack_script_text_translation
    mg_data
    ssl
    crypto
)

add_executable(mrg_extract
    src/tools/mrg_extract.cpp
)
target_link_libraries(mrg_extract
    mg_data
    stdc++fs
)

add_executable(mrg_pack
    src/tools/mrg_pack.cpp
)
target_link_libraries(mrg_pack
    mg_data
    stdc++fs
)

add_executable(mrg_info
    src/tools/mrg_info.cpp
)
target_link_libraries(mrg_info
    mg_data
    stdc++fs
)

add_executable(mrg_replace
    src/tools/mrg_replace.cpp
)
target_link_libraries(mrg_replace
    mg_data
    stdc++fs
)
#
# install_cfg.cmake uses features introduced in cmake 3.21.0
#
if(NOT ${CMAKE_VERSION} VERSION_LESS "3.21.0") 
    include(cmake/install_cfg.cmake)
else() 
    message("Installation configuration requires cmake 3.21 or higher")
endif()