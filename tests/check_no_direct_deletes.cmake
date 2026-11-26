# Fails if any source file outside core/storage/OwnFiles.cpp calls an API that deletes files or
# folders directly. Library content may only leave the library through ITrash (SPEC 4.4).
# Usage: cmake -DSRC_DIR=<folder> -P check_no_direct_deletes.cmake

if(NOT SRC_DIR)
    message(FATAL_ERROR "SRC_DIR not set")
endif()

get_filename_component(SRC_DIR "${SRC_DIR}" ABSOLUTE)
file(GLOB_RECURSE sources "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h" "${SRC_DIR}/*.qml" "${SRC_DIR}/*.mm")
set(allowed "${SRC_DIR}/core/storage/OwnFiles.cpp")

set(patterns
    [=[QFile::remove]=]
    [=[[A-Za-z_]\.remove\(\)]=]
    [=[removeRecursively]=]
    [=[QDir::rmdir]=]
    [=[\.rmdir\(]=]
    [=[\.rmpath\(]=]
    [=[DeleteFile[AW]?\(]=]
    [=[RemoveDirectory[AW]?\(]=]
    [=[std::filesystem::remove]=]
    [=[unlink\(]=]
    [=[SHFileOperation]=]
    [=[FOF_ALLOWUNDO]=]
)

set(problems "")
foreach(file ${sources})
    if(file STREQUAL allowed)
        continue()
    endif()
    file(READ "${file}" content)
    foreach(pattern ${patterns})
        if(content MATCHES "${pattern}")
            list(APPEND problems "${file}: matches '${pattern}'")
        endif()
    endforeach()
endforeach()

if(problems)
    string(REPLACE ";" "\n  " text "${problems}")
    message(FATAL_ERROR "Direct file deletion outside OwnFiles.cpp:\n  ${text}")
endif()
message(STATUS "no direct deletes found in ${SRC_DIR}")
