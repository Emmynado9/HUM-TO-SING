if(UNIX)
    foreach(package_file IN LISTS CPACK_PACKAGE_FILES)
        if(EXISTS "${package_file}")
            file(CHMOD "${package_file}"
                PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
        endif()
    endforeach()
endif()
