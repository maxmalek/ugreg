function(prepare_copy_target target where)
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${where}")
endfunction()

function(postbuild_copy_target target where)
    if(BUILD_COPY_BINARIES)
        prepare_copy_target("${target}" "${where}")
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${target}>"
                "${where}")
    endif()
endfunction()

function(postbuild_copy_target_exe target)
    postbuild_copy_target("${target}" "${BUILD_BINDIR}/${target}")
endfunction()
