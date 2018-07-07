if(NOT SET_UP_CONFIGURATIONS_DONE)
    set(SET_UP_CONFIGURATIONS_DONE 1)

    # No reason to set CMAKE_CONFIGURATION_TYPES if it's not a multiconfig generator
    # Also no reason mess with CMAKE_BUILD_TYPE if it's a multiconfig generator.
    if(CMAKE_CONFIGURATION_TYPES) # multiconfig generator?
        set(CMAKE_CONFIGURATION_TYPES "Release;Debug" CACHE STRING "" FORCE) 
    else()
        if(NOT CMAKE_BUILD_TYPE)
            message("\nDefaulting to Release build.\n")
            set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
        endif()
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY HELPSTRING "Choose the type of build")
        # Set the valid options for cmake-gui drop-down list
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Release;Debug")
    endif()	
	
    # Set up the Release and Debug flags if not defined	
	if(NOT CMAKE_C_FLAGS_DEBUG)
		set(CMAKE_C_FLAGS_PROFILE "-g -Wall")
	endif()
	if(NOT CMAKE_C_FLAGS_RELEASE)
		set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
	endif()
	if(NOT CMAKE_CXX_FLAGS_DEBUG)
		set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")
	endif()
	if(NOT CMAKE_CXX_FLAGS_RELEASE)
		set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
	endif()
endif()
