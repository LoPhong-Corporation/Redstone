# Shared compiler flags for every target in this project.
# Usage: msm_apply_compiler_options(<target>)

function(msm_apply_compiler_options target)
    if (MSVC)
        target_compile_options(${target} PRIVATE
            /W4          # high warning level
            /permissive- # strict standard conformance
            /utf-8       # source + execution charset = UTF-8 (Vietnamese literals, emoji)
            /Zc:__cplusplus
            /MP          # parallel compilation
        )
        if (MSM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if (MSM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
