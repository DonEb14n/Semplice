# Helpers shared by tests and the command-line tools below.
add_library(semplice_testtools STATIC
    IntegrityChecker.h
    IntegrityChecker.cpp
    SyntheticLibrary.h
    SyntheticLibrary.cpp
    TestAudioFiles.h
    TestAudioFiles.cpp
    PlaybackFakes.h
)
set_target_properties(semplice_testtools PROPERTIES AUTOMOC ON)
target_include_directories(semplice_testtools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(semplice_testtools PUBLIC semplice_core Qt6::Core Qt6::Sql)
semplice_set_warnings(semplice_testtools)

# semplice_gen: writes a synthetic library database (50k / 200k presets).
qt_add_executable(semplice_gen gen_main.cpp)
target_link_libraries(semplice_gen PRIVATE semplice_testtools)
semplice_set_warnings(semplice_gen)

# semplice_bench: generates a library and measures the SPEC 9 budgets that exist at this milestone.
qt_add_executable(semplice_bench bench_main.cpp)
target_link_libraries(semplice_bench PRIVATE semplice_testtools)
semplice_set_warnings(semplice_bench)
if(WIN32)
    target_link_libraries(semplice_bench PRIVATE psapi)
endif()

# semplice_libtool: import / rescan / check a REAL library from the command line (Windows: real Recycle Bin).
if(TARGET semplice_platform)
    qt_add_executable(semplice_libtool libtool_main.cpp)
    target_link_libraries(semplice_libtool PRIVATE semplice_testtools semplice_platform)
    semplice_set_warnings(semplice_libtool)
endif()

# semplice_playtool: plays a real library with the real audio device, SMTC and thumbnail buttons.
if(TARGET semplice_platform)
    qt_add_executable(semplice_playtool playtool_main.cpp)
    target_link_libraries(semplice_playtool PRIVATE semplice_testtools semplice_platform Qt6::Gui)
    semplice_set_warnings(semplice_playtool)
endif()

# semplice_recotool: prints the best recommendation candidates with their score components (to tune the weights).
qt_add_executable(semplice_recotool recotool_main.cpp)
target_link_libraries(semplice_recotool PRIVATE semplice_testtools)
semplice_set_warnings(semplice_recotool)

# semplice_lockcheck: can a file be renamed / deleted while Qt Multimedia plays it? (answers how much "release" is needed)
if(TARGET semplice_platform)
    qt_add_executable(semplice_lockcheck lockcheck_main.cpp)
    target_link_libraries(semplice_lockcheck PRIVATE Qt6::Multimedia Qt6::Core)
endif()

# semplice_stopcheck: how long stop() / setSource() block while a file plays (diagnostic for the "freeze on release")
if(TARGET semplice_platform)
    qt_add_executable(semplice_stopcheck stopcheck_main.cpp)
    target_link_libraries(semplice_stopcheck PRIVATE Qt6::Multimedia Qt6::Gui Qt6::Core)
endif()

# semplice_startcheck: time from play() to the first sound with the real backend (diagnostic for slow starts)
if(TARGET semplice_platform)
    qt_add_executable(semplice_startcheck startcheck_main.cpp)
    target_link_libraries(semplice_startcheck PRIVATE semplice_platform Qt6::Multimedia Qt6::Gui Qt6::Core)
endif()

# semplice_sharecheck: rename / delete while Qt Multimedia plays from a FILE_SHARE_DELETE handle
if(WIN32)
    qt_add_executable(semplice_sharecheck sharecheck_main.cpp)
    target_link_libraries(semplice_sharecheck PRIVATE semplice_platform Qt6::Multimedia Qt6::Core)
endif()
