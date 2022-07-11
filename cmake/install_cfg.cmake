#
# Installation configuration
#
if (NOT SKIP_INSTALL_ALL)
    #
    # Configure install destinations
    #
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND ${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
        set(CMAKE_INSTALL_PREFIX $ENV{LOCALAPPDATA} CACHE PATH "Installation prefix on Windows." FORCE)
        set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_PROJECT_NAME}" CACHE PATH "Installation directory for executables")
    else()
        set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installation directory for executables")
    endif()
    #
    # Actually add install destination for each binary
    #
    get_property(_TARGETS_LIST
        DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} 
        PROPERTY BUILDSYSTEM_TARGETS
    )
    foreach (_target ${_TARGETS_LIST})
        get_target_property(_target_type ${_target} TYPE)
        #
        # Only install shared libraries and executables, not static libraries.
        #
         if (NOT ${_target_type} STREQUAL "STATIC_LIBRARY")
            install( TARGETS ${_target}
                CONFIGURATIONS Release 
                RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
            )
         endif()
        #
        # Strip built files of everything except relocation information
        # when building for Release. 
        # ref1: https://www.technovelty.org/linux/stripping-shared-libraries.html
        # ref2: https://stackoverflow.com/a/14176386/
        # src:  https://stackoverflow.com/a/53692364/
        #
        add_custom_command(
           TARGET ${_target} DEPENDS ${_target}
           POST_BUILD
           COMMAND $<$<CONFIG:release>:${CMAKE_STRIP}>
           ARGS --strip-unneeded $<TARGET_FILE:${_target}>
         )
     endforeach()
     #
     # Post-Install Tasks (only Windows for now)
     # src:      https://stackoverflow.com/a/29979349/
     #
	 if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
        # Make local vars available to subdirectory's CMakeLists.txt
        install(CODE "set(WORKINGDIR \"${CMAKE_SOURCE_DIR}\")")
        install(CODE "set(INSTALL_BIN_DIR \"${INSTALL_BIN_DIR}\")")
        add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/postinstall)
     endif()
endif(NOT SKIP_INSTALL_ALL)
