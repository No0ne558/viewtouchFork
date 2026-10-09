# Writes OUT: this build's number (the commits so far, so each newer build
# numbers higher), its commit and the version. Rewritten only when changed,
# so nothing recompiles needlessly. Run at every build (vtm_build_info).
# (safe.directory: in a build container the checkout belongs to someone else.)
execute_process(COMMAND git -c safe.directory=* rev-list --count HEAD WORKING_DIRECTORY "${SRC}"
                OUTPUT_VARIABLE number OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -c safe.directory=* rev-parse --short HEAD WORKING_DIRECTORY "${SRC}"
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT number MATCHES "^[0-9]+$")
    set(number 0)   # not from git (a source tarball): build 0
endif()
set(content "#pragma once\n#define VTM_BUILD_NUMBER ${number}\n#define VTM_BUILD_COMMIT \"${commit}\"\n#define VTM_BUILD_VERSION \"${VERSION}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT "${old}" STREQUAL "${content}")
    file(WRITE "${OUT}" "${content}")
endif()
