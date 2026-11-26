# Fails when a QML file sets user-visible text from a plain string literal instead of qsTr()
# (SPEC 11, M10). Usage: cmake -DSRC_DIR=<folder> -P check_strings.cmake
# Legitimate exceptions (names, symbols, colours) are listed in tests/string_check_allow.txt as
# "<file name>|<text>" lines.

if(NOT SRC_DIR)
    message(FATAL_ERROR "SRC_DIR not set")
endif()
get_filename_component(SRC_DIR "${SRC_DIR}" ABSOLUTE)
get_filename_component(HERE "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
file(STRINGS "${HERE}/string_check_allow.txt" allow)
file(GLOB_RECURSE sources "${SRC_DIR}/*.qml")

set(patterns
    [=[(^|[ {;])(text|title|placeholderText|tooltip|label|description|windowTitle)[ ]*:[ ]*"[^"#]*[A-Za-z]]=]
    [=["(label|title|text|tooltip)"[ ]*:[ ]*"[^"]*[A-Za-z]]=]
)

set(problems "")
foreach(file ${sources})
    get_filename_component(name "${file}" NAME)
    file(STRINGS "${file}" lines)
    set(n 0)
    foreach(line ${lines})
        math(EXPR n "${n} + 1")
        if(line MATCHES "^[ ]*//")
            continue()
        endif()
        foreach(p ${patterns})
            if(line MATCHES "${p}")
                set(ok FALSE)
                foreach(a ${allow})
                    string(REPLACE "|" ";" parts "${a}")
                    list(GET parts 0 aname)
                    list(GET parts 1 atext)
                    if(name STREQUAL aname AND line MATCHES "\"${atext}\"")
                        set(ok TRUE)
                    endif()
                endforeach()
                if(NOT ok)
                    string(APPEND problems "${file}:${n}: ${line}\n")
                endif()
            endif()
        endforeach()
    endforeach()
endforeach()

if(problems)
    message(FATAL_ERROR "User-visible text outside qsTr():\n${problems}")
endif()
