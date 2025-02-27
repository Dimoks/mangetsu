#
# Install.cmake: Installation configuration
#

# Override default installation folder on Windows
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND WIN32)
  set(CMAKE_INSTALL_PREFIX "$ENV{LOCALAPPDATA}" CACHE PATH "Installation prefix on Windows." FORCE)
  set(INSTALL_BIN_DIR "${CMAKE_PROJECT_NAME}" CACHE PATH "Installation directory for executables")
else()
  set(INSTALL_BIN_DIR "bin" CACHE PATH "Installation directory for executables")
endif()

# Get list of targets
get_property(_TARGETS_LIST
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} 
    PROPERTY BUILDSYSTEM_TARGETS
)
# Loop through target list
foreach (_target ${_TARGETS_LIST})
  # Get type of target (executable, shared lib, static lib)
  get_target_property(_target_type ${_target} TYPE)
  #
  # Strip built files of everything except relocation information
  # when building for Release. 
  # ref1: https://www.technovelty.org/linux/stripping-shared-libraries.html
  # ref2: https://stackoverflow.com/a/14176386/
  # src:  https://stackoverflow.com/a/53692364/
  #
  # if() added to prevent mman-win32 ExternalProject from being evaluated
  #
  if (${_target_type} STREQUAL "EXECUTABLE" OR 
      ${_target_type} STREQUAL "SHARED_LIBRARY" OR
      ${_target_type} STREQUAL "STATIC_LIBRARY")
         add_custom_command(
            TARGET ${_target}
            POST_BUILD
            COMMAND $<$<CONFIG:release>:${CMAKE_STRIP}>
            ARGS --strip-unneeded $<TARGET_FILE:${_target}>
          )
   endif()
  # Only install shared libraries and executables, not static libraries.
  if (${_target_type} STREQUAL "EXECUTABLE" OR 
      ${_target_type} STREQUAL "SHARED_LIBRARY")
      install( TARGETS ${_target}
         CONFIGURATIONS Release 
         RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
      )
  endif()
endforeach()
#
# Post-Install Tasks (only Windows for now)
# src: https://stackoverflow.com/a/29979349/
#
if(WIN32)
# Make local vars available to subdirectory's CMakeLists.txt
  install(CODE "set(WORKINGDIR \"${CMAKE_SOURCE_DIR}\")")
  install(CODE "set(INSTALL_BIN_DIR \"${CMAKE_INSTALL_PREFIX}/${INSTALL_BIN_DIR}\")")
  add_subdirectory("${CMAKE_SOURCE_DIR}/cmake/postinstall")
endif()
